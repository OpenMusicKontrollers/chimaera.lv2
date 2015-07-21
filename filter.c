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
#include <math.h>

#include <chimaera.h>
#include <osc.h>

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	chimaera_forge_t cforge;

	const LV2_Atom_Sequence *event_in;
	const float *group_sel;
	const float *north_sel;
	const float *south_sel;
	const float *on_sel;
	const float *off_sel;
	const float *set_sel;
	const float *idle_sel;
	LV2_Atom_Sequence *event_out;
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
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->group_sel = (const float *)data;
			break;
		case 3:
			handle->north_sel = (const float *)data;
			break;
		case 4:
			handle->south_sel = (const float *)data;
			break;
		case 5:
			handle->on_sel = (const float *)data;
			break;
		case 6:
			handle->off_sel = (const float *)data;
			break;
		case 7:
			handle->set_sel = (const float *)data;
			break;
		case 8:
			handle->idle_sel = (const float *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	//handle_t *handle = (handle_t *)instance;
	//nothing
}

static inline LV2_Atom_Forge_Ref
_chim_event(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Ref ref;
		
	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = chimaera_event_forge(&handle->cforge, cev);

	return ref;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	uint8_t group_mask = floor(*handle->group_sel);
	uint32_t north = *handle->north_sel > 0.f ? 0x80 : 0;
	uint32_t south = *handle->south_sel > 0.f ? 0x100 : 0;
	uint32_t pid = north | south;
	chimaera_state_t state = 0;
	if(*handle->on_sel > 0.f)
		state |= CHIMAERA_STATE_ON;
	if(*handle->off_sel > 0.f)
		state |= CHIMAERA_STATE_OFF;
	if(*handle->set_sel > 0.f)
		state |= CHIMAERA_STATE_SET;
	if(*handle->idle_sel > 0.f)
		state |= CHIMAERA_STATE_IDLE;

	// prepare osc atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref;
	ref = lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(chimaera_event_check_type(&handle->cforge, &ev->body) && ref)
		{
			int64_t frames = ev->time.frames;
			chimaera_event_t cev;

			chimaera_event_deforge(&handle->cforge, &ev->body, &cev);

			if(cev.state == CHIMAERA_STATE_IDLE) // don't check for gid and pid
			{
				if(cev.state & state)
					ref = _chim_event(handle, frames, &cev);
			}
			else // ON, OFF, SET
			{
				if( ((1 << cev.gid) & group_mask) && (cev.pid & pid) && (cev.state & state) )
					ref = _chim_event(handle, frames, &cev);
			}
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
deactivate(LV2_Handle instance)
{
	//handle_t *handle = (handle_t *)instance;
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

const LV2_Descriptor filter = {
	.URI						= CHIMAERA_FILTER_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
