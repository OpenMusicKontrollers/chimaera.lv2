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
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#define LV2_OSC__OscEvent					"http://opensoundcontrol.org#OscEvent"

// bundle uris
#define CHIMAERA_URI							"http://open-music-kontrollers.ch/lv2/chimaera"

// event uris
#define CHIMAERA_EVENT_URI				CHIMAERA_URI"#Event"

#if !defined(CHIMAERA_FAST_DISPATCH)
#	define CHIMAERA_STATE_NONE_URI	CHIMAERA_URI"#StateNone"
#	define CHIMAERA_STATE_ON_URI		CHIMAERA_URI"#StateOn"
#	define CHIMAERA_STATE_SET_URI		CHIMAERA_URI"#StateSet"
#	define CHIMAERA_STATE_OFF_URI		CHIMAERA_URI"#StateOff"
#	define CHIMAERA_STATE_IDLE_URI	CHIMAERA_URI"#StateIdle"

#	define CHIMAERA_PROP_SID_URI		CHIMAERA_URI"#PropSID"
#	define CHIMAERA_PROP_GID_URI		CHIMAERA_URI"#PropGID"
#	define CHIMAERA_PROP_PID_URI		CHIMAERA_URI"#PropPID"
#	define CHIMAERA_PROP_XPOS_URI		CHIMAERA_URI"#PropXPos"
#	define CHIMAERA_PROP_ZPOS_URI		CHIMAERA_URI"#PropZPos"
#	define CHIMAERA_PROP_XVEL_URI		CHIMAERA_URI"#PropXVel"
#	define CHIMAERA_PROP_ZVEL_URI		CHIMAERA_URI"#PropZVel"
#endif

// dump uri
#define CHIMAERA_DUMP_URI					CHIMAERA_URI"#Dump"
#define CHIMAERA_SENSORS_URI			CHIMAERA_URI"#Sensors"
#define CHIMAERA_VALUES_URI				CHIMAERA_URI"#Values"

// plugin uris
#define CHIMAERA_DUMMY_IN_URI			CHIMAERA_URI"#dummy_in"
#define CHIMAERA_TUIO2_IN_URI			CHIMAERA_URI"#tuio2_in"
#define CHIMAERA_DUMP_IN_URI			CHIMAERA_URI"#dump_in"

#define CHIMAERA_FILTER_URI				CHIMAERA_URI"#filter"
#define CHIMAERA_MAPPER_URI				CHIMAERA_URI"#mapper"
#define CHIMAERA_CONTROL_OUT_URI	CHIMAERA_URI"#control_out"
#define CHIMAERA_MIDI_OUT_URI			CHIMAERA_URI"#midi_out"
#define CHIMAERA_OSC_OUT_URI			CHIMAERA_URI"#osc_out"
#define CHIMAERA_SIMULATOR_URI		CHIMAERA_URI"#simulator"
#define CHIMAERA_VISUALIZER_URI		CHIMAERA_URI"#visualizer"
#define CHIMAERA_INJECTOR_URI			CHIMAERA_URI"#injector"

const LV2_Descriptor dummy_in;
const LV2_Descriptor dump_in;
const LV2_Descriptor tuio2_in;
const LV2_Descriptor filter;
const LV2_Descriptor mapper;
const LV2_Descriptor control_out;
const LV2_Descriptor midi_out;
const LV2_Descriptor osc_out;
const LV2_Descriptor simulator;
const LV2_Descriptor visualizer;
const LV2_Descriptor injector;

// ui plugins uris
#if defined(CHIMAERA_UI_PLUGINS)
#	define CHIMAERA_SIMULATOR_EO_URI	CHIMAERA_URI"#simulator_eo"
#	define CHIMAERA_VISUALIZER_EO_URI	CHIMAERA_URI"#visualizer_eo"

#	define CHIMAERA_SIMULATOR_UI_URI	CHIMAERA_URI"#simulator_ui"
#	define CHIMAERA_VISUALIZER_UI_URI	CHIMAERA_URI"#visualizer_ui"

const LV2UI_Descriptor simulator_eo;
const LV2UI_Descriptor visualizer_eo;

const LV2UI_Descriptor simulator_ui;
const LV2UI_Descriptor visualizer_ui;
#endif

// bundle enums and structs
typedef enum _chimaera_state_t		chimaera_state_t;
typedef struct _chimaera_event_t	chimaera_event_t;
typedef struct _chimaera_dump_t		chimaera_dump_t;
typedef struct _chimaera_forge_t	chimaera_forge_t;
typedef struct _chimaera_dict_t		chimaera_dict_t;

