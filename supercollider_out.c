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
#include <bsd/string.h>

#include <chimaera.h>
#include <osc.h>
#include <lv2_osc.h>
#include <props.h>

#define SYNTH_NAMES 8
#define STRING_SIZE 256
#define MAX_NPROPS (SYNTH_NAMES)

typedef struct _handle_t handle_t;

struct _handle_t {
	char synth_name [STRING_SIZE][SYNTH_NAMES];

	LV2_URID_Map *map;
	chimaera_forge_t cforge;
	osc_forge_t oforge;
	LV2_Atom_Forge forge;

	PROPS_T(props, MAX_NPROPS);

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

	int i_allocate;
	int i_gate;
	int i_group;
};

static const props_def_t synth_name_def [SYNTH_NAMES] = {
	[0] = {
		.label = "Synth Name 0",
		.property = CHIMAERA_URI"#synth_name_0",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[1] = {
		.label = "Synth Name 1",
		.property = CHIMAERA_URI"#synth_name_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[2] = {
		.label = "Synth Name 2",
		.property = CHIMAERA_URI"#synth_name_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[3] = {
		.label = "Synth Name 3",
		.property = CHIMAERA_URI"#synth_name_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[4] = {
		.label = "Synth Name 4",
		.property = CHIMAERA_URI"#synth_name_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[5] = {
		.label = "Synth Name 5",
		.property = CHIMAERA_URI"#synth_name_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[6] = {
		.label = "Synth Name 6",
		.property = CHIMAERA_URI"#synth_name_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
	[7] = {
		.label = "Synth Name 7",
		.property = CHIMAERA_URI"#synth_name_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = STRING_SIZE // strlen
	},
};

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_save(&handle->props, &handle->forge, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	handle_t *handle = instance;

	return props_restore(&handle->props, &handle->forge, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(int i=0; features[i]; i++)
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

	if(!props_init(&handle->props, SYNTH_NAMES, descriptor->URI, handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	LV2_URID urid = 1;
	for(unsigned i=0; (i<SYNTH_NAMES) && urid; i++)
	{
		sprintf(handle->synth_name[i], "synth_%i", i);
		urid = props_register(&handle->props, &synth_name_def[i], PROP_EVENT_NONE, NULL, &handle->synth_name[i]);
	}
	if(urid)
	{
		props_sort(&handle->props);
	}
	else
	{
		free(handle);
		return NULL;
	}

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
	//handle_t *handle = (handle_t *)instance;
	//nothing
}

static LV2_Atom_Forge_Ref
_osc_on(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	const int32_t sid = (int32_t)floor(*handle->sid_offset)
		+ cev->sid % (int32_t)floor(*handle->sid_wrap);
	const int32_t gid = (int32_t)floor(*handle->gid_offset) + cev->gid;
	const int32_t out = (int32_t)floor(*handle->out_offset) + cev->gid;
	const int32_t id = handle->i_group ? gid : sid;
	const int32_t arg_offset = (int32_t)floor(*handle->arg_offset);
	const int32_t arg_num = 4;

	LV2_Atom_Forge_Ref ref;

	if(handle->i_allocate)
	{
		ref = lv2_atom_forge_frame_time(forge, frames);
		if(handle->i_gate)
		{
			if(ref)
			{
				ref = osc_forge_message_vararg(&handle->oforge, forge,
					"/s_new", "siiiiisisi",
					handle->synth_name[cev->gid], id, 0, gid,
					arg_offset + 4, cev->pid,
					"gate", 1,
					"out", out);
				(void)ref;
			}
		}
		else // !handle->i_gate
		{
			if(ref)
			{
				ref = osc_forge_message_vararg(&handle->oforge, forge,
					"/s_new", "siiiiisi",
					handle->synth_name[cev->gid], id, 0, gid,
					arg_offset + 4, cev->pid,
					"out", out);
				(void)ref;
			}
		}
	}
	else if(handle->i_gate)
	{
		ref = lv2_atom_forge_frame_time(forge, frames);
		if(ref)
		{
			ref = osc_forge_message_vararg(&handle->oforge, forge,
				"/n_set", "isi",
				id,
				"gate", 1);
			(void)ref;
		}
	}

	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
	{
		ref = osc_forge_message_vararg(&handle->oforge, forge,
			"/n_setn", "iiiffff",
			id, arg_offset, arg_num,
			cev->x, cev->z, cev->X, cev->Z);
	}

	return ref;
}

static LV2_Atom_Forge_Ref
_osc_off(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	const int32_t sid = (int32_t)floor(*handle->sid_offset)
		+ cev->sid % (int32_t)floor(*handle->sid_wrap);
	const int32_t gid = (int32_t)floor(*handle->gid_offset) + cev->gid;
	const int32_t id = handle->i_group ? gid : sid;

	LV2_Atom_Forge_Ref ref = 1;

	if(handle->i_gate)
	{
		ref = lv2_atom_forge_frame_time(forge, frames);
		if(ref)
		{
			ref = osc_forge_message_vararg(&handle->oforge, forge,
				"/n_set", "isi",
				id,
				"gate", 0);
		}
	}

	return ref;
}

static LV2_Atom_Forge_Ref
_osc_set(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	const int32_t sid = (int32_t)floor(*handle->sid_offset)
		+ cev->sid % (int32_t)floor(*handle->sid_wrap);
	const int32_t gid = (int32_t)floor(*handle->gid_offset) + cev->gid;
	const int32_t id = handle->i_group ? gid : sid;
	const int32_t arg_offset = (int32_t)floor(*handle->arg_offset);
	const int32_t arg_num = 4;

	LV2_Atom_Forge_Ref ref;

	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
	{
		ref = osc_forge_message_vararg(&handle->oforge, forge,
			"/n_setn", "iiiffff",
			id, arg_offset, arg_num,
			cev->x, cev->z, cev->X, cev->Z);
	}

	return ref;
}

static LV2_Atom_Forge_Ref
_osc_idle(handle_t *handle, LV2_Atom_Forge *forge, int64_t frames,
	const chimaera_event_t *cev)
{
	return 1;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;
	
	handle->i_allocate = *handle->allocate != 0.f;
	handle->i_gate = *handle->gate != 0.f;
	handle->i_group = *handle->group != 0.f;

	// prepare osc atom forge
	const uint32_t capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref;
	ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		int64_t frames = ev->time.frames;

		if(  !props_advance(&handle->props, forge, frames, obj, &ref)
			&& chimaera_event_check_type(&handle->cforge, &obj->atom) && ref)
		{
			chimaera_event_t cev;
			chimaera_event_deforge(&handle->cforge, &obj->atom, &cev);

			switch(cev.state)
			{
				case CHIMAERA_STATE_ON:
					ref = _osc_on(handle, forge, frames, &cev);
					break;
				case CHIMAERA_STATE_SET:
					ref = _osc_set(handle, forge, frames, &cev);
					break;
				case CHIMAERA_STATE_OFF:
					ref = _osc_off(handle, forge, frames, &cev);
					break;
				case CHIMAERA_STATE_IDLE:
					ref = _osc_idle(handle, forge, frames, &cev);
					break;
			}
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->osc_out);
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

	if(handle)
		free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;

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
