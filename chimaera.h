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

#ifndef _CHIMAERA_LV2_H
#define _CHIMAERA_LV2_H

#include <stdint.h>

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/util.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
	
#define LV2_OSC__OscEvent					"http://opensoundcontrol.org#OscEvent"

#define CHIMAERA_URI							"http://open-music-kontrollers.ch/lv2/chimaera"

#define CHIMAERA_EVENT_URI				CHIMAERA_URI"#Event"

#define CHIMAERA_DUMMY_IN_URI			CHIMAERA_URI"#dummy_in"
#define CHIMAERA_TUIO2_IN_URI			CHIMAERA_URI"#tuio2_in"

#define CHIMAERA_FILTER_URI				CHIMAERA_URI"#filter"
#define CHIMAERA_CONTROL_OUT_URI	CHIMAERA_URI"#control_out"
#define CHIMAERA_MIDI_OUT_URI			CHIMAERA_URI"#midi_out"
#define CHIMAERA_OSC_OUT_URI			CHIMAERA_URI"#osc_out"

typedef enum _chimaera_state_t		chimaera_state_t;
typedef struct _chimaera_event_t	chimaera_event_t;

enum _chimaera_state_t {
	CHIMAERA_STATE_ON,
	CHIMAERA_STATE_SET,
	CHIMAERA_STATE_OFF,
	CHIMAERA_STATE_IDLE
};

struct _chimaera_event_t {
	chimaera_state_t state;
	uint32_t sid;
	uint32_t gid;
	uint32_t pid;
	float x;
	float z;
	float X;
	float Z;
};

const LV2_Descriptor dummy_in;
const LV2_Descriptor tuio2_in;
const LV2_Descriptor filter;
const LV2_Descriptor control_out;
const LV2_Descriptor midi_out;
const LV2_Descriptor osc_out;

#endif // _CHIMAERA_LV2_H