enum _chimaera_state_t {
	CHIMAERA_STATE_NONE		= 0,
	CHIMAERA_STATE_ON			= 1,
	CHIMAERA_STATE_SET		= 2,
	CHIMAERA_STATE_OFF		= 4,
	CHIMAERA_STATE_IDLE		= 8
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

struct _chimaera_dump_t {
	uint32_t sensors;
	int32_t values [160];
};

struct _chimaera_forge_t {
	LV2_Atom_Forge forge;

	struct {
		LV2_URID event;

		LV2_URID dump;
		LV2_URID sensors;
		LV2_URID values;

#if !defined(CHIMAERA_FAST_DISPATCH)
		LV2_URID none;
		LV2_URID on;
		LV2_URID set;
		LV2_URID off;
		LV2_URID idle;

		LV2_URID sid;
		LV2_URID gid;
		LV2_URID pid;
		LV2_URID x;
		LV2_URID z;
		LV2_URID X;
		LV2_URID Z;
#endif
	} uris;
};

struct _chimaera_dict_t {
	uint32_t sid;
	void *ref;
};

// bundle functions
#if defined(CHIMAERA_FAST_DISPATCH)

static inline void
chimaera_forge_init(chimaera_forge_t *cforge, LV2_URID_Map *map)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	cforge->uris.event = map->map(map->handle, CHIMAERA_EVENT_URI);
	cforge->uris.dump = map->map(map->handle, CHIMAERA_DUMP_URI);
	cforge->uris.sensors = map->map(map->handle, CHIMAERA_SENSORS_URI);
	cforge->uris.values = map->map(map->handle, CHIMAERA_VALUES_URI);

	lv2_atom_forge_init(forge, map);
}

static inline void
chimaera_dump_forge(chimaera_forge_t *cforge, chimaera_dump_t *dump)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	size_t size = sizeof(uint32_t) + dump->sensors*sizeof(int32_t);

	lv2_atom_forge_atom(forge, size, cforge->uris.dump);
 	lv2_atom_forge_raw(forge, dump, size); 
	lv2_atom_forge_pad(forge, size);
}

static inline void
chimaera_dump_deforge(const chimaera_forge_t *cforge, const LV2_Atom *atom,
	chimaera_dump_t *dump)
{
	const chimaera_dump_t *ptr;

	ptr = LV2_ATOM_CONTENTS_CONST(LV2_Atom, atom);
	memcpy(dump, ptr, atom->size);
}

static inline int
chimaera_dump_check_type(const chimaera_forge_t *cforge, const LV2_Atom *atom)
{
	if(atom->type == cforge->uris.dump)
		return 1;
	
	return 0;
}

static inline void
chimaera_event_forge(chimaera_forge_t *cforge, const chimaera_event_t *ev)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	lv2_atom_forge_atom(forge, sizeof(chimaera_event_t), cforge->uris.event);
 	lv2_atom_forge_raw(forge, ev, sizeof(chimaera_event_t)); 
	lv2_atom_forge_pad(forge, sizeof(chimaera_event_t));
}

static inline int
chimaera_event_check_type(const chimaera_forge_t *cforge, const LV2_Atom *atom)
{
	if(atom->type == cforge->uris.event)
		return 1;
	
	return 0;
}

static inline void
chimaera_event_deforge(const chimaera_forge_t *cforge, const LV2_Atom *atom,
	chimaera_event_t *ev)
{
	const chimaera_event_t *ptr;

	ptr = LV2_ATOM_BODY_CONST(atom);
	ev->state = ptr->state;
	ev->sid = ptr->sid;
	ev->gid = ptr->gid;
	ev->pid = ptr->pid;
	ev->x = ptr->x;
	ev->z = ptr->z;
	ev->X = ptr->X;
	ev->Z = ptr->Z;
}

#else // !defined(CHIMAERA_FAST_DISPATCH)

