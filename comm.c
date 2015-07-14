/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdio.h>
#include <stdlib.h>

#include <uv.h>

#include <chimaera.h>
#include <osc.h>
#include <osc_stream.h>
#include <lv2_osc.h>
#include <clock_sync.h>
#include <varchunk.h>
#include <tlsf.h>

#if defined(_WIN32)
#	include <avrt.h>
#endif

#define POOL_SIZE 0x20000 // 128KB
#define BUF_SIZE 0x10000

typedef enum _plugstate_t plugstate_t;
typedef struct _list_t list_t;
typedef struct _pos_t pos_t;
typedef struct _dummy_ref_t dummy_ref_t;
typedef struct _tuio2_ref_t tuio2_ref_t;
typedef struct _handle_t handle_t;

struct _pos_t {
	uint64_t stamp;

	float x;
	float z;
	float a;
	struct {
		float f1;
		float f11;
	} vx;
	struct {
		float f1;
		float f11;
	} vz;
	float v;
	float A;
	float m;
	float R;
};

struct _dummy_ref_t {
	uint32_t gid;
	uint32_t pid;

	pos_t pos;
};

enum _plugstate_t {
	STATE_IDLE					= 0,
	STATE_READY					= 1,
	STATE_TIMEDOUT			= 2,
	STATE_ERRORED				= 3,
	STATE_CONNECTED			= 4
};

struct _tuio2_ref_t {
	uint32_t gid;
	uint32_t tuid;

	pos_t pos;
};

struct _list_t {
	list_t *next;
	uint64_t frames;

	size_t size;
	osc_data_t buf [0];
};

struct _handle_t {
	LV2_URID_Map *map;
	chimaera_forge_t cforge;
	osc_forge_t oforge;

	struct {
		LV2_URID chimaera_comm_url;
		LV2_URID chimaera_data_url;
	} uris;

	float rate;
	float s;
	float sm1;
	uint64_t stamp;

	struct {
		chimaera_dict_t dict [CHIMAERA_DICT_SIZE];
		dummy_ref_t ref [CHIMAERA_DICT_SIZE];
	} dummy;

	struct {
		chimaera_dict_t dict [2][CHIMAERA_DICT_SIZE];
		tuio2_ref_t ref [2][CHIMAERA_DICT_SIZE];
		int pos;

		uint32_t fid;
		osc_time_t last;
		uint16_t width;
		uint16_t height;
		int ignore;
		int n;
		int reset;
	} tuio2;

	LV2_Atom_Sequence *event_out;
	const float *reset_in;
	const float *through_in;
	float *comm_state;
	float *data_state;
	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;

	uv_loop_t loop;

	LV2_Worker_Schedule *sched;
	LV2_Worker_Respond_Function respond;
	LV2_Worker_Respond_Handle target;
	
	Clock_Sync_Schedule *clock_sched;
	uint64_t last_frame;
	list_t *list;
	uint8_t mem [POOL_SIZE];
	tlsf_t tlsf;

	struct {
		osc_stream_driver_t driver;
		osc_stream_t *stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
		volatile int reconnection_requested;
		volatile int restored;
		volatile int needs_flushing;
		char url [512];
	} comm;
	
	struct {
		osc_stream_driver_t driver;
		osc_stream_t *stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
		volatile int reconnection_requested;
		volatile int restored;
		char url [512];
		int frame_cnt;
		LV2_Atom_Forge_Frame frame [32][2]; // 32 nested bundles should be enough
	} data;
};

const char flush_msg [] = "/chimaera/flush\0,\0\0\0";
const char recv_msg [] = "/chimaera/recv\0\0,\0\0\0";

