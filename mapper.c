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

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	chimaera_forge_t cforge;

	int order;
	float ex;
	float sign;

	const LV2_Atom_Sequence *event_in;
	const float *sensors;
	const float *mode;
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
			handle->sensors = (const float *)data;
			break;
		case 3:
			handle->mode = (const float *)data;
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

static inline void
_chim_event(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;

	chimaera_event_t mev;

	const float n = *handle->sensors;

	if( (cev->state == CHIMAERA_STATE_ON) || (cev->state == CHIMAERA_STATE_SET) )
	{
		if(handle->order == 0)
		{
			const float val = cev->x * n;
			const float ro = floor(val + 0.5);
			mev.x = ro / n;
		}
		else if(handle->order == 1)
		{
			mev.x = cev->x;
		}
		else // handle->order == 2, 3, 4, 5
		{
			const float val = cev->x * n;
			const float ro = floor(val + 0.5);
			float rel = val - ro;
			if(rel < 0.f)
				rel = pow(rel * handle->ex, handle->order) * handle->sign;
			else
				rel = pow(rel * handle->ex, handle->order);
			mev.x = (ro + rel) / n;
		}

		mev.state = cev->state;
		mev.sid = cev->sid;
		mev.gid = cev->gid;
		mev.pid = cev->pid;
		mev.z = cev->z;
		mev.X = cev->X;
		mev.Z = cev->Z;

		lv2_atom_forge_frame_time(forge, frames);
		chimaera_event_forge(&handle->cforge, &mev);
	}
	else
	{
		lv2_atom_forge_frame_time(forge, frames);
		chimaera_event_forge(&handle->cforge, cev);
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	int order = floor(*handle->mode);

	if(handle->order != order)
	{
		if(order > 1)
		{
			handle->ex = pow(2.f, ((float)order - 1.f) / (float)order);
			handle->sign = order % 2 == 0 ? -1.f : 1.f;
		}
		handle->order = order;
	}

	// prepare osc atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(chimaera_event_check_type(&handle->cforge, &ev->body))
		{
			int64_t frames = ev->time.frames;
			chimaera_event_t cev;

			chimaera_event_deforge(&handle->cforge, &ev->body, &cev);
			_chim_event(handle, frames, &cev);
		}
	}

	lv2_atom_forge_pop(forge, &frame);
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

const LV2_Descriptor mapper = {
	.URI						= CHIMAERA_MAPPER_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
