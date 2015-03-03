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

#define BUF_SIZE 2048

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID osc_OscEvent;
	} uris;
	chimaera_forge_t cforge;

	osc_data_t buf [BUF_SIZE];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *osc_out;
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

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

	switch(port)
	{
		case 0:
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->osc_out = (LV2_Atom_Sequence *)data;
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
_osc_event(handle_t *handle, int64_t frames, osc_data_t *o, size_t len)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
		
	LV2_Atom osc_atom;
	osc_atom.type = handle->uris.osc_OscEvent;
	osc_atom.size = len;
		
	lv2_atom_forge_frame_time(forge, frames);
	lv2_atom_forge_raw(forge, &osc_atom, sizeof(LV2_Atom));
	lv2_atom_forge_raw(forge, o, len);
	lv2_atom_forge_pad(forge, len);
}

static void
_osc_on(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	osc_data_t *buf = handle->buf;
	osc_data_t *ptr = buf;
	osc_data_t *end = buf + BUF_SIZE;

	ptr = osc_set_vararg(ptr, end, "/s_new", "siiiiisisi",
		"base", cev->sid, 0, cev->gid,
		4, cev->pid,
		"out", cev->gid,
		"gate", 1);

	if(ptr)
	{
		size_t len = ptr - buf;
		if(len > 0)
			_osc_event(handle, frames, buf, len);
	}
}

static void
_osc_off(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	osc_data_t *buf = handle->buf;
	osc_data_t *ptr = buf;
	osc_data_t *end = buf + BUF_SIZE;

	ptr = osc_set_vararg(ptr, end, "/n_set", "isi",
		cev->sid,
		"gate", 0);

	if(ptr)
	{
		size_t len = ptr - buf;
		if(len > 0)
			_osc_event(handle, frames, buf, len);
	}
}

static void
_osc_set(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	osc_data_t *buf = handle->buf;
	osc_data_t *ptr = buf;
	osc_data_t *end = buf + BUF_SIZE;

	ptr = osc_set_vararg(ptr, end, "/n_setn", "iiiffff",
		cev->sid, 0, 4,
		cev->x, cev->z, cev->X, cev->Z);

	if(ptr)
	{
		size_t len = ptr - buf;
		if(len > 0)
			_osc_event(handle, frames, buf, len);
	}
}

static void
_osc_idle(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	// do nothing
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
	
	LV2_Atom_Event *ev = NULL;
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(chimaera_event_check_type(&handle->cforge, &ev->body))
		{
			int64_t frames = ev->time.frames;
			size_t len = ev->body.size;
			chimaera_event_t cev;

			chimaera_event_deforge(&handle->cforge, &ev->body, &cev);

			switch(cev.state)
			{
				case CHIMAERA_STATE_ON:
					_osc_on(handle, frames, &cev);
					// fall-through
				case CHIMAERA_STATE_SET:
					_osc_set(handle, frames, &cev);
					break;
				case CHIMAERA_STATE_OFF:
					_osc_off(handle, frames, &cev);
					break;
				case CHIMAERA_STATE_IDLE:
					_osc_idle(handle, frames, &cev);
					break;
				case CHIMAERA_STATE_NONE:
					break;
			}
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

const LV2_Descriptor osc_out = {
	.URI						= CHIMAERA_OSC_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
