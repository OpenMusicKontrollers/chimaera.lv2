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
#include <tlsf.h>

#if defined(_WIN32)
#	include <avrt.h>
#endif

#define POOL_SIZE 0x20000 // 128KB
#define BUF_SIZE 0x10000

typedef enum _plugstate_t plugstate_t;
typedef struct _list_t list_t;
typedef struct _handle_t handle_t;

enum _plugstate_t {
	STATE_IDLE					= 0,
	STATE_READY					= 1,
	STATE_TIMEDOUT			= 2,
	STATE_ERRORED				= 3,
	STATE_CONNECTED			= 4
};

struct _list_t {
	list_t *next;
	int64_t frames;

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

		LV2_URID log_note;
		LV2_URID log_error;
		LV2_URID log_trace;
	} uris;

	LV2_Log_Log *log;

	LV2_Atom_Sequence *osc_out;
	float *comm_state;
	float *data_state;
	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;

	uv_loop_t loop;

	LV2_Worker_Schedule *sched;
	LV2_Worker_Respond_Function respond;
	LV2_Worker_Respond_Handle target;

	osc_schedule_t *osc_sched;
	list_t *list;
	uint8_t mem [POOL_SIZE];
	tlsf_t tlsf;

	LV2_Atom_Forge_Ref ref;

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

static const char flush_msg [] = "/chimaera/flush\0,\0\0\0";
static const char recv_msg [] = "/chimaera/recv\0\0,\0\0\0";

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

static void
lprintf(handle_t *handle, LV2_URID type, const char *fmt, ...)
{
	if(handle->log)
	{
		va_list args;
		va_start(args, fmt);
		handle->log->vprintf(handle->log->handle, type, fmt, args);
		va_end(args);
	}
	else if(type != handle->uris.log_trace)
	{
		const char *type_str = NULL;
		if(type == handle->uris.log_note)
			type_str = "Note";
		else if(type == handle->uris.log_error)
			type_str = "Error";

		fprintf(stderr, "[%s]", type_str);
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		fputc('\n', stderr);
	}
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
	const osc_data_t *buf, size_t size, int frame_cnt)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Frame frame [2];

	const osc_data_t *ptr = buf;
	LV2_Atom_Forge_Ref ref = handle->ref;

	if(!frame_cnt && ref)
		ref = lv2_atom_forge_frame_time(forge, 0);
	if(ref)
		ref = osc_forge_message_push(&handle->oforge, forge, frame, path, fmt);

	for(const char *type = fmt; *type && ref; type++)
		switch(*type)
		{
			case 'i':
			{
				int32_t i;
				if((ptr = osc_get_int32(ptr, &i)))
					ref = osc_forge_int32(&handle->oforge, forge, i);
				break;
			}
			case 'f':
			{
				float f;
				if((ptr = osc_get_float(ptr, &f)))
					ref = osc_forge_float(&handle->oforge, forge, f);
				break;
			}
			case 's':
			case 'S':
			{
				const char *s;
				if((ptr = osc_get_string(ptr, &s)))
					ref = osc_forge_string(&handle->oforge, forge, s);
				break;
			}
			case 'b':
			{
				osc_blob_t b;
				if((ptr = osc_get_blob(ptr, &b)))
					ref = osc_forge_blob(&handle->oforge, forge, b.size, b.payload);
				break;
			}

			case 'h':
			{
				int64_t h;
				if((ptr = osc_get_int64(ptr, &h)))
					ref = osc_forge_int64(&handle->oforge, forge, h);
				break;
			}
			case 'd':
			{
				double d;
				if((ptr = osc_get_double(ptr, &d)))
					ref = osc_forge_double(&handle->oforge, forge, d);
				break;
			}
			case 't':
			{
				uint64_t t;
				if((ptr = osc_get_timetag(ptr, &t)))
					ref = osc_forge_timestamp(&handle->oforge, forge, t);
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
					ref = osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				const uint8_t *m;
				if((ptr = osc_get_midi(ptr, &m)))
					ref = osc_forge_midi(&handle->oforge, forge, 3, m + 1);
				break;
			}
		}

	if(ref)
		osc_forge_message_pop(&handle->oforge, forge, frame);

	handle->ref = ref;
}

