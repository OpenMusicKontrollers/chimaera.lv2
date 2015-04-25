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

#ifndef _OSC_LV2_H
#define _OSC_LV2_H

#include <math.h> // INFINITY

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#define LV2_OSC_PREFIX						"http://opensoundcontrol.org"

#define LV2_OSC__OscEvent					LV2_OSC_PREFIX"#OscEvent"
#define LV2_OSC__OscBundle				LV2_OSC_PREFIX"#OscBundle"
#define LV2_OSC__OscTimestamp			LV2_OSC_PREFIX"#OscTimestamp"
#define LV2_OSC__OscMessage				LV2_OSC_PREFIX"#OscMessage"
#define LV2_OSC__OscPath					LV2_OSC_PREFIX"#OscPath"
#define LV2_OSC__OscFormat				LV2_OSC_PREFIX"#OscFormat"
#define LV2_OSC__OscBody					LV2_OSC_PREFIX"#OscBody"

/*
#define LV2_OSC__IntrospectPath		LV2_OSC_PREFIX"#IntrospectPath"
#define LV2_OSC__IntrospectType		LV2_OSC_PREFIX"#IntrospectType"
#define LV2_OSC__IntrospectNode		LV2_OSC_PREFIX"#IntrospectNode"
#define LV2_OSC__IntrospectMethod	LV2_OSC_PREFIX"#IntrospectMethod"
#define LV2_OSC__IntrospectDesk		LV2_OSC_PREFIX"#IntrospectDesc"
#define LV2_OSC__IntrospectItems	LV2_OSC_PREFIX"#IntrospectItems"
#define LV2_OSC__IntrospectArgs		LV2_OSC_PREFIX"#IntrospectArgs"
#define LV2_OSC__IntrospectRead		LV2_OSC_PREFIX"#IntrospectRead"
#define LV2_OSC__IntrospectWrite	LV2_OSC_PREFIX"#IntrospectWrite"
#define LV2_OSC__IntrospectRange	LV2_OSC_PREFIX"#IntrospectRange"
#define LV2_OSC__IntrospectValues	LV2_OSC_PREFIX"#IntrospectValues"
*/

typedef struct _osc_forge_t osc_forge_t;

struct _osc_forge_t {
	LV2_Atom_Forge forge;

	struct {
		LV2_URID event;

		LV2_URID bundle;
		LV2_URID timestamp;
		LV2_URID message;
		LV2_URID path;
		LV2_URID format;
		LV2_URID body;

		LV2_URID midi;
	} uris;
};

static inline void
osc_forge_init(osc_forge_t *oforge, LV2_URID_Map *map)
{
	LV2_Atom_Forge *forge = &oforge->forge;

	oforge->uris.event = map->map(map->handle, LV2_OSC__OscEvent);
	oforge->uris.bundle = map->map(map->handle, LV2_OSC__OscBundle);
	oforge->uris.timestamp = map->map(map->handle, LV2_OSC__OscTimestamp);
	oforge->uris.message = map->map(map->handle, LV2_OSC__OscMessage);
	oforge->uris.path = map->map(map->handle, LV2_OSC__OscPath);
	oforge->uris.format = map->map(map->handle, LV2_OSC__OscFormat);
	oforge->uris.body = map->map(map->handle, LV2_OSC__OscBody);
	
	oforge->uris.midi = map->map(map->handle, LV2_MIDI__MidiEvent);

	lv2_atom_forge_init(forge, map);
}

static inline int
osc_atom_is_bundle(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	return obj->body.otype == oforge->uris.bundle;
}

static inline uint64_t
osc_atom_bundle_timestamp_get(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	const LV2_Atom_Long* timestamp = NULL;

	LV2_Atom_Object_Query q[] = {
		{ oforge->uris.timestamp, (const LV2_Atom **)&timestamp },
		LV2_ATOM_OBJECT_QUERY_END
	};

	lv2_atom_object_query(obj, q);

	return timestamp
		? timestamp->body
		: 1ULL;
}

static inline int
osc_atom_is_message(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	return obj->body.otype == oforge->uris.message;
}

static inline const char *
osc_atom_message_path_get(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	const LV2_Atom_String* path = NULL;

	LV2_Atom_Object_Query q[] = {
		{ oforge->uris.path, (const LV2_Atom **)&path },
		LV2_ATOM_OBJECT_QUERY_END
	};

	lv2_atom_object_query(obj, q);

	return path
		? LV2_ATOM_BODY_CONST(path)
		: NULL;
}

static inline const char *
osc_atom_message_fmt_get(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	const LV2_Atom_String* fmt = NULL;

	LV2_Atom_Object_Query q[] = {
		{ oforge->uris.format, (const LV2_Atom **)&fmt },
		LV2_ATOM_OBJECT_QUERY_END
	};

	lv2_atom_object_query(obj, q);

	return fmt
		? LV2_ATOM_BODY_CONST(fmt)
		: NULL;
}

