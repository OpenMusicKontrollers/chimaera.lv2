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
	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *notify;
	const float *sensors;
	const float *fps;

	LV2_URID_Map *map;
	LV2_Atom_Forge forge;

	uint32_t rate;
	uint32_t cnt;
	uint32_t thresh;
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

	lv2_atom_forge_init(&handle->forge, handle->map);	

	handle->rate = rate;

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
			handle->sensors = (const float *)data;
			break;
		case 2:
			handle->fps = (const float *)data;
			break;
		case 3:
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

	handle->cnt = 0;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	// update threshold
	handle->thresh = handle->rate / *handle->fps;

	if( (handle->cnt += nsamples) >= handle->thresh)
	{
		uint32_t size = sizeof(LV2_Atom) + handle->event_in->atom.size;
		memcpy(handle->notify, handle->event_in, size);

		handle->cnt = 0;
	}
	else
	{
		LV2_Atom_Sequence *seq = handle->notify;
		seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
		seq->atom.type = handle->forge.Sequence;
		seq->body.unit = 0;
		seq->body.pad = 0;
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

const LV2_Descriptor visualizer = {
	.URI						= CHIMAERA_VISUALIZER_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
