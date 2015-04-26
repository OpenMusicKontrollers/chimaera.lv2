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

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID osc_event;
	} uris;
	chimaera_forge_t cforge;
	osc_forge_t oforge;

	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;
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

		LV2_Atom_Forge_Frame obj_frame;
		LV2_Atom_Forge_Frame tup_frame;

		osc_data_t *buf;
		size_t size;
	} comm;
	
	struct {
		osc_stream_driver_t driver;
		osc_stream_t *stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
		LV2_Atom_Forge_Frame obj_frame;
		LV2_Atom_Forge_Frame tup_frame;
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

static void
_comm_bundle_in(osc_time_t timestamp, void *data)
{
	handle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Frame *obj_frame = &handle->comm.obj_frame;
	LV2_Atom_Forge_Frame *tup_frame = &handle->comm.tup_frame;

	osc_forge_bundle_push(&handle->oforge, forge, obj_frame, tup_frame, timestamp);
}

static void
_comm_bundle_out(osc_time_t timestamp, void *data)
{
	handle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Frame *obj_frame = &handle->comm.obj_frame;
	LV2_Atom_Forge_Frame *tup_frame = &handle->comm.tup_frame;
	
	osc_forge_bundle_pop(&handle->oforge, forge, obj_frame, tup_frame);
}

static void
_data_bundle_in(osc_time_t timestamp, void *data)
{
	handle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Frame *obj_frame = &handle->data.obj_frame;
	LV2_Atom_Forge_Frame *tup_frame = &handle->data.tup_frame;

	osc_forge_bundle_push(&handle->oforge, forge, obj_frame, tup_frame, timestamp);
}

static void
_data_bundle_out(osc_time_t timestamp, void *data)
{
	handle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Frame *obj_frame = &handle->data.obj_frame;
	LV2_Atom_Forge_Frame *tup_frame = &handle->data.tup_frame;
	
	osc_forge_bundle_pop(&handle->oforge, forge, obj_frame, tup_frame);
}

static int
_comm_method(osc_time_t timestamp, const char *path, const char *fmt,
	osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Frame tup_frame;

	osc_data_t *ptr = buf;

	osc_forge_message_push(&handle->oforge, forge, &obj_frame, &tup_frame,
		path, fmt);

	for(const char *type = fmt+1; *type; type++)
		switch(*type)
		{
			case 'i':
			{
				int32_t i;
				ptr = osc_get_int32(ptr, &i);
				osc_forge_int32(&handle->oforge, forge, i);
				break;
			}
			case 'f':
			{
				float f;
				ptr = osc_get_float(ptr, &f);
				osc_forge_float(&handle->oforge, forge, f);
				break;
			}
			case 's':
			case 'S':
			{
				const char *s;
				ptr = osc_get_string(ptr, &s);
				osc_forge_string(&handle->oforge, forge, s);
				break;
			}
			case 'b':
			{
				osc_blob_t b;
				ptr = osc_get_blob(ptr, &b);
				osc_forge_blob(&handle->oforge, forge, b.size, b.payload);
				break;
			}

			case 'h':
			{
				int64_t h;
				ptr = osc_get_int64(ptr, &h);
				osc_forge_int64(&handle->oforge, forge, h);
				break;
			}
			case 'd':
			{
				double d;
				ptr = osc_get_double(ptr, &d);
				osc_forge_double(&handle->oforge, forge, d);
				break;
			}
			case 't':
			{
				uint64_t t;
				ptr = osc_get_timetag(ptr, &t);
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
				ptr = osc_get_char(ptr, &c);
				osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				uint8_t *m;
				ptr = osc_get_midi(ptr, &m);
				osc_forge_midi(&handle->oforge, forge, m);
				break;
			}
		}

	osc_forge_message_pop(&handle->oforge, forge, &obj_frame, &tup_frame);

	return 1;
}

static const osc_method_t _comm_methods [] = {
	{NULL, NULL, _comm_method},
	{NULL, NULL, NULL}
};

static const osc_method_t _data_methods [] = {
	/*
	{"/tuio2/frm", "itis", _tuio2_frm},
	{"/tuio2/tok", "iiiff", _tuio2_tok},
	{"/tuio2/tok", "iiifffff", _tuio2_tok},
	{"/tuio2/alv", NULL, _tuio2_alv},
	{"/on", "iiiff", _on},
	{"/on", "iiiffff", _on},
	{"/off", "i", _off},
	{"/off", "iii", _off},
	{"/set", "iff", _set},
	{"/set", "iffff", _set},
	{"/set", "iiiff", _set},
	{"/set", "iiiffff", _set},
	{"/idle", "", _idle},
	*/
	{NULL, NULL, NULL}
};

// rt-thread
static void
_comm_message(uint64_t timestamp, const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	handle_t *handle = data;

	osc_data_t *buf;
	if((buf = varchunk_write_request(handle->comm.to_worker, body->atom.size)))
	{
		osc_data_t *ptr = buf;
		osc_data_t *end = buf + body->atom.size;

		ptr = osc_set_path(ptr, end, path);
		ptr = osc_set_fmt(ptr, end, fmt);

		const LV2_Atom *itr = lv2_atom_tuple_begin(body);
		for(const char *type = fmt;
			*type && !lv2_atom_tuple_is_end(LV2_ATOM_BODY(body), body->atom.size, itr);
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
					ptr = osc_set_blob(ptr, end, itr->size, LV2_ATOM_BODY(itr));
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
					ptr = osc_set_midi(ptr, end, LV2_ATOM_BODY(itr));
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
	
	handle->uris.osc_event = handle->map->map(handle->map->handle, LV2_OSC__OscEvent);
	chimaera_forge_init(&handle->cforge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);

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
			handle->control = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->notify = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->event_out = (LV2_Atom_Sequence *)data;
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
	const void *ptr;
	size_t size;

	// write outgoing comm
	LV2_ATOM_SEQUENCE_FOREACH(handle->control, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		osc_atom_unpack(&handle->oforge, obj, _comm_message, handle);
	}
	if(handle->control->atom.size > sizeof(LV2_Atom_Sequence_Body))
		uv_async_send(&handle->flush);

	// read incoming comm
	capacity = handle->notify->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->notify, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	while((ptr = varchunk_read_request(handle->comm.from_worker, &size)))
	{
		lv2_atom_forge_frame_time(forge, 0); //TODO
		osc_dispatch_method(0, (osc_data_t *)ptr, size, (osc_method_t *)_comm_methods,
			_comm_bundle_in, _comm_bundle_out, handle);

		varchunk_read_advance(handle->comm.from_worker);
	}
	lv2_atom_forge_pop(forge, &frame);

	// read incoming data
	capacity = handle->event_out->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	while((ptr = varchunk_read_request(handle->data.from_worker, &size)))
	{
		lv2_atom_forge_frame_time(forge, 0);
		lv2_atom_forge_atom(forge, size, handle->uris.osc_event);
		lv2_atom_forge_raw(forge, ptr, size);
		lv2_atom_forge_pad(forge, size);

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