static inline const LV2_Atom_Tuple *
osc_atom_body_get(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	const LV2_Atom_Tuple* body = NULL;

	LV2_Atom_Object_Query q[] = {
		{ oforge->uris.body, (const LV2_Atom **)&body },
		LV2_ATOM_OBJECT_QUERY_END
	};

	lv2_atom_object_query(obj, q);

	return body;
}

typedef void (*osc_message_cb_t)(uint64_t timestamp, const char *path,
	const char *fmt, const LV2_Atom_Tuple *body);

static inline void osc_atom_unpack(osc_forge_t *oforge,
	const LV2_Atom_Object *obj, osc_message_cb_t cb);

static inline void
osc_atom_message_unpack(osc_forge_t *oforge, uint64_t timestamp,
	const LV2_Atom_Object *obj, osc_message_cb_t cb)
{
	const LV2_Atom_String* path = NULL;
	const LV2_Atom_String* fmt = NULL;
	const LV2_Atom_Tuple* body = NULL;

	LV2_Atom_Object_Query q[] = {
		{ oforge->uris.path, (const LV2_Atom **)&path },
		{ oforge->uris.format, (const LV2_Atom **)&fmt },
		{ oforge->uris.body, (const LV2_Atom **)&body },
		LV2_ATOM_OBJECT_QUERY_END
	};
	
	lv2_atom_object_query(obj, q);

	const char *path_str = path ? LV2_ATOM_BODY_CONST(path) : NULL;
	const char *fmt_str = fmt ? LV2_ATOM_BODY_CONST(fmt) : NULL;

	cb(timestamp, path_str, fmt_str, body);
}

static inline void
osc_atom_bundle_unpack(osc_forge_t *oforge, const LV2_Atom_Object *obj,
	osc_message_cb_t cb)
{
	const LV2_Atom_Long* timestamp = NULL;
	const LV2_Atom_Tuple* body = NULL;

	LV2_Atom_Object_Query q[] = {
		{ oforge->uris.timestamp, (const LV2_Atom **)&timestamp },
		{ oforge->uris.body, (const LV2_Atom **)&body },
		LV2_ATOM_OBJECT_QUERY_END
	};
	
	lv2_atom_object_query(obj, q);

	uint64_t timestamp_body = timestamp ? timestamp->body : 1ULL;

	if(!body)
		return;

	// iterate over tuple body
	for(const LV2_Atom *itr = lv2_atom_tuple_begin(body);
		!lv2_atom_tuple_is_end(LV2_ATOM_BODY(body), body->atom.size, itr);
		itr = lv2_atom_tuple_next(itr))
	{
		osc_atom_unpack(oforge, (const LV2_Atom_Object *)itr, cb);
	}
}

static inline void
osc_atom_unpack(osc_forge_t *oforge, const LV2_Atom_Object *obj,
	osc_message_cb_t cb)
{
	if(osc_atom_is_bundle(oforge, obj))
		osc_atom_bundle_unpack(oforge, obj, cb);
	else if(osc_atom_is_message(oforge, obj))
		osc_atom_message_unpack(oforge, 1ULL, obj, cb);
}

static inline void
osc_forge_bundle_push(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame *obj_frame, LV2_Atom_Forge_Frame *tup_frame,
	uint64_t timestamp)
{
	lv2_atom_forge_object(forge, obj_frame, 0, oforge->uris.bundle);
	{
		lv2_atom_forge_key(forge, oforge->uris.timestamp);
		lv2_atom_forge_long(forge, timestamp);

		lv2_atom_forge_key(forge, oforge->uris.body);
		lv2_atom_forge_tuple(forge, tup_frame);
	}
}

static inline void
osc_forge_bundle_pop(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame *obj_frame, LV2_Atom_Forge_Frame *tup_frame)
{
	lv2_atom_forge_pop(forge, tup_frame);
	lv2_atom_forge_pop(forge, obj_frame);
}

static inline void
osc_forge_message_push(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame *obj_frame, LV2_Atom_Forge_Frame *tup_frame,
	const char *path, const char *fmt)
{
	lv2_atom_forge_object(forge, obj_frame, 0, oforge->uris.message);
	{
		lv2_atom_forge_key(forge, oforge->uris.path);
		lv2_atom_forge_string(forge, path, strlen(path));

		lv2_atom_forge_key(forge, oforge->uris.format);
		lv2_atom_forge_string(forge, fmt, strlen(fmt));

		lv2_atom_forge_key(forge, oforge->uris.body);
		lv2_atom_forge_tuple(forge, tup_frame);
	}
}

