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

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#define _ATOM_ALIGNED __attribute__((aligned(8)))

#if defined(HAS_BUILTIN_ASSUME_ALIGNED)
#	define ASSUME_ALIGNED(PTR) __builtin_assume_aligned((PTR), 8)
#else
#	define ASSUME_ALIGNED(PTR) (PTR)
#endif


// bundle uri
#define CHIMAERA_URI							"http://open-music-kontrollers.ch/lv2/chimaera"

// event uri
#define CHIMAERA_EVENT_URI				CHIMAERA_URI"#event"

// state uris
#define CHIMAERA_STATE_ON_URI			CHIMAERA_URI"#on"
#define CHIMAERA_STATE_SET_URI		CHIMAERA_URI"#set"
#define CHIMAERA_STATE_OFF_URI		CHIMAERA_URI"#off"
#define CHIMAERA_STATE_IDLE_URI		CHIMAERA_URI"#idle"

// dump uri
#define CHIMAERA_DUMP_URI					CHIMAERA_URI"#dump"

// plugin uris
#define CHIMAERA_FILTER_URI				CHIMAERA_URI"#filter"
#define CHIMAERA_MAPPER_URI				CHIMAERA_URI"#mapper"
#define CHIMAERA_CONTROL_OUT_URI	CHIMAERA_URI"#control_out"
#define CHIMAERA_MIDI_OUT_URI			CHIMAERA_URI"#midi_out"
#define CHIMAERA_OSC_OUT_URI			CHIMAERA_URI"#osc_out"
#define CHIMAERA_SIMULATOR_URI		CHIMAERA_URI"#simulator"
#define CHIMAERA_VISUALIZER_URI		CHIMAERA_URI"#visualizer"
#define CHIMAERA_DRIVER_URI				CHIMAERA_URI"#driver"

extern const LV2_Descriptor filter;
extern const LV2_Descriptor mapper;
extern const LV2_Descriptor control_out;
extern const LV2_Descriptor midi_out;
extern const LV2_Descriptor osc_out;
extern const LV2_Descriptor simulator;
extern const LV2_Descriptor visualizer;
extern const LV2_Descriptor driverer;

// ui plugins uris
#if defined(CHIMAERA_UI_PLUGINS)
#	define CHIMAERA_SIMULATOR_EO_URI	CHIMAERA_URI"#simulator_eo"
#	define CHIMAERA_SIMULATOR_UI_URI	CHIMAERA_URI"#simulator_ui"
#	define CHIMAERA_SIMULATOR_X11_URI	CHIMAERA_URI"#simulator_x11"
#	define CHIMAERA_SIMULATOR_KX_URI	CHIMAERA_URI"#simulator_kx"

#	define CHIMAERA_VISUALIZER_EO_URI	CHIMAERA_URI"#visualizer_eo"
#	define CHIMAERA_VISUALIZER_UI_URI	CHIMAERA_URI"#visualizer_ui"
#	define CHIMAERA_VISUALIZER_X11_URI	CHIMAERA_URI"#visualizer_x11"
#	define CHIMAERA_VISUALIZER_KX_URI	CHIMAERA_URI"#visualizer_kx"

extern const LV2UI_Descriptor simulator_eo;
extern const LV2UI_Descriptor simulator_ui;
extern const LV2UI_Descriptor simulator_x11;
extern const LV2UI_Descriptor simulator_kx;

extern const LV2UI_Descriptor visualizer_eo;
extern const LV2UI_Descriptor visualizer_ui;
extern const LV2UI_Descriptor visualizer_x11;
extern const LV2UI_Descriptor visualizer_kx;
#endif

// bundle enums and structs
typedef enum _chimaera_state_t		chimaera_state_t;
typedef struct _chimaera_event_t	chimaera_event_t;
typedef struct _chimaera_obj_t		chimaera_obj_t;
typedef struct _chimaera_pack_t		chimaera_pack_t;
typedef struct _chimaera_dump_t		chimaera_dump_t;
typedef struct _chimaera_forge_t	chimaera_forge_t;
typedef struct _chimaera_dict_t		chimaera_dict_t;

enum _chimaera_state_t {
	CHIMAERA_STATE_ON		= 1,
	CHIMAERA_STATE_SET	= 2,
	CHIMAERA_STATE_OFF	= 4,
	CHIMAERA_STATE_IDLE	= 8
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
	float m; //TODO
};

