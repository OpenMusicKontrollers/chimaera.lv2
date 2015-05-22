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
#include <varchunk.h>

#if defined(_WIN32)
#	include <avrt.h>
#endif

#define BUF_SIZE 0x10000

typedef struct _dummy_ref_t dummy_ref_t;
typedef struct _tuio2_ref_t tuio2_ref_t;
typedef struct _handle_t handle_t;

struct _dummy_ref_t {
	uint32_t gid;
	uint32_t pid;
};

struct _tuio2_ref_t {
	uint32_t gid;
	uint32_t tuid;
	float x;
	float z;
	float a;
	float X;
	float Z;
	float A;
	float m;
	float R;
};

struct _handle_t {
	LV2_URID_Map *map;
	chimaera_forge_t cforge;
	osc_forge_t oforge;

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

	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;
	const float *reset_in;
	const float *through_in;
	LV2_Atom_Sequence *event_out;

	uv_thread_t thread;
	uv_loop_t loop;
	uv_async_t quit;
	uv_async_t flush;

	struct {
		osc_stream_driver_t driver;
		osc_stream_t *stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
	} comm;
	
	struct {
		osc_stream_driver_t driver;
		osc_stream_t *stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
	} data;
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

static int
_comm_resolve(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_comm_timeout(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_comm_error(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_comm_connect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_comm_disconnect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

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
			{
				osc_forge_true(&handle->oforge, forge);
				break;
			}
			case 'F':
			{
				osc_forge_false(&handle->oforge, forge);
				break;
			}
			case 'N':
			{
				osc_forge_nil(&handle->oforge, forge);
				break;
			}
			case 'I':
			{
				osc_forge_bang(&handle->oforge, forge);
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
					osc_forge_midi(&handle->oforge, forge, m);
				break;
			}
		}

	osc_forge_message_pop(&handle->oforge, forge, frame);
}

static int
_comm_method(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	_osc_atom_serialize(handle, path, fmt, buf, size);	

	return 1;
}

static inline void
_chim_event(handle_t *handle, osc_time_t frames, chimaera_event_t *cev)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;

	lv2_atom_forge_frame_time(forge, frames);
	chimaera_event_forge(&handle->cforge, cev);
}

static int
_data_resolve(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_data_timeout(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_data_error(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_data_connect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_data_disconnect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	//TODO

	return 1;
}

static int
_data_through(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	if(*handle->through_in != 0.f)
		_osc_atom_serialize(handle, path, fmt, buf, size);	

	return 0;
}

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

static int
_tuio2_tok(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

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
	ptr = osc_get_float(ptr, &ref->x);
	ptr = osc_get_float(ptr, &ref->z);
	ptr = osc_get_float(ptr, &ref->a);

	if(has_derivatives)
	{
		ptr = osc_get_float(ptr, &ref->X);
		ptr = osc_get_float(ptr, &ref->Z);
		ptr = osc_get_float(ptr, &ref->A);
		ptr = osc_get_float(ptr, &ref->m);
		ptr = osc_get_float(ptr, &ref->R);
	}
	else
	{
		ref->X = 0.f;
		ref->Z = 0.f;
		ref->A = 0.f;
		ref->m = 0.f;
		ref->R = 0.f;
		//TODO calculate !
	}

	return 1;
}

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
			cev.x = src->x;
			cev.z = src->z;
			cev.X = src->X;
			cev.Z = src->Z;

			_chim_event(handle, timestamp, &cev);
		}
	}

	// iterate over this step's blobs
	CHIMAERA_DICT_FOREACH(handle->tuio2.dict[handle->tuio2.pos], sid, dst)
	{
		cev.sid = sid;
		cev.gid = dst->gid;
		cev.pid = dst->tuid & 0xffff;
		cev.x = dst->x;
		cev.z = dst->z;
		cev.X = dst->X;
		cev.Z = dst->Z;

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

static int
_dummy_on(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

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

	ptr = osc_get_float(ptr, &cev.x);
	ptr = osc_get_float(ptr, &cev.z);

	if(has_derivatives)
	{
		ptr = osc_get_float(ptr, &cev.X);
		ptr = osc_get_float(ptr, &cev.Z);
	}
	else
	{
		cev.X = 0.f;
		cev.Z = 0.f;
		//TODO calculate !
	}

	_chim_event(handle, timestamp, &cev);

	return 1;
}

static int
_dummy_off(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	int is_redundant = strlen(fmt) == 3;

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
	cev.x = 0.f;
	cev.z = 0.f;
	cev.X = 0.f;
	cev.Z = 0.f;

	_chim_event(handle, timestamp, &cev);

	return 1;
}

static int
_dummy_set(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

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

	ptr = osc_get_float(ptr, &cev.x);
	ptr = osc_get_float(ptr, &cev.z);

	if(has_derivatives)
	{
		ptr = osc_get_float(ptr, &cev.X);
		ptr = osc_get_float(ptr, &cev.Z);
	}
	else
	{
		cev.X = 0.f;
		cev.Z = 0.f;
	}

	_chim_event(handle, timestamp, &cev);

	return 1;
}

static int
_dummy_idle(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	const osc_data_t *ptr = buf;
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
_ui_recv(uint64_t timestamp, const char *path, const char *fmt,
	const LV2_Atom_Tuple *args, void *data)
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
					ptr = osc_set_int32(ptr, end, ((const LV2_Atom_Int *)itr)->body);
					break;
				case 'f':
					ptr = osc_set_float(ptr, end, ((const LV2_Atom_Float *)itr)->body);
					break;
				case 's':
				case 'S':
					ptr = osc_set_string(ptr, end, LV2_ATOM_BODY_CONST(itr));
					break;
				case 'b':
					ptr = osc_set_blob(ptr, end, itr->size, LV2_ATOM_BODY_CONST(itr));
					break;
				
				case 'h':
					ptr = osc_set_int64(ptr, end, ((const LV2_Atom_Long *)itr)->body);
					break;
				case 'd':
					ptr = osc_set_double(ptr, end, ((const LV2_Atom_Double *)itr)->body);
					break;
				case 't':
					ptr = osc_set_timetag(ptr, end, ((const LV2_Atom_Long *)itr)->body);
					break;
				
				case 'T':
				case 'F':
				case 'N':
				case 'I':
					break;
				
				case 'c':
					ptr = osc_set_char(ptr, end, ((const LV2_Atom_Int *)itr)->body);
					break;
				case 'm':
					ptr = osc_set_midi(ptr, end, LV2_ATOM_BODY_CONST(itr));
					break;
			}
		}