static inline void
chimaera_forge_init(chimaera_forge_t *cforge, LV2_URID_Map *map)
{
	LV2_Atom_Forge *forge = &cforge->forge;

	cforge->uris.event = map->map(map->handle, CHIMAERA_EVENT_URI);
	cforge->uris.dump = map->map(map->handle, CHIMAERA_DUMP_URI);
	cforge->uris.sensors = map->map(map->handle, CHIMAERA_SENSORS_URI);
	cforge->uris.values = map->map(map->handle, CHIMAERA_VALUES_URI);

	cforge->uris.none = map->map(map->handle, CHIMAERA_STATE_NONE_URI);
	cforge->uris.on = map->map(map->handle, CHIMAERA_STATE_ON_URI);
	cforge->uris.set = map->map(map->handle, CHIMAERA_STATE_SET_URI);
	cforge->uris.off = map->map(map->handle, CHIMAERA_STATE_OFF_URI);
	cforge->uris.idle = map->map(map->handle, CHIMAERA_STATE_IDLE_URI);

	cforge->uris.sid = map->map(map->handle, CHIMAERA_PROP_SID_URI);
	cforge->uris.gid = map->map(map->handle, CHIMAERA_PROP_GID_URI);
	cforge->uris.pid = map->map(map->handle, CHIMAERA_PROP_PID_URI);
	cforge->uris.x = map->map(map->handle, CHIMAERA_PROP_XPOS_URI);
	cforge->uris.z = map->map(map->handle, CHIMAERA_PROP_ZPOS_URI);
	cforge->uris.X = map->map(map->handle, CHIMAERA_PROP_XVEL_URI);
	cforge->uris.Z = map->map(map->handle, CHIMAERA_PROP_ZVEL_URI);

	lv2_atom_forge_init(forge, map);
}

static inline void
chimaera_dump_forge(chimaera_forge_t *cforge, chimaera_dump_t *dump)
{
	LV2_Atom_Forge *forge = &cforge->forge;
	LV2_Atom_Forge_Frame frame;

	lv2_atom_forge_object(forge, &frame, 0, cforge->uris.dump);
		lv2_atom_forge_key(forge, cforge->uris.sensors);
		lv2_atom_forge_int(forge, dump->sensors);

		lv2_atom_forge_key(forge, cforge->uris.values);
		lv2_atom_forge_vector(forge, sizeof(int32_t), forge->Int, dump->sensors,
			dump->values);
	lv2_atom_forge_pop(forge, &frame);
}

static inline void
chimaera_dump_deforge(const chimaera_forge_t *cforge, const LV2_Atom *atom,
	chimaera_dump_t *dump)
{
	const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;
	const LV2_Atom_Int *sensors = NULL;
	const LV2_Atom_Vector *vec = NULL;

	LV2_Atom_Object_Query q [] = {
		{ cforge->uris.sensors, (const LV2_Atom **)&sensors },
		{ cforge->uris.values, (const LV2_Atom **)&vec },
		LV2_ATOM_OBJECT_QUERY_END
	};
	lv2_atom_object_query(obj, q);
	
	const int32_t *values = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Vector, vec);

	dump->sensors = sensors->body;
	for(int i=0; i<dump->sensors; i++)
		dump->values[i] = values[i];
}

static inline int
chimaera_dump_check_type(const chimaera_forge_t *cforge, const LV2_Atom *atom)
{
	const LV2_Atom_Forge *forge = &cforge->forge;
	const LV2_Atom_Object *obj = (LV2_Atom_Object *)atom;

	if(lv2_atom_forge_is_object_type(forge, atom->type)
			&& (obj->body.otype == cforge->uris.dump) )
		return 1;
	
	return 0;
}

static inline void
chimaera_event_forge(chimaera_forge_t *cforge, const chimaera_event_t *ev)
{
	LV2_Atom_Forge *forge = &cforge->forge;
	LV2_Atom_Forge_Frame frame;

	LV2_URID id = 0;
	switch(ev->state)
	{
		case CHIMAERA_STATE_NONE:
			id = cforge->uris.none;
			break;
		case CHIMAERA_STATE_ON:
			id = cforge->uris.on;
			break;
		case CHIMAERA_STATE_SET:
			id = cforge->uris.set;
			break;
		case CHIMAERA_STATE_OFF:
			id = cforge->uris.off;
			break;
		case CHIMAERA_STATE_IDLE:
			id = cforge->uris.idle;
			break;
	}

	lv2_atom_forge_object(forge, &frame, id, cforge->uris.event);

	if( (ev->state != CHIMAERA_STATE_NONE) && (ev->state != CHIMAERA_STATE_IDLE) )
	{
		lv2_atom_forge_key(forge, cforge->uris.sid);
		lv2_atom_forge_int(forge, ev->sid);

		lv2_atom_forge_key(forge, cforge->uris.gid);
		lv2_atom_forge_int(forge, ev->gid);

		lv2_atom_forge_key(forge, cforge->uris.pid);
		lv2_atom_forge_int(forge, ev->pid);

		lv2_atom_forge_key(forge, cforge->uris.x);
		lv2_atom_forge_float(forge, ev->x);

		lv2_atom_forge_key(forge, cforge->uris.z);
		lv2_atom_forge_float(forge, ev->z);

		lv2_atom_forge_key(forge, cforge->uris.X);
		lv2_atom_forge_float(forge, ev->X);

		lv2_atom_forge_key(forge, cforge->uris.Z);
		lv2_atom_forge_float(forge, ev->Z);
	}
   
	lv2_atom_forge_pop(forge, &frame);
}