struct _chimaera_obj_t {
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body prop _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _chimaera_pack_t {
	chimaera_obj_t cobj _ATOM_ALIGNED;

	LV2_Atom_Int sid _ATOM_ALIGNED;
	LV2_Atom_Int gid _ATOM_ALIGNED;
	LV2_Atom_Int pid _ATOM_ALIGNED;
	LV2_Atom_Float x _ATOM_ALIGNED;
	LV2_Atom_Float z _ATOM_ALIGNED;
	LV2_Atom_Float X _ATOM_ALIGNED;
	LV2_Atom_Float Z _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _chimaera_dump_t {
	chimaera_obj_t cobj _ATOM_ALIGNED;

	LV2_Atom_Vector_Body vec _ATOM_ALIGNED;
	int32_t values [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

struct _chimaera_forge_t {
	LV2_Atom_Forge forge;

	struct {
		LV2_URID event;

		LV2_URID on;
		LV2_URID set;
		LV2_URID off;
		LV2_URID idle;

		LV2_URID dump;
	} uris;
};

struct _chimaera_dict_t {
	uint32_t sid;
	void *ref;
};

static inline void
chimaera_forge_init(chimaera_forge_t *cforge, LV2_URID_Map *map)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	cforge->uris.event = map->map(map->handle, CHIMAERA_EVENT_URI);

	cforge->uris.on = map->map(map->handle, CHIMAERA_STATE_ON_URI);
	cforge->uris.set = map->map(map->handle, CHIMAERA_STATE_SET_URI);
	cforge->uris.off = map->map(map->handle, CHIMAERA_STATE_OFF_URI);
	cforge->uris.idle = map->map(map->handle, CHIMAERA_STATE_IDLE_URI);

	cforge->uris.dump = map->map(map->handle, CHIMAERA_DUMP_URI);

	lv2_atom_forge_init(forge, map);
}

// dump handling
static inline LV2_Atom_Forge_Ref
chimaera_dump_forge(chimaera_forge_t *cforge, int32_t *values, uint32_t sensors)
{
	LV2_Atom_Forge *forge = &cforge->forge;
	uint32_t values_size = sensors * sizeof(int32_t);
	LV2_Atom_Forge_Ref ref;

	const chimaera_dump_t dump = {
		.cobj = {
			.obj = {
				.atom.type = forge->Object,
				.atom.size = sizeof(chimaera_dump_t) + values_size - sizeof(LV2_Atom),
				.body.id = 0,
				.body.otype = cforge->uris.dump 
			},
			.prop = {
				.key = cforge->uris.dump,
				.context = 0,
				.value.type = forge->Vector,
				.value.size = sizeof(LV2_Atom_Vector_Body) + values_size
			}
		},
		.vec = {
			.child_size = sizeof(int32_t),
			.child_type = forge->Int
		}
	};

	ref = lv2_atom_forge_raw(forge, &dump, sizeof(chimaera_dump_t));
	if(ref)
		ref = lv2_atom_forge_raw(forge, values, values_size); // always a multiple of 8

	return ref;
}

static inline const int32_t *
chimaera_dump_deforge(const chimaera_forge_t *cforge, const LV2_Atom *atom,
	uint32_t *sensors)
{
	const chimaera_dump_t *dump = ASSUME_ALIGNED(atom);

	if(sensors)
		*sensors = dump->vec.child_size / sizeof(int32_t);

	return LV2_ATOM_CONTENTS_CONST(LV2_Atom_Vector_Body, &dump->vec);
}

static inline int
chimaera_dump_check_type(const chimaera_forge_t *cforge, const LV2_Atom *atom)
{
	const LV2_Atom_Forge *forge = &cforge->forge;
	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);

	if(lv2_atom_forge_is_object_type(forge, obj->atom.type)
			&& (obj->body.otype == cforge->uris.dump) )
		return 1;
	
	return 0;
}

// event handle 
static inline LV2_Atom_Forge_Ref
chimaera_event_forge(chimaera_forge_t *cforge, const chimaera_event_t *ev)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	uint32_t otype = 0;
	switch(ev->state)
	{
		case CHIMAERA_STATE_ON:
			otype = cforge->uris.on;
			break;
		case CHIMAERA_STATE_SET:
			otype = cforge->uris.set;
			break;
		case CHIMAERA_STATE_OFF:
			otype = cforge->uris.off;
			break;
		case CHIMAERA_STATE_IDLE:
			otype = cforge->uris.idle;
			break;
	}

	const chimaera_pack_t pack = {
		.cobj = {
			.obj = {
				.atom.type = forge->Object,
				.atom.size = sizeof(chimaera_pack_t) - sizeof(LV2_Atom),
				.body.id = 0,
				.body.otype = otype
			},
			.prop = {
				.key = otype,
				.context = 0,
				.value.type = forge->Tuple,
				.value.size = sizeof(chimaera_pack_t) - sizeof(LV2_Atom_Object) - sizeof(LV2_Atom_Property_Body)
			}
		},
		.sid = {
			.atom.size = sizeof(int32_t),
			.atom.type = forge->Int,
			.body = ev->sid
		},
		.gid = {
			.atom.size = sizeof(int32_t),
			.atom.type = forge->Int,
			.body = ev->gid
		},
		.pid = {
			.atom.size = sizeof(int32_t),
			.atom.type = forge->Int,
			.body = ev->pid
		},
		.x = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->x
		},
		.z = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->z
		},
		.X = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->X
		},
		.Z = {
			.atom.size = sizeof(float),
			.atom.type = forge->Float,
			.body = ev->Z
		}
	};

	return lv2_atom_forge_raw(forge, &pack, sizeof(chimaera_pack_t));
}