		size_t size = ptr ? ptr - buf : 0;

		if(size)
			varchunk_write_advance(handle->comm.to_worker, size);
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	chimaera_forge_init(&handle->cforge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);
	CHIMAERA_DICT_INIT(handle->dummy.dict, handle->dummy.ref);
	CHIMAERA_DICT_INIT(handle->tuio2.dict[0], handle->tuio2.ref[0]);
	CHIMAERA_DICT_INIT(handle->tuio2.dict[1], handle->tuio2.ref[1]);

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
			handle->control = (const LV2_Atom_Sequence *)data;
			break;
		case 4:
			handle->notify = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

// non-rt
static void
_quit(uv_async_t *quit)
{
	handle_t *handle = quit->data;

	uv_close((uv_handle_t *)&handle->quit, NULL);
	uv_close((uv_handle_t *)&handle->flush, NULL);
	osc_stream_free(handle->comm.stream);
	osc_stream_free(handle->data.stream);
}

// non-rt
static void
_flush(uv_async_t *quit)
{
	handle_t *handle = quit->data;

	// flush sending queue
	osc_stream_flush(handle->comm.stream);
	osc_stream_flush(handle->data.stream);
}

// non-rt
static void
_thread(void *data)
{
	handle_t *handle = data;

	const int priority = 50;

#if defined(_WIN32)
	int mcss_sched_priority;
	mcss_sched_priority = priority > 50 // TODO when to use CRITICAL?
		? AVRT_PRIORITY_CRITICAL
		: (priority > 0
			? AVRT_PRIORITY_HIGH
			: AVRT_PRIORITY_NORMAL);

	// Multimedia Class Scheduler Service
	DWORD dummy = 0;
	HANDLE task = AvSetMmThreadCharacteristics("Pro Audio", &dummy);
	if(!task)
		fprintf(stderr, "AvSetMmThreadCharacteristics error: %d\n", GetLastError());
	else if(!AvSetMmThreadPriority(task, mcss_sched_priority))
		fprintf(stderr, "AvSetMmThreadPriority error: %d\n", GetLastError());

#else
	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = priority;
	
	if(pthread_setschedparam(pthread_self(), SCHED_RR, &schedp))
		fprintf(stderr, "pthread_setschedparam error\n");
#endif

	// main loop
	uv_run(&handle->loop, UV_RUN_DEFAULT);
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	uv_loop_init(&handle->loop);

	handle->quit.data = handle;
	uv_async_init(&handle->loop, &handle->quit, _quit);
	
	handle->flush.data = handle;
	uv_async_init(&handle->loop, &handle->flush, _flush);

	handle->comm.stream = osc_stream_new(&handle->loop, "osc.udp4://chimaera.local:4444",
		&handle->comm.driver, handle);

	handle->data.stream = osc_stream_new(&handle->loop, "osc.udp4://:3333",
		&handle->data.driver, handle);

	uv_thread_create(&handle->thread, _thread, handle);
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
		osc_atom_event_unroll(&handle->oforge, obj, _ui_recv, handle);
	}
	if(handle->control->atom.size > sizeof(LV2_Atom_Sequence_Body))
		uv_async_send(&handle->flush);

	// read incoming comm and write to ui
	capacity = handle->notify->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->notify, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	while((ptr = varchunk_read_request(handle->comm.from_worker, &size)))
	{
		osc_dispatch_method(OSC_IMMEDIATE, ptr, size,
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
		osc_dispatch_method(OSC_IMMEDIATE, ptr, size,
			data_methods, NULL, NULL, handle);

		varchunk_read_advance(handle->data.from_worker);
	}
	lv2_atom_forge_pop(forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	uv_async_send(&handle->quit);
	uv_thread_join(&handle->thread);
	uv_loop_close(&handle->loop);
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	varchunk_free(handle->comm.from_worker);
	varchunk_free(handle->comm.to_worker);
	varchunk_free(handle->data.from_worker);
	varchunk_free(handle->data.to_worker);
	free(handle);
}

static const void*
extension_data(const char* uri)
{
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
