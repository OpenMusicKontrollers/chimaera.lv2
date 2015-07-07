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

typedef struct _ref_t ref_t;
typedef struct _handle_t handle_t;

enum {
	Z_MAPPING_CONTROL_CHANGE = 0,
	Z_MAPPING_NOTE_PRESSURE = 1,
	Z_MAPPING_CHANNEL_PRESSURE = 2
};

struct _ref_t {
	uint8_t chn;
	uint8_t key;
};

struct _handle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;
	chimaera_forge_t cforge;

	chimaera_dict_t dict [CHIMAERA_DICT_SIZE];
	ref_t ref [CHIMAERA_DICT_SIZE];
	float bot;
	float ran;
	float ran_1; // 1 / ran
	int n;
	int oct;

	const LV2_Atom_Sequence *event_in;
	const float *sensors;
	const float *z_mapping;
	const float *octave;
	const float *controller;
	LV2_Atom_Sequence *midi_out;
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

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);
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
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->midi_out = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->sensors = (const float *)data;
			break;
		case 3:
			handle->z_mapping = (const float *)data;
			break;
		case 4:
			handle->octave = (const float *)data;
			break;
		case 5:
			handle->controller = (const float *)data;
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
_midi_event(handle_t *handle, int64_t frames, const uint8_t *m, size_t len)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
		
	lv2_atom_forge_frame_time(forge, frames);
	lv2_atom_forge_atom(forge, len, handle->uris.midi_MidiEvent);
	lv2_atom_forge_raw(forge, m, len);
	lv2_atom_forge_pad(forge, len);
}

static inline void
_midi_on(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	ref_t *ref = chimaera_dict_add(handle->dict, cev->sid);
	if(!ref)
		return;
	
	const float val = handle->bot + cev->x * handle->ran;

	const uint8_t chn = cev->gid & 0x0f;
	const uint8_t key = floor(val);
	const uint8_t vel = 0x7f;

	const uint8_t note_on [3] = {
		0x90 | chn,
		key,
		vel
	};
	_midi_event(handle, frames, note_on, 3);
	
	ref->chn = chn;
	ref->key = key;
}

static inline void
_midi_off(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	ref_t *ref = chimaera_dict_del(handle->dict, cev->sid);
	if(!ref)
		return;

	const uint8_t chn = ref->chn;
	const uint8_t key = ref->key;
	const uint8_t vel = 0x7f;

	const uint8_t note_off [3] = {
		0x80 | chn,
		key,
		vel
	};
	_midi_event(handle, frames, note_off, 3);
}

static inline void
_midi_set(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	ref_t *ref = chimaera_dict_ref(handle->dict, cev->sid);
	if(!ref)
		return;

	const uint8_t controller = *handle->controller;
	const float val = handle->bot + cev->x * handle->ran;
	
	const uint8_t chn = ref->chn;
	const uint8_t key = ref->key;

	const uint16_t bnd = (val-key) * handle->ran_1 * 0x2000 + 0x1fff;
	const uint8_t bnd_msb = bnd >> 7;
	const uint8_t bnd_lsb = bnd & 0x7f;

	const uint8_t bend [3] = {
		0xe0 | chn,
		bnd_lsb,
		bnd_msb
	};
	_midi_event(handle, frames, bend, 3);


	const int z_mapping = floor(*handle->z_mapping);
	const uint16_t eff = cev->z * 0x3fff;
	const uint8_t eff_msb = eff >> 7;
	const uint8_t eff_lsb = eff & 0x7f;

	switch(z_mapping)
	{
		case Z_MAPPING_CONTROL_CHANGE:
		{
			if(controller <= 0x0d)
			{
				const uint8_t control_lsb [3] = {
					0xb0 | chn,
					0x20 | controller,
					eff_lsb
				};
				_midi_event(handle, frames, control_lsb, 3);
			}

			const uint8_t control_msb [3] = {
				0xb0 | chn,
				controller,
				eff_msb
			};
			_midi_event(handle, frames, control_msb, 3);

			break;
		}
		case Z_MAPPING_NOTE_PRESSURE:
		{
			const uint8_t note_pressure [3] = {
				0xa0 | chn,
				key,
				eff_msb
			};
			_midi_event(handle, frames, note_pressure, 3);

			break;
		}
		case Z_MAPPING_CHANNEL_PRESSURE:
		{
			const uint8_t channel_pressure [2] = {
				0xd0 | chn,
				eff_msb
			};
			_midi_event(handle, frames, channel_pressure, 2);

			break;
		}
	}
}

static inline void
_midi_idle(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	chimaera_dict_clear(handle->dict);
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	int n = *handle->sensors;
	int oct = *handle->octave;

	if(n != handle->n)
	{
		handle->n = n;
		handle->ran = (float)n / 3.f;
		handle->ran_1 = 1.f / handle->ran;
	}

	if(oct != handle->oct)
	{
		handle->oct = oct;
		handle->bot = oct*12.f - 0.5 - (n % 18 / 6.f);
	}

	// prepare midi atom forge
	const uint32_t capacity = handle->midi_out->atom.size;
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->midi_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(chimaera_event_check_type(&handle->cforge, &ev->body))
		{
			const int64_t frames = ev->time.frames;
			chimaera_event_t cev;

			chimaera_event_deforge(&handle->cforge, &ev->body, &cev);

			switch(cev.state)
			{
				case CHIMAERA_STATE_ON:
					_midi_on(handle, frames, &cev);
					// fall-through
				case CHIMAERA_STATE_SET:
					_midi_set(handle, frames, &cev);
					break;
				case CHIMAERA_STATE_OFF:
					_midi_off(handle, frames, &cev);
					break;
				case CHIMAERA_STATE_IDLE:
					_midi_idle(handle, frames, &cev);
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

const LV2_Descriptor midi_out = {
	.URI						= CHIMAERA_MIDI_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
