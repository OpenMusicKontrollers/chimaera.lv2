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

	LV2_Atom_Sequence *osc_out;

	uv_thread_t thread;
	uv_loop_t loop;
	uv_async_t quit;
	uv_async_t flush;
	osc_stream_driver_t driver;
	osc_stream_t *stream;
	varchunk_t *from_worker;
	varchunk_t *to_worker;
};

// non-rt
static void *
_recv_req(size_t size, void *data)
{
	handle_t *handle = data;

	//printf("_recv_req: %zu\n", size);
	void *ptr;
	do ptr = varchunk_write_request(handle->from_worker, size);
	while(!ptr);

	return ptr;
}

// non-rt
static void
_recv_adv(size_t written, void *data)
{
	handle_t *handle = data;

	//printf("_recv_adv: %zu\n", written);
	varchunk_write_advance(handle->from_worker, written);
}

// non-rt
static const void *
_send_req(size_t *len, void *data)
{
	handle_t *handle = data;

	//printf("_send_req\n");
	return varchunk_read_request(handle->to_worker, len);
}

// non-rt
static void
_send_adv(void *data)
{
	handle_t *handle = data;

	//printf("_send_adv\n");
	return varchunk_read_advance(handle->to_worker);
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

	handle->from_worker = varchunk_new(BUF_SIZE);
	handle->to_worker = varchunk_new(BUF_SIZE);
	if(!handle->from_worker || !handle->to_worker)
	{
		free(handle);
		return NULL;
	}

	handle->driver.recv_req = _recv_req;
	handle->driver.recv_adv = _recv_adv;
	handle->driver.send_req = _send_req;
	handle->driver.send_adv = _send_adv;

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
	osc_stream_free(handle->stream);
}

// non-rt
static void
_flush(uv_async_t *quit)
{
	handle_t *handle = quit->data;

	// flush sending queue
	osc_stream_flush(handle->stream);
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

	handle->stream = osc_stream_new(&handle->loop, "osc.udp4://:3333",
		&handle->driver, handle);

	uv_thread_create(&handle->thread, _thread, handle);
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;
		
	// prepare osc atom forge
	const uint32_t capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);

	// read varchunk buffer and add it to forge
	const void *data;
	size_t size;
	while((data = varchunk_read_request(handle->from_worker, &size)))
	{
		lv2_atom_forge_frame_time(forge, 0);
		lv2_atom_forge_atom(forge, size, handle->uris.osc_event);
		lv2_atom_forge_raw(forge, data, size);
		lv2_atom_forge_pad(forge, size);

		varchunk_read_advance(handle->from_worker);
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

	varchunk_free(handle->from_worker);
	varchunk_free(handle->to_worker);
	free(handle);
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}

const LV2_Descriptor injector = {
	.URI						= CHIMAERA_INJECTOR_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