static inline list_t *
_list_insert(list_t *root, list_t *item)
{
	if(!root || (item->frames < root->frames) ) // prepend
	{
		item->next = root;
		return item;
	}

	list_t *l0;
	for(l0 = root; l0->next != NULL; l0 = l0->next)
	{
		if(item->frames < l0->next->frames)
			break; // found insertion point
	}

	item->next = l0->next; // is NULL at end of list
	l0->next = item;
	return root;
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = (handle_t *)instance;
	LV2_State_Status status;

	status = store(
		state,
		handle->uris.chimaera_comm_url,
		handle->comm.url,
		strlen(handle->comm.url) + 1,
		handle->cforge.forge.String,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
	if(status != LV2_STATE_SUCCESS)
		return status;

	status = store(
		state,
		handle->uris.chimaera_data_url,
		handle->data.url,
		strlen(handle->data.url) + 1,
		handle->cforge.forge.String,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

	return status;
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = (handle_t *)instance;

	size_t size;
	uint32_t type;
	uint32_t flags2;

	// retrieve comm_url
	const char *comm_url = retrieve(
		state,
		handle->uris.chimaera_comm_url,
		&size,
		&type,
		&flags2
	);

	// check
	if(!comm_url)
		return LV2_STATE_ERR_NO_PROPERTY;
	if(type != handle->cforge.forge.String)
		return LV2_STATE_ERR_BAD_TYPE;

	strcpy(handle->comm.url, comm_url);
	handle->comm.restored = 1;

	//printf("comm.url: %s\n", comm_url);

	// retrieve data_url
	const char *data_url = retrieve(
		state,
		handle->uris.chimaera_data_url,
		&size,
		&type,
		&flags2
	);

	// check type
	if(!data_url)
		return LV2_STATE_ERR_NO_PROPERTY;
	if(type != handle->cforge.forge.String)
		return LV2_STATE_ERR_BAD_TYPE;

	strcpy(handle->data.url, data_url);
	handle->data.restored = 1;

	//printf("data.url: %s\n", data_url);

	return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt
static void *
_comm_recv_req(size_t size, void *data)
{
	handle_t *handle = data;

	void *ptr;
	do ptr = varchunk_write_request(handle->comm.from_worker, size);
	while(!ptr);

	return ptr;
}

// non-rt
static void
_comm_recv_adv(size_t written, void *data)
{
	handle_t *handle = data;

	varchunk_write_advance(handle->comm.from_worker, written);
}

// non-rt
static const void *
_comm_send_req(size_t *len, void *data)
{
	handle_t *handle = data;

	return varchunk_read_request(handle->comm.to_worker, len);
}

// non-rt
static void
_comm_send_adv(void *data)
{
	handle_t *handle = data;

	return varchunk_read_advance(handle->comm.to_worker);
}

// non-rt
static void
_comm_free(void *data)
{
	handle_t *handle = data;

	handle->comm.stream = NULL;
	handle->comm.reconnection_requested = 1;
}

// non-rt
static void *
_data_recv_req(size_t size, void *data)
{
	handle_t *handle = data;

	void *ptr;
	do ptr = varchunk_write_request(handle->data.from_worker, size);
	while(!ptr);

	return ptr;
}

// non-rt
static void
_data_recv_adv(size_t written, void *data)
{
	handle_t *handle = data;

	varchunk_write_advance(handle->data.from_worker, written);
}

// non-rt
static const void *
_data_send_req(size_t *len, void *data)
{
	handle_t *handle = data;

	return varchunk_read_request(handle->data.to_worker, len);
}

// non-rt
static void
_data_send_adv(void *data)
{
	handle_t *handle = data;

	return varchunk_read_advance(handle->data.to_worker);
}

// non-rt
static void
_data_free(void *data)
{
	handle_t *handle = data;

	handle->data.stream = NULL;
	handle->data.reconnection_requested = 1;
}

// rt
static int
_comm_resolve(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->comm_state = STATE_READY;

	return 1;
}

// rt
static int
_comm_timeout(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->comm_state = STATE_TIMEDOUT;

	return 1;
}

// rt
static int
_comm_error(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->comm_state = STATE_ERRORED;

	return 1;
}

// rt
static int
_comm_connect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->comm_state = STATE_CONNECTED;

	return 1;
}

// rt
static int
_comm_disconnect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->comm_state = STATE_READY;

	return 1;
}

// rt
static void
_osc_atom_serialize(handle_t *handle, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Frame frame [2];

	const osc_data_t *ptr = buf;

	lv2_atom_forge_frame_time(forge, 0); //TODO
	osc_forge_message_push(&handle->oforge, forge, frame, path, fmt);

	for(const char *type = fmt; *type; type++)
		switch(*type)
		{
			case 'i':
			{
				int32_t i;
				if((ptr = osc_get_int32(ptr, &i)))
					osc_forge_int32(&handle->oforge, forge, i);
				break;
			}
			case 'f':
			{
				float f;
				if((ptr = osc_get_float(ptr, &f)))
					osc_forge_float(&handle->oforge, forge, f);
				break;
			}
			case 's':
			case 'S':
			{
				const char *s;
				if((ptr = osc_get_string(ptr, &s)))
					osc_forge_string(&handle->oforge, forge, s);
				break;
			}
			case 'b':
			{
				osc_blob_t b;
				if((ptr = osc_get_blob(ptr, &b)))
					osc_forge_blob(&handle->oforge, forge, b.size, b.payload);
				break;
			}

			case 'h':
			{
				int64_t h;
				if((ptr = osc_get_int64(ptr, &h)))
					osc_forge_int64(&handle->oforge, forge, h);
				break;
			}
			case 'd':
			{
				double d;
				if((ptr = osc_get_double(ptr, &d)))
					osc_forge_double(&handle->oforge, forge, d);
				break;
			}
			case 't':
			{
				uint64_t t;
				if((ptr = osc_get_timetag(ptr, &t)))
					osc_forge_timestamp(&handle->oforge, forge, t);
				break;
			}

			case 'T':
			case 'F':
			case 'N':
			case 'I':
			{
				break;
			}
			
			case 'c':
			{
				char c;
				if((ptr = osc_get_char(ptr, &c)))
					osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				const uint8_t *m;
				if((ptr = osc_get_midi(ptr, &m)))
					osc_forge_midi(&handle->oforge, forge, 3, m + 1);
				break;
			}
		}

	osc_forge_message_pop(&handle->oforge, forge, frame);
}

// rt
static int
_comm_method(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	_osc_atom_serialize(handle, path, fmt, buf, size);	

	return 1;
}

// rt
static inline void
_chim_event(handle_t *handle, osc_time_t frames, chimaera_event_t *cev)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;

	lv2_atom_forge_frame_time(forge, frames);
	chimaera_event_forge(&handle->cforge, cev);
}

