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

struct _ref_t {
	uint8_t voice;
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
	const float *octave;
	LV2_Atom_Sequence *midi_out;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	handle_t *handle = calloc(1, sizeof(handle_t));
	if(!handle)
		return NULL;

	for(unsigned i=0; features[i]; i++)
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

	// initialize voices
	for(unsigned i=0; i<CHIMAERA_DICT_SIZE; i++)
		handle->ref[i].voice = i + 1; //FIXME set voice according to manual

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
			handle->octave = (const float *)data;
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
_midi_event(handle_t *handle, int64_t frames, const uint8_t *m, size_t len)
{
	LV2_Atom_Forge *forge = &handle->cforge.forge;
	LV2_Atom_Forge_Ref ref;
		
	ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = lv2_atom_forge_atom(forge, len, handle->uris.midi_MidiEvent);
	if(ref)
		ref = lv2_atom_forge_raw(forge, m, len);
	if(ref)
		lv2_atom_forge_pad(forge, len);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_midi_on(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	ref_t *ref = chimaera_dict_add(handle->dict, cev->sid);
	if(!ref)
		return 1;

	LV2_Atom_Forge_Ref fref;	
	
	const float val = handle->bot + cev->x * handle->ran;

	const uint8_t voice = ref->voice;
	const uint8_t key = floor(val);
	const uint8_t vel = 0x7f;

	const uint8_t note_on [3] = {
		LV2_MIDI_MSG_NOTE_ON | voice,
		key,
		vel
	};
	fref = _midi_event(handle, frames, note_on, 3);
	
	ref->key = key;

	return fref;
}

static inline LV2_Atom_Forge_Ref
_midi_off(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	ref_t *ref = chimaera_dict_del(handle->dict, cev->sid);
	if(!ref)
		return 1;

	LV2_Atom_Forge_Ref fref;

	const uint8_t voice = ref->voice;
	const uint8_t key = ref->key;
	const uint8_t vel = 0x7f;

	const uint8_t note_off [3] = {
		LV2_MIDI_MSG_NOTE_OFF | voice,
		key,
		vel
	};
	fref = _midi_event(handle, frames, note_off, 3);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_midi_set(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	ref_t *ref = chimaera_dict_ref(handle->dict, cev->sid);
	if(!ref)
		return 1;

	LV2_Atom_Forge_Ref fref;

	const float val = handle->bot + cev->x * handle->ran;
	
	const uint8_t voice = ref->voice;
	const uint8_t key = ref->key;

	// bender
	const uint16_t bnd = (val-key) * handle->ran_1 * 0x2000 + 0x1fff;
	const uint8_t bnd_msb = bnd >> 7;
	const uint8_t bnd_lsb = bnd & 0x7f;

	const uint8_t bend [3] = {
		LV2_MIDI_MSG_BENDER | voice,
		bnd_lsb,
		bnd_msb
	};
	fref = _midi_event(handle, frames, bend, 3);

	// pressure
	const uint16_t z = cev->z * 0x3fff;
	const uint8_t z_msb = z >> 7;
	const uint8_t z_lsb = z & 0x7f;

	const uint8_t pressure_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice,
		LV2_MIDI_CTL_SC1_SOUND_VARIATION | 0x20,
		z_lsb
	};

	const uint8_t pressure_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice,
		LV2_MIDI_CTL_SC1_SOUND_VARIATION,
		z_msb
	};

	if(fref)
		fref = _midi_event(handle, frames, pressure_lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, pressure_msb, 3);

	// timbre
	const uint16_t vx = (cev->X * 0x2000) + 0x1fff; //TODO limit
	const uint8_t vx_msb = vx >> 7;
	const uint8_t vx_lsb = vx & 0x7f;

	const uint8_t timbre_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice,
		LV2_MIDI_CTL_SC5_BRIGHTNESS | 0x20,
		vx_lsb
	};