static inline int
chimaera_event_check_type(const chimaera_forge_t *cforge, const LV2_Atom *atom)
{
	const LV2_Atom_Forge *forge = &cforge->forge;
	const LV2_Atom_Object *obj = (LV2_Atom_Object *)atom;

	if(lv2_atom_forge_is_object_type(forge, atom->type)
			&& (obj->body.otype == cforge->uris.event) )
		return 1;
	
	return 0;
}

static inline void
chimaera_event_deforge(const chimaera_forge_t *cforge, const LV2_Atom *atom,
	chimaera_event_t *ev)
{
	const LV2_Atom_Forge *forge = &cforge->forge;
	const LV2_Atom_Object *obj = (LV2_Atom_Object *)atom;

	if(obj->body.id == cforge->uris.none)
		ev->state = CHIMAERA_STATE_NONE;
	else if(obj->body.id == cforge->uris.on)
		ev->state = CHIMAERA_STATE_ON;
	else if(obj->body.id == cforge->uris.set)
		ev->state = CHIMAERA_STATE_SET;
	else if(obj->body.id == cforge->uris.off)
		ev->state = CHIMAERA_STATE_OFF;
	else if(obj->body.id == cforge->uris.idle)
		ev->state = CHIMAERA_STATE_IDLE;

	LV2_ATOM_OBJECT_FOREACH(obj, prop)
	{
		if(prop->key == cforge->uris.sid)
			ev->sid = ((LV2_Atom_Int *)&prop->value)->body;
		else if(prop->key == cforge->uris.gid)
			ev->gid = ((LV2_Atom_Int *)&prop->value)->body;
		else if(prop->key == cforge->uris.pid)
			ev->pid = ((LV2_Atom_Int *)&prop->value)->body;
		else if(prop->key == cforge->uris.x)
			ev->x = ((LV2_Atom_Float *)&prop->value)->body;
		else if(prop->key == cforge->uris.z)
			ev->z = ((LV2_Atom_Float *)&prop->value)->body;
		else if(prop->key == cforge->uris.X)
			ev->X = ((LV2_Atom_Float *)&prop->value)->body;
		else if(prop->key == cforge->uris.Z)
			ev->Z = ((LV2_Atom_Float *)&prop->value)->body;
	}
}

#endif

#if !defined(CHIMAERA_DICT_SIZE)
#	define CHIMAERA_DICT_SIZE 16
#endif

#define CHIMAERA_DICT_INIT(DICT, REF) \
({ \
	for(int i=0; i<CHIMAERA_DICT_SIZE; i++) \
	{ \
		(DICT)[i].sid = 0; \
		(DICT)[i].ref = (REF) + i; \
	} \
})

#define CHIMAERA_DICT_FOREACH(DICT, SID, REF) \
	for(int i=0; i<CHIMAERA_DICT_SIZE; i++) \
		if( (((SID) = (DICT)[i].sid) != 0) && ((REF) = (DICT)[i].ref) )

static inline void
chimaera_dict_clear(chimaera_dict_t *dict)
{
	for(int i=0; i<CHIMAERA_DICT_SIZE; i++)
		dict[i].sid = 0;
}

static inline void *
chimaera_dict_add(chimaera_dict_t *dict, uint32_t sid)
{
	for(int i=0; i<CHIMAERA_DICT_SIZE; i++)
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
	for(int i=0; i<CHIMAERA_DICT_SIZE; i++)
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
	for(int i=0; i<CHIMAERA_DICT_SIZE; i++)
		if(dict[i].sid == sid)
			return dict[i].ref;

	return NULL;
}

#endif // _CHIMAERA_LV2_H