// rt
static int
_data_resolve(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->data_state = STATE_READY;

	return 1;
}

// rt
static int
_data_timeout(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->data_state = STATE_TIMEDOUT;

	return 1;
}

// rt
static int
_data_error(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->data_state = STATE_ERRORED;

	return 1;
}

// rt
static int
_data_connect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->data_state = STATE_CONNECTED;

	return 1;
}

// rt
static int
_data_disconnect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	*handle->data_state = STATE_READY;

	return 1;
}

// rt
static int
_data_through(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	if(*handle->through_in != 0.f)
		_osc_atom_serialize(handle, path, fmt, buf, size);	

	return 0;
}

static inline void
_pos_init(pos_t *dst, uint64_t stamp)
{
	dst->stamp = stamp;
	dst->x = 0.f;
	dst->z = 0.f;
	dst->a = 0.f;
	dst->vx.f1 = 0.f;
	dst->vx.f11 = 0.f;
	dst->vz.f1 = 0.f;
	dst->vz.f11 = 0.f;
	dst->v = 0.f;
	dst->A = 0.f;
	dst->m = 0.f;
	dst->R = 0.f;
	
	//memset(dst, 0x0, sizeof(pos_t));
}

static inline void
_pos_clone(pos_t *dst, pos_t *src)
{
	dst->stamp = src->stamp;
	dst->x = src->x;
	dst->z = src->z;
	dst->a = src->a;
	dst->vx.f1 = src->vx.f1;
	dst->vx.f11 = src->vx.f11;
	dst->vz.f1 = src->vz.f1;
	dst->vz.f11 = src->vz.f11;
	dst->v = src->v;
	dst->A = src->A;
	dst->m = src->m;
	dst->R = src->R;
	
	//memcpy(dst, src, sizeof(pos_t));
}

static inline void
_pos_deriv(handle_t *handle, pos_t *neu, pos_t *old)
{
	if(neu->stamp <= old->stamp)
	{
		neu->stamp = old->stamp;
		neu->vx.f1 = old->vx.f1;
		neu->vx.f11 = old->vx.f11;
		neu->vz.f1 = old->vz.f1;
		neu->vz.f11 = old->vz.f11;
		neu->v = old->v;
		neu->A = old->A;
		neu->m = old->m;
		neu->R = old->R;
	}
	else
	{
		float rate = handle->rate / (neu->stamp - old->stamp);

		float dx = neu->x - old->x;
		neu->vx.f1 = dx * rate;

		float dz = neu->z - old->z;
		neu->vz.f1 = dz * rate;

		// first-order IIR filter
		neu->vx.f11 = handle->s*(neu->vx.f1 + old->vx.f1) + old->vx.f11*handle->sm1;
		neu->vz.f11 = handle->s*(neu->vz.f1 + old->vz.f1) + old->vz.f11*handle->sm1;

		neu->v = sqrtf(neu->vx.f11 * neu->vx.f11 + neu->vz.f11 * neu->vz.f11);

		float dv =  neu->v - old->v;
		neu->A = 0.f;
		neu->m = dv * rate;
		neu->R = 0.f;
	}
}

// rt
static int
_tuio2_frm(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	const osc_data_t *ptr = buf;
	uint32_t fid;
	osc_time_t last;

	ptr = osc_get_int32(ptr, (int32_t *)&fid);
	ptr = osc_get_timetag(ptr, &last);

	if( (fid > handle->tuio2.fid) && (last >= handle->tuio2.last) ) //TODO handle immediate
	{
		uint32_t dim;
		//const char *source;

		handle->tuio2.fid = fid;
		handle->tuio2.last = last;

		ptr = osc_get_int32(ptr, (int32_t *)&dim);
		handle->tuio2.width = dim >> 16;
		handle->tuio2.height = dim & 0xffff;
		
		//ptr = osc_get_string(ptr, &source);

		handle->tuio2.pos ^= 1; // toggle pos
		chimaera_dict_clear(handle->tuio2.dict[handle->tuio2.pos]);

		handle->tuio2.ignore = 0;
	}
	else
		handle->tuio2.ignore = 1;

	return 1;
}