static inline void
osc_forge_message_pop(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame *obj_frame, LV2_Atom_Forge_Frame *tup_frame)
{
	lv2_atom_forge_pop(forge, tup_frame);
	lv2_atom_forge_pop(forge, obj_frame);
}
	
static inline void
osc_forge_int32(osc_forge_t *oforge, LV2_Atom_Forge *forge, int32_t i)
{
	lv2_atom_forge_int(forge, i);
}

static inline void
osc_forge_float(osc_forge_t *oforge, LV2_Atom_Forge *forge, float f)
{
	lv2_atom_forge_float(forge, f);
}

static inline void
osc_forge_string(osc_forge_t *oforge, LV2_Atom_Forge *forge, const char *s)
{
	lv2_atom_forge_string(forge, s, strlen(s));
}

static inline void
osc_forge_symbol(osc_forge_t *oforge, LV2_Atom_Forge *forge, const char *s)
{
	lv2_atom_forge_string(forge, s, strlen(s));
}

static inline void
osc_forge_blob(osc_forge_t *oforge, LV2_Atom_Forge *forge, int32_t size, const uint8_t *b)
{
	lv2_atom_forge_atom(forge, size, forge->Chunk);
	lv2_atom_forge_raw(forge, b, size);
	lv2_atom_forge_pad(forge, size);
}

static inline void
osc_forge_int64(osc_forge_t *oforge, LV2_Atom_Forge *forge, int64_t h)
{
	lv2_atom_forge_long(forge, h);
}

static inline void
osc_forge_double(osc_forge_t *oforge, LV2_Atom_Forge *forge, double d)
{
	lv2_atom_forge_double(forge, d);
}

static inline void
osc_forge_timestamp(osc_forge_t *oforge, LV2_Atom_Forge *forge, uint64_t t)
{
	lv2_atom_forge_long(forge, t);
}

static inline void
osc_forge_char(osc_forge_t *oforge, LV2_Atom_Forge *forge, char c)
{
	lv2_atom_forge_int(forge, c);
}

static inline void
osc_forge_midi(osc_forge_t *oforge, LV2_Atom_Forge *forge, const uint8_t *m)
{
	lv2_atom_forge_atom(forge, 4, oforge->uris.midi);
	lv2_atom_forge_raw(forge, m, 4);
	lv2_atom_forge_pad(forge, 4);
}

static inline void
osc_forge_true(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	lv2_atom_forge_bool(forge, 1);
}

static inline void
osc_forge_false(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	lv2_atom_forge_bool(forge, 0);
}

static inline void
osc_forge_nil(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	lv2_atom_forge_atom(forge, 0, 0);
}

static inline void
osc_forge_bang(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	lv2_atom_forge_float(forge, INFINITY);
}

static inline void
osc_forge_message_vararg(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	const char *path, const char *fmt, ...)
{
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Frame tup_frame;

	va_list args;
	va_start(args, fmt);

	osc_forge_message_push(oforge, forge, &obj_frame, &tup_frame, path, fmt);

	for(const char *type = fmt; *type; type++)
	{
		switch(*type)
		{
			case 'i':
				osc_forge_int32(oforge, forge, va_arg(args, int32_t));
				break;
			case 'f':
				osc_forge_float(oforge, forge, (float)va_arg(args, double));
				break;
			case 's':
				osc_forge_string(oforge, forge, va_arg(args, const char *));
				break;
			case 'S':
				osc_forge_symbol(oforge, forge, va_arg(args, const char *));
				break;
			case 'b':
			{
				int32_t size = va_arg(args, int32_t);
				const uint8_t *b = va_arg(args, const uint8_t *);
				osc_forge_blob(oforge, forge, size, b);
				break;
			}
			
			case 'h':
				osc_forge_int64(oforge, forge, va_arg(args, int64_t));
				break;
			case 'd':
				osc_forge_double(oforge, forge, va_arg(args, double));
				break;
			case 't':
				osc_forge_timestamp(oforge, forge, va_arg(args, uint64_t));
				break;
			
			case 'c':
				osc_forge_char(oforge, forge, (char)va_arg(args, unsigned int));
				break;
			case 'm':
				osc_forge_midi(oforge, forge, va_arg(args, const uint8_t *));
				break;
			
			case 'T':
				osc_forge_true(oforge, forge);
				break;
			case 'F':
				osc_forge_false(oforge, forge);
				break;
			case 'N':
				osc_forge_nil(oforge, forge);
				break;
			case 'I':
				osc_forge_bang(oforge, forge);
				break;

			default:
				break;
		}
	}

	osc_forge_message_pop(oforge, forge, &obj_frame, &tup_frame);

	va_end(args);
}

#endif // _OSC_LV2_H
