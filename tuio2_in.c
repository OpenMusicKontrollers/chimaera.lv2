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

typedef struct _ref_t ref_t;
typedef struct _handle_t handle_t;

struct _ref_t {
	uint32_t tuid;
	uint32_t gid;
	float x;
	float z;
	float a;
	float X;
	float Z;
	float A;
	float m;
	float R;
};

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID osc_OscEvent;
	} uris;
	chimaera_forge_t cforge;

	chimaera_dict_t dict [2][CHIMAERA_DICT_SIZE];
	ref_t ref [2][CHIMAERA_DICT_SIZE];
	int pos;

	uint32_t fid;
	osc_data_t last;
	uint16_t width;
	uint16_t height;
	int ignore;
	int n;
	int reset;

	const LV2_Atom_Sequence *osc_in;
	const float *reset_in;
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
	CHIMAERA_DICT_INIT(handle->dict[0], handle->ref[0]);
	CHIMAERA_DICT_INIT(handle->dict[1], handle->ref[1]);

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
			handle->reset_in = (const float *)data;
			break;
		case 2:
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
_frm(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	uint32_t fid;
	osc_time_t last;
	uint32_t dim;
	//const char *source;

	ptr = osc_get_int32(ptr, (int32_t *)&fid);
	ptr = osc_get_timetag(ptr, &last);

	if( (fid > handle->fid) && (last >= handle->last) )
	{
		handle->fid = fid;
		handle->last = last;

		ptr = osc_get_int32(ptr, (int32_t *)&dim);
		//ptr = osc_get_string(ptr, &source);
		handle->width = dim >> 16;
		handle->height = dim & 0xffff;

		handle->pos ^= 1; // toggle pos
		chimaera_dict_clear(handle->dict[handle->pos]);

		handle->ignore = 0;
	}
	else
		handle->ignore = 1;

	return 1;
}

static int
_tok(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	ref_t *ref;

	if(handle->ignore)
		return 1;

	uint32_t sid;
	ptr = osc_get_int32(ptr, (int32_t *)&sid);
	
	ref = chimaera_dict_add(handle->dict[handle->pos], sid); // get new blob ref
	if(!ref)
		return 1;

	ptr = osc_get_int32(ptr, (int32_t *)&ref->tuid);
	ptr = osc_get_int32(ptr, (int32_t *)&ref->gid);
	ptr = osc_get_float(ptr, &ref->x);
	ptr = osc_get_float(ptr, &ref->z);
	ptr = osc_get_float(ptr, &ref->a);

	if(strlen(fmt) > 6)
	{
		ptr = osc_get_float(ptr, &ref->X);
		ptr = osc_get_float(ptr, &ref->Z);
		ptr = osc_get_float(ptr, &ref->A);
		ptr = osc_get_float(ptr, &ref->m);
		ptr = osc_get_float(ptr, &ref->R);
	}
	else
	{
		ref->X = NAN;
		ref->Z = NAN;
		ref->A = NAN;
		ref->m = NAN;
		ref->R = NAN;
		//TODO calculate !

		ref->X = 0.f;
		ref->Z = 0.f;
		ref->A = 0.f;
		ref->m = 0.f;
		ref->R = 0.f;
	}

	return 1;
}

static int
_alv(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	chimaera_event_t cev;
	int n;
	uint32_t sid;
	ref_t *dst;
	ref_t *src;

	if(handle->ignore)
		return 1;

	n = strlen(fmt);

	for(int i=0; i<n; i++)
	{
		ptr = osc_get_int32(ptr, (int32_t *)&sid);

		// already registered in this step?
		dst = chimaera_dict_ref(handle->dict[handle->pos], sid);
		if(!dst)
		{
			// register in this step
			dst = chimaera_dict_add(handle->dict[handle->pos], sid);
			// clone from previous step
			src = chimaera_dict_ref(handle->dict[!handle->pos], sid);
			if(dst && src)
				memcpy(dst, src, sizeof(ref_t));
		}
	}

	// iterate over last step's blobs
	CHIMAERA_DICT_FOREACH(handle->dict[!handle->pos], sid, src)
	{
		// is it registered in this step?
		if(!chimaera_dict_ref(handle->dict[handle->pos], sid))
		{
			// is disappeared blob
			cev.state = CHIMAERA_STATE_OFF;
			cev.sid = sid;
			cev.gid = src->gid;
			cev.pid = src->tuid & 0xffff;
			cev.x = src->x;
			cev.z = src->z;
			cev.X = src->X;
			cev.Z = src->Z;

			_chim_event(handle, time, &cev);
		}
	}

	// iterate over this step's blobs
	CHIMAERA_DICT_FOREACH(handle->dict[handle->pos], sid, dst)
	{
		cev.sid = sid;
		cev.gid = dst->gid;
		cev.pid = dst->tuid & 0xffff;
		cev.x = dst->x;
		cev.z = dst->z;
		cev.X = dst->X;
		cev.Z = dst->Z;

		// was it registered in previous step?
		if(!chimaera_dict_ref(handle->dict[!handle->pos], sid))
			cev.state = CHIMAERA_STATE_ON;
		else
			cev.state = CHIMAERA_STATE_SET;

		_chim_event(handle, time, &cev);
	}

	if(!n && !handle->n)
	{
		// is idling
		cev.state = CHIMAERA_STATE_IDLE;
		cev.sid = 0;
		cev.gid = 0;
		cev.pid = 0;
		cev.x = 0.f;
		cev.z = 0.f;
		cev.X = 0.f;
		cev.Z = 0.f;

		_chim_event(handle, time, &cev);
	}

	handle->n = n;

	return 1;
}

static const osc_method_t tuio2 [] = {
	{"/tuio2/frm", "itis", _frm},

	{"/tuio2/tok", "iiifff", _tok},
	{"/tuio2/tok", "iiiffffffff", _tok},

	{"/tuio2/alv", NULL, _alv},

	{NULL, NULL, NULL}
};

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	int reset = *handle->reset_in > 0.f ? 1 : 0;
	if(reset && !handle->reset)
	{
		chimaera_dict_clear(handle->dict[0]);
		chimaera_dict_clear(handle->dict[1]);
		handle->pos = 0;

		handle->fid = 0;
		handle->last = 0;
		handle->width = 0;
		handle->height = 0;
		handle->ignore = 0;
		handle->n = 0;
	}
	handle->reset = reset;

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
				osc_dispatch_method(frames, (osc_data_t *)buf, len, (osc_method_t *)tuio2, NULL, NULL, handle);
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

const LV2_Descriptor tuio2_in = {
	.URI						= CHIMAERA_TUIO2_IN_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
