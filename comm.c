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

		if((ptr = varchunk_write_request(handle->comm.to_worker, obj->atom.size)))
		{
			size_t len = 0;
			//TODO serialize to OSC

			varchunk_write_advance(handle->comm.to_worker, len);
		}
	}
	if(handle->control->atom.size > sizeof(LV2_Atom_Sequence_Body))
		uv_async_send(&handle->flush);

	// read incoming comm
	capacity = handle->notify->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->notify, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	while((ptr = varchunk_read_request(handle->comm.from_worker, &size)))
	{
		/*
		lv2_atom_forge_frame_time(forge, 0);
		lv2_atom_forge_atom(forge, size, handle->uris.osc_event);
		lv2_atom_forge_raw(forge, ptr, size);
		lv2_atom_forge_pad(forge, size);
		*/
		//TODO serialize to Atom

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