// rt
static int
_tuio2_tok(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	pos_t pos;
	_pos_init(&pos, handle->stamp);

	int has_derivatives = strlen(fmt) == 11;

	const osc_data_t *ptr = buf;
	tuio2_ref_t *ref;

	if(handle->tuio2.ignore)
		return 1;

	uint32_t sid;
	ptr = osc_get_int32(ptr, (int32_t *)&sid);
	
	ref = chimaera_dict_add(handle->tuio2.dict[handle->tuio2.pos], sid); // get new blob ref
	if(!ref)
		return 1;

	ptr = osc_get_int32(ptr, (int32_t *)&ref->tuid);
	ptr = osc_get_int32(ptr, (int32_t *)&ref->gid);
	ptr = osc_get_float(ptr, &pos.x);
	ptr = osc_get_float(ptr, &pos.z);
	ptr = osc_get_float(ptr, &pos.a);

	if(has_derivatives)
	{
		ptr = osc_get_float(ptr, &pos.vx.f11);
		ptr = osc_get_float(ptr, &pos.vz.f11);
		ptr = osc_get_float(ptr, &pos.A);
		ptr = osc_get_float(ptr, &pos.m);
		ptr = osc_get_float(ptr, &pos.R);
	}
	else // !has_derivatives
	{
		_pos_deriv(handle, &pos, &ref->pos);
	}

	_pos_clone(&ref->pos, &pos);

	return 1;
}

// rt
static int
_tuio2_alv(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	const osc_data_t *ptr = buf;
	chimaera_event_t cev;
	int n;
	uint32_t sid;
	tuio2_ref_t *dst;
	tuio2_ref_t *src;

	if(handle->tuio2.ignore)
		return 1;

	n = strlen(fmt);

	for(int i=0; i<n; i++)
	{
		ptr = osc_get_int32(ptr, (int32_t *)&sid);

		// already registered in this step?
		dst = chimaera_dict_ref(handle->tuio2.dict[handle->tuio2.pos], sid);
		if(!dst)
		{
			// register in this step
			dst = chimaera_dict_add(handle->tuio2.dict[handle->tuio2.pos], sid);
			// clone from previous step
			src = chimaera_dict_ref(handle->tuio2.dict[!handle->tuio2.pos], sid);
			if(dst && src)
				memcpy(dst, src, sizeof(tuio2_ref_t));
		}
	}

	// iterate over last step's blobs
	CHIMAERA_DICT_FOREACH(handle->tuio2.dict[!handle->tuio2.pos], sid, src)
	{
		// is it registered in this step?
		if(!chimaera_dict_ref(handle->tuio2.dict[handle->tuio2.pos], sid))
		{
			// is disappeared blob
			cev.state = CHIMAERA_STATE_OFF;
			cev.sid = sid;
			cev.gid = src->gid;
			cev.pid = src->tuid & 0xffff;
			cev.x = src->pos.x;
			cev.z = src->pos.z;
			cev.X = src->pos.vx.f11;
			cev.Z = src->pos.vz.f11;

			_chim_event(handle, timestamp, &cev);
		}
	}

	// iterate over this step's blobs
	CHIMAERA_DICT_FOREACH(handle->tuio2.dict[handle->tuio2.pos], sid, dst)
	{
		cev.sid = sid;
		cev.gid = dst->gid;
		cev.pid = dst->tuid & 0xffff;
		cev.x = dst->pos.x;
		cev.z = dst->pos.z;
		cev.X = dst->pos.vx.f11;
		cev.Z = dst->pos.vz.f11;

		// was it registered in previous step?
		if(!chimaera_dict_ref(handle->tuio2.dict[!handle->tuio2.pos], sid))
			cev.state = CHIMAERA_STATE_ON;
		else
			cev.state = CHIMAERA_STATE_SET;

		_chim_event(handle, timestamp, &cev);
	}

	if(!n && !handle->tuio2.n)
	{
		// is idling
		cev.state = CHIMAERA_STATE_IDLE;
		cev.sid = 0;
		cev.gid = 0;
		cev.pid = 0;
		cev.x = 0.f;
		cev.z = 0.f;
		cev.X = 0.f;
		cev.Z = 0.f;

		_chim_event(handle, timestamp, &cev);
	}

	handle->tuio2.n = n;

	return 1;
}

// rt
static int
_dummy_on(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	pos_t pos;
	_pos_init(&pos, handle->stamp);

	int has_derivatives = strlen(fmt) == 7;

	const osc_data_t *ptr = buf;
	chimaera_event_t cev;
	dummy_ref_t *ref;
	
	cev.state = CHIMAERA_STATE_ON;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.sid);
	ref = chimaera_dict_add(handle->dummy.dict, cev.sid);
	if(!ref)
		return 1;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.gid);
	ref->gid = cev.gid;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.pid);
	ref->pid = cev.pid;

	ptr = osc_get_float(ptr, &pos.x);
	ptr = osc_get_float(ptr, &pos.z);

	if(has_derivatives)
	{
		ptr = osc_get_float(ptr, &pos.vx.f11);
		ptr = osc_get_float(ptr, &pos.vz.f11);
	}

	_pos_clone(&ref->pos, &pos);
	cev.x = ref->pos.x;
	cev.z = ref->pos.z;
	cev.X = ref->pos.vx.f11;
	cev.Z = ref->pos.vz.f11;

	_chim_event(handle, timestamp, &cev);

	return 1;
}