// rt
static int
_comm_method(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	_osc_atom_serialize(handle, path, fmt, buf, size, 0);

	return 1;
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
_data_method(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	_osc_atom_serialize(handle, path, fmt, buf, size, handle->data.frame_cnt);

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

	{NULL, NULL, _data_method},

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

	int64_t frames;
	if(handle->osc_sched)
		frames = handle->osc_sched->osc2frames(handle->osc_sched->handle, time);
	else
		frames = 0;

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
		lprintf(handle, handle->uris.log_trace, "message pool overflow");
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

	for(i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = (LV2_Worker_Schedule *)features[i]->data;
		else if(!strcmp(features[i]->URI, OSC__schedule))
			handle->osc_sched = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
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

	handle->uris.chimaera_comm_url = handle->map->map(handle->map->handle,
		CHIMAERA_COMM_URL_URI);
	handle->uris.chimaera_data_url = handle->map->map(handle->map->handle,
		CHIMAERA_DATA_URL_URI);

	handle->uris.log_note = handle->map->map(handle->map->handle,
		LV2_LOG__Note);
	handle->uris.log_error = handle->map->map(handle->map->handle,
		LV2_LOG__Error);
	handle->uris.log_trace = handle->map->map(handle->map->handle,
		LV2_LOG__Trace);

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
			handle->osc_out = (LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->comm_state = (float *)data;
			break;
		case 2:
			handle->data_state = (float *)data;
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

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	uv_loop_init(&handle->loop);

	handle->comm.restored = 1;
	handle->data.restored = 1;
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
	if(ptr)
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
	if(ptr)
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
	LV2_Atom_Forge_Frame frame;
	uint32_t capacity;
	handle->comm.needs_flushing = 0;

	const osc_data_t *ptr;
	size_t size;

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
		osc_dispatch_method(ptr, size, comm_methods, NULL, NULL, handle);

		varchunk_read_advance(handle->comm.from_worker);
	}
	lv2_atom_forge_pop(forge, &frame);

	// reschedule scheduled bundles
	list_t *l;
	for(l = handle->list; l; l = l->next)
	{
		uint64_t time = be64toh(*(uint64_t *)(l->buf + 8));

		int64_t frames = handle->osc_sched->osc2frames(handle->osc_sched->handle, time);
		l->frames = frames;
	}

	// read incoming data
	capacity = handle->osc_out->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	while((ptr = varchunk_read_request(handle->data.from_worker, &size)))
	{
		if(osc_check_packet(ptr, size))
			osc_unroll_packet((osc_data_t *)ptr, size, OSC_UNROLL_MODE_PARTIAL, &inject, handle);

		varchunk_read_advance(handle->data.from_worker);
	}

	// handle scheduled bundles
	for(l = handle->list; l; )
	{
		uint64_t time = be64toh(*(uint64_t *)(l->buf + 8));
		//lprintf(handle, handle->uris.log_trace, "frames: %lu, %lu", time, l->frames);

		if(l->frames < 0) // late event
		{
			lprintf(handle, handle->uris.log_trace, "late event: %li samples", l->frames);
			l->frames = 0; // dispatch as early as possible
		}
		else if(l->frames >= nsamples) // not scheduled for this period
		{
			l = l->next;
			continue;
		}

		lv2_atom_forge_frame_time(forge, l->frames);
		handle->ref = osc_forge_bundle_push(&handle->oforge, forge,
			handle->data.frame[handle->data.frame_cnt++], time);
		if(handle->ref)
			osc_dispatch_method(l->buf, l->size, data_methods, NULL, NULL, handle);
		if(handle->ref)
			osc_forge_bundle_pop(&handle->oforge, forge,
				handle->data.frame[--handle->data.frame_cnt]);

		list_t *l0 = l;
		l = l->next;
		handle->list = l;
		tlsf_free(handle->tlsf, l0);
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->osc_out);

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