	const uint8_t timbre_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice,
		LV2_MIDI_CTL_SC5_BRIGHTNESS,
		vx_msb
	};

	if(fref)
		fref = _midi_event(handle, frames, timbre_lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, timbre_msb, 3);

	// timbre
	const uint16_t vz = (cev->Z * 0x2000) + 0x1fff; //TODO limit
	const uint8_t vz_msb = vz >> 7;
	const uint8_t vz_lsb = vz & 0x7f;

	const uint8_t mod_lsb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice,
		LV2_MIDI_CTL_LSB_MODWHEEL,
		vz_lsb
	};

	const uint8_t mod_msb [3] = {
		LV2_MIDI_MSG_CONTROLLER | voice,
		LV2_MIDI_CTL_MSB_MODWHEEL,
		vz_msb
	};

	if(fref)
		fref = _midi_event(handle, frames, mod_lsb, 3);
	if(fref)
		fref = _midi_event(handle, frames, mod_msb, 3);

	return fref;
}

static inline LV2_Atom_Forge_Ref
_midi_idle(handle_t *handle, int64_t frames, const chimaera_event_t *cev)
{
	chimaera_dict_clear(handle->dict);

	LV2_Atom_Forge_Ref fref = 1;

	// create 16/(1+span) zones
	const unsigned span = 1;
	for(unsigned z=1; z<=16; z+=1+span)
	{
		const uint8_t zone_lsb [3] = {
			LV2_MIDI_MSG_CONTROLLER | z,
			LV2_MIDI_CTL_RPN_LSB,
			0x6 // zone
		};

		const uint8_t zone_msb [3] = {
			LV2_MIDI_MSG_CONTROLLER | z,
			LV2_MIDI_CTL_RPN_MSB,
			0x0
		};

		const uint8_t zone_data [3] = {
			LV2_MIDI_MSG_CONTROLLER | z,
			LV2_MIDI_CTL_MSB_DATA_ENTRY,
			span
		};

		if(fref)
			fref = _midi_event(handle, frames, zone_lsb, 3);
		if(fref)
			fref = _midi_event(handle, frames, zone_msb, 3);
		if(fref)
			fref = _midi_event(handle, frames, zone_data, 3);

		const uint8_t bend_range_lsb [3] = {
			LV2_MIDI_MSG_CONTROLLER | z,
			LV2_MIDI_CTL_RPN_LSB,
			0x0, // bend range
		};

		const uint8_t bend_range_msb [3] = {
			LV2_MIDI_MSG_CONTROLLER | z,
			LV2_MIDI_CTL_RPN_MSB,
			0x0,
		};
		const uint8_t bend_range_data [3] = {
			LV2_MIDI_MSG_CONTROLLER | z,
			LV2_MIDI_CTL_MSB_DATA_ENTRY,
			60 //TODO
		};

		if(fref)
			fref = _midi_event(handle, frames, bend_range_lsb, 3);
		if(fref)
			fref = _midi_event(handle, frames, bend_range_msb, 3);
		if(fref)
			fref = _midi_event(handle, frames, bend_range_data, 3);
	}

	return fref;
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
	LV2_Atom_Forge_Ref ref;
	ref = lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		if(chimaera_event_check_type(&handle->cforge, &ev->body) && ref)
		{
			const int64_t frames = ev->time.frames;
			chimaera_event_t cev;

			chimaera_event_deforge(&handle->cforge, &ev->body, &cev);

			switch(cev.state)
			{
				case CHIMAERA_STATE_ON:
					ref = _midi_on(handle, frames, &cev);
					// fall-through
				case CHIMAERA_STATE_SET:
					if(ref)
						ref = _midi_set(handle, frames, &cev);
					break;
				case CHIMAERA_STATE_OFF:
					ref = _midi_off(handle, frames, &cev);
					break;
				case CHIMAERA_STATE_IDLE:
					ref = _midi_idle(handle, frames, &cev);
					break;
			}
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->midi_out);
}

static void
deactivate(LV2_Handle instance)
{
	//handle_t *handle = (handle_t *)instance;
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

const LV2_Descriptor mpe_out = {
	.URI						= CHIMAERA_MPE_OUT_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