// rt
static int
_dummy_off(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//int is_redundant = strlen(fmt) == 3;

	const osc_data_t *ptr = buf;
	chimaera_event_t cev;
	dummy_ref_t *ref;
	
	cev.state = CHIMAERA_STATE_OFF;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.sid);
	ref = chimaera_dict_del(handle->dummy.dict, cev.sid);
	if(!ref)
		return 1;

	cev.gid = ref->gid;
	cev.pid = ref->pid;
	cev.x = ref->pos.x;
	cev.z = ref->pos.z;
	cev.X = ref->pos.vx.f11;
	cev.Z = ref->pos.vz.f11;

	_chim_event(handle, timestamp, &cev);

	return 1;
}

// rt
static int
_dummy_set(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	pos_t pos;
	_pos_init(&pos, handle->stamp);

	int is_redundant = fmt[2] == 'i';
	int has_derivatives = is_redundant
		? (strlen(fmt) == 7)
		: (strlen(fmt) == 5);

	const osc_data_t *ptr = buf;
	chimaera_event_t cev;
	dummy_ref_t *ref;
	
	cev.state = CHIMAERA_STATE_SET;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.sid);
	ref = chimaera_dict_ref(handle->dummy.dict, cev.sid);
	if(!ref)
		return 1;

	if(is_redundant)
	{
		int32_t _gid, _pid;
		ptr = osc_get_int32(ptr, &_gid);
		ptr = osc_get_int32(ptr, &_pid);
	}

	cev.gid = ref->gid;
	cev.pid = ref->pid;

	ptr = osc_get_float(ptr, &pos.x);
	ptr = osc_get_float(ptr, &pos.z);

	if(has_derivatives)
	{
		ptr = osc_get_float(ptr, &pos.vx.f11);
		ptr = osc_get_float(ptr, &pos.vz.f11);
	}
	else
	{
		_pos_deriv(handle, &pos, &ref->pos);
	}

	_pos_clone(&ref->pos, &pos);
	cev.x = ref->pos.x;
	cev.z = ref->pos.z;
	cev.X = ref->pos.vx.f11;
	cev.Z = ref->pos.vz.f11;

	_chim_event(handle, timestamp, &cev);

	return 1;
}

// rt
static int
_dummy_idle(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	chimaera_event_t cev;
	
	cev.state = CHIMAERA_STATE_IDLE;
	
	cev.sid = 0;
	cev.gid = 0;
	cev.pid = 0;
	cev.x = 0.f;
	cev.z = 0.f;
	cev.X = 0.f;
	cev.Z = 0.f;

	_chim_event(handle, timestamp, &cev);

	chimaera_dict_clear(handle->dummy.dict);

	return 1;
}

// rt
static int
_dump(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	LV2_Atom_Forge *forge = &handle->cforge.forge;
	const osc_data_t *ptr = buf;
	uint32_t sensors;
	int32_t values[160];

	uint32_t fid;
	osc_blob_t b;

	ptr = osc_get_int32(ptr, (int32_t *)&fid);
	ptr = osc_get_blob(ptr, &b);
	const int16_t *payload = b.payload;

	sensors = b.size / sizeof(int16_t);
	for(int i=0; i<sensors; i++)
	{
		int16_t val = be16toh(payload[i]);
		values[i] = val;
	}

	lv2_atom_forge_frame_time(forge, timestamp);
	chimaera_dump_forge(&handle->cforge, values, sensors);

	return 1;
}

static const osc_method_t comm_methods [] = {
	{"/stream/resolve", "", _comm_resolve},
	{"/stream/timeout", "", _comm_timeout},
	{"/stream/error", "ss", _comm_error},
	{"/stream/connect", "", _comm_connect},
	{"/stream/disconnect", "", _comm_disconnect},

	{NULL, NULL, _comm_method},

	{NULL, NULL, NULL}
};

static const osc_method_t data_methods [] = {
	{"/stream/resolve", "", _data_resolve},
	{"/stream/timeout", "", _data_timeout},
	{"/stream/error", "ss", _data_error},
	{"/stream/connect", "", _data_connect},
	{"/stream/disconnect", "", _data_disconnect},

	{NULL, NULL, _data_through},

	{"/tuio2/frm", "itis", _tuio2_frm},
	{"/tuio2/tok", "iiifff", _tuio2_tok},
	{"/tuio2/tok", "iiiffffffff", _tuio2_tok},
	{"/tuio2/alv", NULL, _tuio2_alv},

	{"/on", "iiiff", _dummy_on},
	{"/on", "iiiffff", _dummy_on},
	{"/off", "i", _dummy_off},
	{"/off", "iii", _dummy_off},
	{"/set", "iff", _dummy_set},
	{"/set", "iffff", _dummy_set},
	{"/set", "iiiff", _dummy_set},
	{"/set", "iiiffff", _dummy_set},
	{"/idle", "", _dummy_idle},
	
	{"/dump", "ib", _dump},

	{NULL, NULL, NULL}
};