static inline int
chimaera_event_check_type(const chimaera_forge_t *cforge, const LV2_Atom *atom)
{
	const LV2_Atom_Forge *forge = &cforge->forge;
	const LV2_Atom_Object *obj = ASSUME_ALIGNED(atom);

	if(lv2_atom_forge_is_object_type(forge, obj->atom.type)
			&& ( (obj->body.otype == cforge->uris.on)
				|| (obj->body.otype == cforge->uris.off)
				|| (obj->body.otype == cforge->uris.set)
				|| (obj->body.otype == cforge->uris.idle) ) )
	{
		return 1;
	}
	
	return 0;
}

static inline void
chimaera_event_deforge(const chimaera_forge_t *cforge, const LV2_Atom *atom,
	chimaera_event_t *ev)
{
	const chimaera_pack_t *pack = ASSUME_ALIGNED(atom);

	uint32_t otype = pack->cobj.obj.body.otype;

	if(otype == cforge->uris.on)
		ev->state = CHIMAERA_STATE_ON;
	else if(otype == cforge->uris.set)
		ev->state = CHIMAERA_STATE_SET;
	else if(otype == cforge->uris.off)
		ev->state = CHIMAERA_STATE_OFF;
	else if(otype == cforge->uris.idle)
		ev->state = CHIMAERA_STATE_IDLE;

	ev->sid = pack->sid.body;
	ev->gid = pack->gid.body;
	ev->pid = pack->pid.body;
	ev->x = pack->x.body;
	ev->z = pack->z.body;
	ev->X = pack->X.body;
	ev->Z = pack->Z.body;
}

#if !defined(CHIMAERA_DICT_SIZE)
#	define CHIMAERA_DICT_SIZE 16
#endif

#define CHIMAERA_DICT_INIT(DICT, REF) \
({ \
	for(unsigned _i=0; _i<CHIMAERA_DICT_SIZE; _i++) \
	{ \
		(DICT)[_i].sid = 0; \
		(DICT)[_i].ref = (REF) + _i; \
	} \
})

#define CHIMAERA_DICT_FOREACH(DICT, SID, REF) \
	for(unsigned _i=0; _i<CHIMAERA_DICT_SIZE; _i++) \
		if( (((SID) = (DICT)[_i].sid) != 0) && ((REF) = (DICT)[_i].ref) )

static inline void
chimaera_dict_clear(chimaera_dict_t *dict)
{
	for(unsigned i=0; i<CHIMAERA_DICT_SIZE; i++)
		dict[i].sid = 0;
}

static inline void *
chimaera_dict_add(chimaera_dict_t *dict, uint32_t sid)
{
	for(unsigned i=0; i<CHIMAERA_DICT_SIZE; i++)
		if(dict[i].sid == 0)
		{
			dict[i].sid = sid;
			return dict[i].ref;
		}

	return NULL;
}

static inline void *
chimaera_dict_del(chimaera_dict_t *dict, uint32_t sid)
{
	for(unsigned i=0; i<CHIMAERA_DICT_SIZE; i++)
		if(dict[i].sid == sid)
		{
			dict[i].sid = 0;
			return dict[i].ref;
		}

	return NULL;
}

static inline void *
chimaera_dict_ref(chimaera_dict_t *dict, uint32_t sid)
{
	for(unsigned i=0; i<CHIMAERA_DICT_SIZE; i++)
		if(dict[i].sid == sid)
			return dict[i].ref;

	return NULL;
}

#endif // _CHIMAERA_LV2_H
