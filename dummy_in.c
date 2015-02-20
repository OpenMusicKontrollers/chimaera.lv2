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

#include <chimaera.h>
#include <osc.h>

typedef struct _ref_t ref_t;
typedef struct _handle_t handle_t;

struct _ref_t {
	uint32_t gid;
	uint32_t pid;
};

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID osc_OscEvent;
	} uris;
	chimaera_forge_t cforge;

	chimaera_dict_t dict [CHIMAERA_DICT_SIZE];
	ref_t ref [CHIMAERA_DICT_SIZE];

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *event_out;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate, const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.osc_OscEvent = handle->map->map(handle->map->handle, LV2_OSC__OscEvent);
	chimaera_forge_init(&handle->cforge, handle->map);
	CHIMAERA_DICT_INIT(handle->dict, handle->ref);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

	switch(port)
	{
		case 0:
			handle->osc_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;
	//nothing
}

static inline void
_chim_event(handle_t *handle, osc_time_t frames, chimaera_event_t *cev)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;

	lv2_atom_forge_frame_time(forge, frames);
	chimaera_event_forge(&handle->cforge, cev);
}

static int
_on(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	chimaera_event_t cev;
	ref_t *ref;
	
	cev.state = CHIMAERA_STATE_ON;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.sid);
	ref = chimaera_dict_add(handle->dict, cev.sid);
	if(!ref)
		return 1;
	ptr = osc_get_int32(ptr, (int32_t *)&cev.gid);
	ptr = osc_get_int32(ptr, (int32_t *)&cev.pid);
	ref->gid = cev.gid;
	ref->pid = cev.pid;
	ptr = osc_get_float(ptr, &cev.x);
	ptr = osc_get_float(ptr, &cev.z);
	if(strlen(fmt) > 5)
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

	_chim_event(handle, time, &cev);

	return 1;
}

static int
_off(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	chimaera_event_t cev;
	ref_t *ref;
	
	cev.state = CHIMAERA_STATE_OFF;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.sid);
	ref = chimaera_dict_del(handle->dict, cev.sid);
	if(!ref)
		return 1;
	cev.gid = ref->gid;
	cev.pid = ref->pid;
	cev.x = 0.f;
	cev.z = 0.f;
	cev.X = 0.f;
	cev.Z = 0.f;

	_chim_event(handle, time, &cev);

	return 1;
}

static int
_set(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	chimaera_event_t cev;
	ref_t *ref;
	
	cev.state = CHIMAERA_STATE_SET;

	ptr = osc_get_int32(ptr, (int32_t *)&cev.sid);
	ref = chimaera_dict_ref(handle->dict, cev.sid);
	if(!ref)
		return 1;
	cev.gid = ref->gid;
	cev.pid = ref->pid;
	ptr = osc_get_float(ptr, &cev.x);
	ptr = osc_get_float(ptr, &cev.z);
	if(strlen(fmt) > 3)
	{
		ptr = osc_get_float(ptr, &cev.X);
		ptr = osc_get_float(ptr, &cev.Z);
	}
	else
	{
		cev.X = 0.f;
		cev.Z = 0.f;
	}

	_chim_event(handle, time, &cev);

	return 1;
}

static int
_idle(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	chimaera_event_t cev;
	
	cev.state = CHIMAERA_STATE_IDLE;
	
	cev.sid = 0;
	cev.gid = 0;
	cev.pid = 0;
	cev.x = 0.f;
	cev.z = 0.f;
	cev.X = 0.f;
	cev.Z = 0.f;

	_chim_event(handle, time, &cev);

	chimaera_dict_clear(handle->dict);

	return 1;
}

static const osc_method_t dummy [] = {
	{"/on", "iiiff", _on},
	{"/on", "iiiffff", _on},

	{"/off", "i", _off},

	{"/set", "iff", _set},
	{"/set", "iffff", _set},

	{"/idle", "", _idle},

	{NULL, NULL, NULL}
};

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_Atom_Event *ev = NULL;
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		if(ev->body.type == handle->uris.osc_OscEvent)
		{
			osc_time_t frames = ev->time.frames;
			size_t len = ev->body.size;
			const osc_data_t *buf = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Event, ev);

			if(osc_check_packet((osc_data_t *)buf, len))
				osc_dispatch_method(frames, (osc_data_t *)buf, len, (osc_method_t *)dummy, NULL, NULL, handle);
		}
	}

	lv2_atom_forge_pop(forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;
	//nothing
}

static void
cleanup(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	free(handle);
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}

const LV2_Descriptor dummy_in = {
	.URI						= CHIMAERA_DUMMY_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