// rt-thread
static void
_ui_recv(const char *path, const char *fmt, const LV2_Atom_Tuple *args,
	void *data)
{
	handle_t *handle = data;

	size_t reserve = osc_strlen(path) + osc_strlen(fmt) + args->atom.size;

	osc_data_t *buf;
	if((buf = varchunk_write_request(handle->comm.to_worker, reserve)))
	{
		osc_data_t *ptr = buf;
		const osc_data_t *end = buf + reserve;

		ptr = osc_set_path(ptr, end, path);
		ptr = osc_set_fmt(ptr, end, fmt);

		const LV2_Atom *itr = lv2_atom_tuple_begin(args);
		for(const char *type = fmt;
			*type && !lv2_atom_tuple_is_end(LV2_ATOM_BODY(args), args->atom.size, itr);
			type++, itr = lv2_atom_tuple_next(itr))
		{
			switch(*type)
			{
				case 'i':
				{
					ptr = osc_set_int32(ptr, end, ((const LV2_Atom_Int *)itr)->body);
					break;
				}
				case 'f':
				{
					ptr = osc_set_float(ptr, end, ((const LV2_Atom_Float *)itr)->body);
					break;
				}
				case 's':
				case 'S':
				{
					ptr = osc_set_string(ptr, end, LV2_ATOM_BODY_CONST(itr));
					break;
				}
				case 'b':
				{
					ptr = osc_set_blob(ptr, end, itr->size, LV2_ATOM_BODY_CONST(itr));
					break;
				}
				
				case 'h':
				{
					ptr = osc_set_int64(ptr, end, ((const LV2_Atom_Long *)itr)->body);
					break;
				}
				case 'd':
				{
					ptr = osc_set_double(ptr, end, ((const LV2_Atom_Double *)itr)->body);
					break;
				}
				case 't':
				{
					ptr = osc_set_timetag(ptr, end, ((const LV2_Atom_Long *)itr)->body);
					break;
				}
				
				case 'T':
				case 'F':
				case 'N':
				case 'I':
				{
					break;
				}
				
				case 'c':
				{
					ptr = osc_set_char(ptr, end, ((const LV2_Atom_Int *)itr)->body);
					break;
				}
				case 'm':
				{
					const uint8_t *src = LV2_ATOM_BODY_CONST(itr);
					const uint8_t dst [4] = {
						0x00, // port byte
						itr->size >= 1 ? src[0] : 0x00,
						itr->size >= 2 ? src[1] : 0x00,
						itr->size >= 3 ? src[2] : 0x00
					};
					ptr = osc_set_midi(ptr, end, dst);
					break;
				}
			}
		}

		size_t size = ptr ? ptr - buf : 0;

		if(size)
			varchunk_write_advance(handle->comm.to_worker, size);
	}
}

// rt
static void
_unroll_stamp(osc_time_t stamp, void *data)
{
	//handle_t *handle = data;

	//FIXME
}

// rt
static void
_unroll_message(const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	osc_dispatch_method(buf, size, data_methods, NULL, NULL, handle);
}

// rt
static void
_unroll_bundle(const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	uint64_t time = be64toh(*(uint64_t *)(buf + 8));

	uint64_t frames;
	if(handle->clock_sched)
		frames = handle->clock_sched->time2frames(handle->clock_sched->handle, time);
	else
		frames = 0;

	frames = frames
		? frames
		: handle->last_frame;

	// add event to list
	list_t *l = tlsf_malloc(handle->tlsf, sizeof(list_t) + size);
	if(l)
	{
		l->frames = frames;
		l->size = size;
		memcpy(l->buf, buf, size);

		handle->list = _list_insert(handle->list, l);
	}
	else
		; //lprintf(handle, handle->uris.log_trace, "message pool overflow"); FIXME
}

