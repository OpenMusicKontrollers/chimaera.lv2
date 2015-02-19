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

typedef struct _handle_t handle_t;

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID chim_Event;
	} uris;

	const LV2_Atom_Sequence *event_in;
	float *gate;
	float *sid;
	float *north;
	float *south;
	float *x;
	float *z;
	float *X;
	float *Z;
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
	
	handle->uris.chim_Event = handle->map->map(handle->map->handle, CHIMAERA_EVENT_URI);

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
			handle->gate = (float *)data;
			break;
		case 2:
			handle->sid = (float *)data;
			break;
		case 3:
			handle->north = (float *)data;
			break;
		case 4:
			handle->south = (float *)data;
			break;
		case 5:
			handle->x = (float *)data;
			break;
		case 6:
			handle->z = (float *)data;
			break;
		case 7:
			handle->X = (float *)data;
			break;
		case 8:
			handle->Z = (float *)data;
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
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	LV2_Atom_Event *ev = NULL;
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(ev->body.type == handle->uris.chim_Event)
		{
			int64_t frames = ev->time.frames;
			size_t len = ev->body.size;
			const chimaera_event_t *cev = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Event, ev);

			switch(cev->state)
			{
				case CHIMAERA_STATE_ON:
					*handle->gate = 1.f;
					// fall-through
				case CHIMAERA_STATE_SET:
					*handle->sid = cev->sid;
					*handle->north = cev->pid & 0x80 ? 1.f : 0.f;
					*handle->south = cev->pid & 0x100 ? 1.f : 0.f;
					*handle->x = cev->x;
					*handle->z = cev->z;
					*handle->X = cev->X;
					*handle->Z = cev->Z;
					break;

				case CHIMAERA_STATE_OFF:
					// fall-through
				case CHIMAERA_STATE_IDLE:
					*handle->gate = 0.f;
					*handle->sid = 0.f;
					*handle->north = 0.f;
					*handle->south = 0.f;
					*handle->x = 0.f;
					*handle->z = 0.f;
					*handle->X = 0.f;
					*handle->Z = 0.f;
					break;
				case CHIMAERA_STATE_NONE:
					break;
			}
		}
	}
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

const LV2_Descriptor control_out = {
	.URI						= CHIMAERA_CONTROL_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
