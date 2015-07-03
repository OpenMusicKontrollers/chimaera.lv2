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
#include <lv2_osc.h>

#define BUF_SIZE 2048

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	chimaera_forge_t cforge;
	osc_forge_t oforge;
	LV2_Atom_Forge forge;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *osc_out;
	const float *out_offset;
	const float *gid_offset;
	const float *sid_offset;
	const float *sid_wrap;
	const float *arg_offset;
	const float *allocate;
	const float *gate;
	const float *group;
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

	osc_forge_init(&handle->oforge, handle->map);
	chimaera_forge_init(&handle->cforge, handle->map);
	lv2_atom_forge_init(&handle->forge, handle->map);

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

		case 2:
			handle->out_offset = (const float *)data;
			break;
		case 3:
			handle->gid_offset = (const float *)data;
			break;
		case 4:
			handle->sid_offset = (const float *)data;
			break;
		case 5:
			handle->sid_wrap = (const float *)data;
			break;
		case 6:
			handle->arg_offset = (const float *)data;
			break;

		case 7:
			handle->allocate = (const float *)data;
			break;
		case 8:
			handle->gate = (const float *)data;
			break;
		case 9:
			handle->group = (const float *)data;
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

static void
_osc_on(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	const int32_t out = (int32_t)floor(*handle->out_offset) + cev->gid;
	const int32_t gid = (int32_t)floor(*handle->gid_offset) + cev->gid;
	const int32_t sid = (int32_t)floor(*handle->sid_offset)
		+ cev->sid % (int32_t)floor(*handle->sid_wrap);
	const int32_t arg_offset = (int32_t)floor(*handle->arg_offset);
	const int32_t arg_num = 4;
	const int allocate = *handle->allocate != 0.f;
	const int gate = *handle->allocate != 0.f;
	const int group = *handle->allocate != 0.f;
	const int32_t id = group ? gid : sid;

	if(allocate)
	{
		lv2_atom_forge_frame_time(forge, frames);
		osc_forge_message_vararg(&handle->oforge, forge,
			"/s_new", "siiiiisisi",
			"base", id, 0, gid,
			arg_offset + 4, cev->pid,
			"out", gid,
			"gate", 1);
	}

	if(gate)
	{
		lv2_atom_forge_frame_time(forge, frames);
		osc_forge_message_vararg(&handle->oforge, forge,
			"/n_setn", "iiiffff",
			id, arg_offset, arg_num,
			cev->x, cev->z, cev->X, cev->Z);
	}
}

static void
_osc_off(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	const int32_t sid = (int32_t)floor(*handle->sid_offset) + cev->sid;
	const int32_t gid = (int32_t)floor(*handle->gid_offset) + cev->gid;
	const int gate = *handle->allocate != 0.f;
	const int group = *handle->allocate != 0.f;
	const int32_t id = group ? sid : gid;

	if(gate)
	{
		lv2_atom_forge_frame_time(forge, frames);
		osc_forge_message_vararg(&handle->oforge, forge,
			"/n_set", "isi",
			id,
			"gate", 0);
	}
}

static void
_osc_set(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	const int32_t sid = (int32_t)floor(*handle->sid_offset) + cev->sid;
	const int32_t gid = (int32_t)floor(*handle->gid_offset) + cev->gid;
	const int32_t arg_offset = (int32_t)floor(*handle->arg_offset);
	const int32_t arg_num = 4;
	const int group = *handle->allocate != 0.f;
	const int32_t id = group ? sid : gid;

	lv2_atom_forge_frame_time(forge, frames);
	osc_forge_message_vararg(&handle->oforge, forge,
		"/n_setn", "iiiffff",
		id, arg_offset, arg_num,
		cev->x, cev->z, cev->X, cev->Z);
}

static void
_osc_idle(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	// do nothing
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);

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
					_osc_on(handle, forge, frames, &cev);
					break;
				case CHIMAERA_STATE_SET:
					_osc_set(handle, forge, frames, &cev);
					break;
				case CHIMAERA_STATE_OFF:
					_osc_off(handle, forge, frames, &cev);
					break;
				case CHIMAERA_STATE_IDLE:
					_osc_idle(handle, forge, frames, &cev);
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