static const osc_unroll_inject_t inject = {
	.stamp = _unroll_stamp,
	.message = _unroll_message,
	.bundle = _unroll_bundle
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	handle->rate = rate;
	handle->s = 1.f / 32.f; //FIXME make stiffness configurable
	handle->sm1 = 1.f - handle->s;
	handle->s *= 0.5;

	for(i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = (LV2_Worker_Schedule *)features[i]->data;
		else if(!strcmp(features[i]->URI, CLOCK_SYNC__schedule))
			handle->clock_sched = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->sched)
	{
		fprintf(stderr, "%s: Host does not support worker:sched\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->tlsf = tlsf_create_with_pool(handle->mem, POOL_SIZE);
	
	chimaera_forge_init(&handle->cforge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);
	CHIMAERA_DICT_INIT(handle->dummy.dict, handle->dummy.ref);
	CHIMAERA_DICT_INIT(handle->tuio2.dict[0], handle->tuio2.ref[0]);
	CHIMAERA_DICT_INIT(handle->tuio2.dict[1], handle->tuio2.ref[1]);

	handle->uris.chimaera_comm_url = handle->map->map(handle->map->handle,
		CHIMAERA_COMM_URL_URI);
	handle->uris.chimaera_data_url = handle->map->map(handle->map->handle,
		CHIMAERA_DATA_URL_URI);

	// init comm
	handle->comm.from_worker = varchunk_new(BUF_SIZE);
	handle->comm.to_worker = varchunk_new(BUF_SIZE);
	if(!handle->comm.from_worker || !handle->comm.to_worker)
	{
		free(handle);
		return NULL;
	}

	handle->comm.driver.recv_req = _comm_recv_req;
	handle->comm.driver.recv_adv = _comm_recv_adv;
	handle->comm.driver.send_req = _comm_send_req;
	handle->comm.driver.send_adv = _comm_send_adv;
	handle->comm.driver.free = _comm_free;

	// init data
	handle->data.from_worker = varchunk_new(BUF_SIZE);
	handle->data.to_worker = varchunk_new(BUF_SIZE);
	if(!handle->data.from_worker || !handle->data.to_worker)
	{
		free(handle);
		return NULL;
	}

	handle->data.driver.recv_req = _data_recv_req;
	handle->data.driver.recv_adv = _data_recv_adv;
	handle->data.driver.send_req = _data_send_req;
	handle->data.driver.send_adv = _data_send_adv;
	handle->data.driver.free = _data_free;
	
	strcpy(handle->comm.url, "osc.udp4://chimaera.local:4444");
	strcpy(handle->data.url, "osc.udp4://:3333");

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

	switch(port)
	{
		case 0:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->reset_in = (const float *)data;
			break;
		case 2:
			handle->through_in = (const float *)data;
			break;
		case 3:
			handle->comm_state = (float *)data;
			break;
		case 4:
			handle->data_state = (float *)data;
			break;
		case 5:
			handle->control = (const LV2_Atom_Sequence *)data;
			break;
		case 6:
			handle->notify = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	uv_loop_init(&handle->loop);

	handle->comm.restored = 1;
	handle->data.restored = 1;

	handle->stamp = 0;
}

// non-rt
static int
_worker_api_flush(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	osc_stream_flush(handle->comm.stream);

	return 1;
}

// non-rt
static int
_worker_api_recv(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	//handle_t *handle = data;
	// do nothing

	return 1;
}

// non-rt
static int
_worker_api_comm_url(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	const osc_data_t *ptr = buf;

	const char *comm_url;
	ptr = osc_get_string(ptr, &comm_url);

	strcpy(handle->comm.url, comm_url);

	if(handle->comm.stream)
		osc_stream_free(handle->comm.stream);
	else
		handle->comm.reconnection_requested = 1;

	return 1;
}

// non-rt
static int
_worker_api_data_url(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	const osc_data_t *ptr = buf;

	const char *data_url;
	ptr = osc_get_string(ptr, &data_url);

	strcpy(handle->data.url, data_url);

	if(handle->data.stream)
		osc_stream_free(handle->data.stream);
	else
		handle->data.reconnection_requested = 1;

	return 1;
}

static const osc_method_t worker_api [] = {
	{"/chimaera/flush", NULL, _worker_api_flush},
	{"/chimaera/recv", NULL, _worker_api_recv},

	{"/chimaera/comm/url", "s", _worker_api_comm_url},
	{"/chimaera/data/url", "s", _worker_api_data_url},

	{NULL, NULL, NULL}
};

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	handle_t *handle = instance;

	if(handle->comm.reconnection_requested)
	{
		handle->comm.stream = osc_stream_new(&handle->loop, handle->comm.url,
			&handle->comm.driver, handle);

		handle->comm.reconnection_requested = 0;
	}
	if(handle->data.reconnection_requested)
	{
		handle->data.stream = osc_stream_new(&handle->loop, handle->data.url,
			&handle->data.driver, handle);

		handle->data.reconnection_requested = 0;
	}

	handle->respond = respond;
	handle->target = target;

	osc_dispatch_method(body, size, worker_api, NULL, NULL, handle);
	uv_run(&handle->loop, UV_RUN_NOWAIT);

	handle->respond = NULL;
	handle->target = NULL;

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	//handle_t *handle = instance;
	// do nothing

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	//handle_t *handle = instance;
	// do nothing

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

// rt
static void
_comm_url_change(handle_t *handle, const char *url)
{
	osc_data_t buf [512];
	osc_data_t *ptr = buf;
	osc_data_t *end = buf + 512;

	ptr = osc_set_path(ptr, end, "/chimaera/comm/url");
	ptr = osc_set_fmt(ptr, end, "s");
	ptr = osc_set_string(ptr, end, url);

	if(ptr)
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, ptr - buf, buf);
		(void)status;
		//TODO check status
	}
}

// rt
static void
_data_url_change(handle_t *handle, const char *url)
{
	osc_data_t buf [512];
	osc_data_t *ptr = buf;
	osc_data_t *end = buf + 512;

	ptr = osc_set_path(ptr, end, "/chimaera/data/url");
	ptr = osc_set_fmt(ptr, end, "s");
	ptr = osc_set_string(ptr, end, url);

	if(ptr)
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, ptr - buf, buf);
		(void)status;
		//TODO check status
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	uint32_t capacity;
	LV2_Atom_Forge_Frame frame;
	const osc_data_t *ptr;
	size_t size;
	handle->comm.needs_flushing = 0;

	handle->stamp += nsamples;

	// handle TUIO reset toggle
	int reset = *handle->reset_in > 0.f ? 1 : 0;
	if(reset && !handle->tuio2.reset)
	{
		chimaera_dict_clear(handle->tuio2.dict[0]);
		chimaera_dict_clear(handle->tuio2.dict[1]);
		handle->tuio2.pos = 0;

		handle->tuio2.fid = 0;
		handle->tuio2.last = 0;
		handle->tuio2.width = 0;
		handle->tuio2.height = 0;
		handle->tuio2.ignore = 0;
		handle->tuio2.n = 0;
	}
	handle->tuio2.reset = reset;

	// write outgoing comm
	LV2_ATOM_SEQUENCE_FOREACH(handle->control, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		osc_atom_event_unroll(&handle->oforge, obj, NULL, NULL, _ui_recv, handle);
	}
	if(handle->control->atom.size > sizeof(LV2_Atom_Sequence_Body))
		handle->comm.needs_flushing = 1;

	// notify worker thread to either flush or receive
	if(handle->comm.needs_flushing)
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, sizeof(flush_msg), flush_msg);
		(void)status;
		//TODO check status
	}
	else
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, sizeof(recv_msg), recv_msg);
		(void)status;
		//TODO check status
	}

	// read incoming comm and write to ui
	capacity = handle->notify->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->notify, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	while((ptr = varchunk_read_request(handle->comm.from_worker, &size)))
	{
		osc_dispatch_method(ptr, size,
			comm_methods, NULL, NULL, handle);

		varchunk_read_advance(handle->comm.from_worker);
	}
	lv2_atom_forge_pop(forge, &frame);

	// read incoming data
	capacity = handle->event_out->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	while((ptr = varchunk_read_request(handle->data.from_worker, &size)))
	{
		if(osc_check_packet(ptr, size))
			osc_unroll_packet((osc_data_t *)ptr, size, OSC_UNROLL_MODE_PARTIAL, &inject, handle);

		varchunk_read_advance(handle->data.from_worker);
	}
	
	// handle scheduled bundles
	list_t *l;
	for(l = handle->list; l; )
	{
		int64_t rel = l->frames - handle->last_frame;

		if(rel < 0) // late event
		{
			//lprintf(handle, handle->uris.log_trace, "late event: %li", rel); FIXME
			rel = 0;
		}
		else if(rel >= nsamples) // not scheduled for this period
		{
			break;
		}

		uint64_t time = be64toh(*(uint64_t *)(l->buf + 8));

		lv2_atom_forge_frame_time(forge, rel);
		//TODO check return
		osc_forge_bundle_push(&handle->oforge, forge,
			handle->data.frame[handle->data.frame_cnt++], time);
		osc_dispatch_method(l->buf, l->size, data_methods, NULL, NULL, handle);
		osc_forge_bundle_pop(&handle->oforge, forge,
			handle->data.frame[--handle->data.frame_cnt]);

		list_t *l0 = l;
		l = l->next;
		tlsf_free(handle->tlsf, l0);
	}
	handle->list = l;
	lv2_atom_forge_pop(forge, &frame);

	if(handle->comm.restored)
	{
		_comm_url_change(handle, handle->comm.url);
		handle->comm.restored = 0;
	}
	if(handle->data.restored)
	{
		_data_url_change(handle, handle->data.url);
		handle->data.restored = 0;
	}
}

static void
deactivate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	if(handle->comm.stream)
		osc_stream_free(handle->comm.stream);
	if(handle->data.stream)
		osc_stream_free(handle->data.stream);

	uv_run(&handle->loop, UV_RUN_NOWAIT);

	uv_loop_close(&handle->loop);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	tlsf_destroy(handle->tlsf);
	varchunk_free(handle->comm.from_worker);
	varchunk_free(handle->comm.to_worker);
	varchunk_free(handle->data.from_worker);
	varchunk_free(handle->data.to_worker);
	free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	else if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;

	return NULL;
}

const LV2_Descriptor comm = {
	.URI						= CHIMAERA_COMM_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
