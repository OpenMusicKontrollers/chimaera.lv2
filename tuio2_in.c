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

#define DICT_SIZE 64

typedef struct _dict_t dict_t;
typedef struct _handle_t handle_t;

struct _dict_t {
	uint32_t sid;
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
		LV2_URID chim_Event;
		LV2_URID osc_OscEvent;
	} uris;

	dict_t dict [2][DICT_SIZE];
	int pos;

	uint32_t fid;
	osc_data_t last;
	uint16_t width;
	uint16_t height;
	int ignore;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *event_out;
	LV2_Atom_Forge forge;
};

static inline dict_t *
_dict_clear(handle_t *handle, int pos)
{
	dict_t *dict = handle->dict[pos];

	for(int i=0; i<DICT_SIZE; i++)
		dict[i].sid = 0;

	return NULL;
}

static inline dict_t *
_dict_ref(handle_t *handle, int pos, uint32_t ref)
{
	dict_t *dict = handle->dict[pos];

	for(int i=0; i<DICT_SIZE; i++)
		if(dict[i].sid == ref)
			return &dict[i];

	return NULL;
}

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
	handle->uris.chim_Event = handle->map->map(handle->map->handle, CHIMAERA_EVENT_URI);
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
	LV2_Atom_Forge *forge = &handle->forge;
		
	LV2_Atom chim_atom;
	chim_atom.type = handle->uris.chim_Event;
	chim_atom.size = sizeof(chimaera_event_t);
		
	lv2_atom_forge_frame_time(forge, frames);
	lv2_atom_forge_raw(forge, &chim_atom, sizeof(LV2_Atom));
	lv2_atom_forge_raw(forge, cev, sizeof(chimaera_event_t));
	lv2_atom_forge_pad(forge, sizeof(chimaera_event_t));
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
		handle->ignore = 0;

		handle->pos ^= 1; // toggle pos
		_dict_clear(handle, handle->pos);
	}
	else
	{
		handle->ignore = 1;
	}

	return 1;
}

static int
_tok(osc_time_t time, const char *path, const char *fmt, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;
	osc_data_t *ptr = buf;
	dict_t *dict;
	
	if(handle->ignore)
		return 1;
	
	dict =_dict_ref(handle, handle->pos, 0);
	if(!dict)
		return 1;

	ptr = osc_get_int32(ptr, (int32_t *)&dict->sid);
	ptr = osc_get_int32(ptr, (int32_t *)&dict->tuid);
	ptr = osc_get_int32(ptr, (int32_t *)&dict->gid);
	ptr = osc_get_float(ptr, &dict->x);
	ptr = osc_get_float(ptr, &dict->z);
	ptr = osc_get_float(ptr, &dict->a);

	if(strlen(fmt) > 6)
	{
		ptr = osc_get_float(ptr, &dict->X);
		ptr = osc_get_float(ptr, &dict->Z);
		ptr = osc_get_float(ptr, &dict->A);
		ptr = osc_get_float(ptr, &dict->m);
		ptr = osc_get_float(ptr, &dict->R);
	}
	else
	{
		dict->X = NAN;
		dict->Z = NAN;
		dict->A = NAN;
		dict->m = NAN;
		dict->R = NAN;
		//TODO calculate !
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
	dict_t *dict;
	dict_t *ref;

	if(handle->ignore)
		return 1;

	n = strlen(fmt);

	if(!n)
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

		return 1;
	}

	for(int i=0; i<n; i++)
	{
		ptr = osc_get_int32(ptr, (int32_t *)&sid);

		dict = _dict_ref(handle, handle->pos, sid);
		if(!dict)
		{
			dict = _dict_ref(handle, handle->pos, 0);
			ref = _dict_ref(handle, !handle->pos, sid);
			memcpy(dict, ref, sizeof(dict_t));
		}
	}

	// discover disappeared
	ref = handle->dict[!handle->pos];
	for(int i=0; i<DICT_SIZE; i++)
	{
		if(!ref[i].sid)
			break; // end of dict

		if(!_dict_ref(handle, handle->pos, ref[i].sid))
		{
			// is disappeared blob
			cev.state = CHIMAERA_STATE_OFF;
			cev.sid = ref[i].sid;
			cev.gid = ref[i].gid;
			cev.pid = ref[i].tuid & 0xffff;
			cev.x = ref[i].x;
			cev.z = ref[i].z;
			cev.X = ref[i].X;
			cev.Z = ref[i].Z;

			_chim_event(handle, time, &cev);
		}
	}

	// discover newly appeared and updated blobs
	dict = handle->dict[handle->pos];
	for(int i=0; i<DICT_SIZE; i++)
	{
		if(!dict[i].sid)
			break; // end of dict

		cev.sid = dict[i].sid;
		cev.gid = dict[i].gid;
		cev.pid = dict[i].tuid & 0xffff;
		cev.x = dict[i].x;
		cev.z = dict[i].z;
		cev.X = dict[i].X;
		cev.Z = dict[i].Z;

		if(_dict_ref(handle, !handle->pos, dict[i].sid))
		{
			// is existing blob
			cev.state = CHIMAERA_STATE_SET;
		}
		else
		{
			// is newly appeared blob
			cev.state = CHIMAERA_STATE_ON;
		}

		_chim_event(handle, time, &cev);
	}

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

	// prepare osc atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
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
