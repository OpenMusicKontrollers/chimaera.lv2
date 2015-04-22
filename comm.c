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
#include <string.h>

#include <uv.h>

#include <chimaera.h>
#include <osc.h>
#include <osc_stream.h>
#include <lv2_osc.h>
#include <cJSON.h>
#include <inlist.h>

#define BUF_SIZE 2048

typedef struct _handle_t handle_t;
typedef struct _intro_job_t intro_job_t;

struct _intro_job_t {
	INLIST;

	char path [512];
	cJSON *parent;
};

struct _handle_t {
	LV2_URID_Map *map;
	osc_forge_t oforge;
	chimaera_forge_t cforge;

	volatile int working;
	LV2_Worker_Schedule *sched;

	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;
	LV2_Atom_Sequence *osc_out;
	
	LV2_Atom_Forge event_forge;
	LV2_Atom_Forge notify_forge;

	// non-rt
	LV2_Worker_Respond_Function respond;
	LV2_Worker_Respond_Handle target;

	uv_loop_t *loop;

	osc_stream_t data_stream;
	uint8_t data_buf [BUF_SIZE];
	LV2_Atom_Forge data_forge;
	volatile int data_resolved;
	volatile int data_connected;

	osc_stream_t comm_stream;
	uint8_t comm_buf [BUF_SIZE];
	LV2_Atom_Forge comm_forge;
	volatile int comm_resolved;
	volatile int comm_connected;

	cJSON *root;
	Inlist *jobs;
};

static uint32_t cnt = 0;

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	handle_t *handle = (handle_t *)instance;

	handle->respond = respond;
	handle->target = target;

	uv_run(handle->loop, UV_RUN_NOWAIT);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	handle_t *handle = (handle_t *)instance;

	LV2_Atom_Forge * forge = &handle->data_forge;
	lv2_atom_forge_frame_time(forge, 0);
	lv2_atom_forge_atom(forge, size, handle->oforge.uris.event);
	lv2_atom_forge_raw(forge, body, size);
	lv2_atom_forge_pad(forge, size);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	handle->working = 0;

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_ext = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

static inline void
_atomize(osc_forge_t *oforge, LV2_Atom_Forge *forge, osc_data_t *buf, size_t size)
{
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Frame tup_frame;

	osc_data_t *ptr = buf;

	if(buf[0] != '/')
		return;

	lv2_atom_forge_object(forge, &obj_frame, 0, oforge->uris.message);
	{
		const char *path;
		ptr = osc_get_path(ptr, &path);
		lv2_atom_forge_key(forge, oforge->uris.path);
		lv2_atom_forge_string(forge, path, strlen(path));
	
		const char *fmt;
		ptr = osc_get_fmt(ptr, &fmt);
		lv2_atom_forge_key(forge, oforge->uris.format);
		lv2_atom_forge_string(forge, fmt, strlen(fmt));

		lv2_atom_forge_key(forge, oforge->uris.body);
		lv2_atom_forge_tuple(forge, &tup_frame);

		for(const char *type = fmt; *type; type++)
		{
			switch(*type)
			{
				case 'i':
				{
					int32_t i;
					ptr = osc_get_int32(ptr, &i);
					lv2_atom_forge_int(forge, i);
					break;
				}
				case 'f':
				{
					float f;
					ptr = osc_get_float(ptr, &f);
					lv2_atom_forge_float(forge, f);
					break;
				}
				case 's':
				case 'S':
				{
					const char *s;
					ptr = osc_get_string(ptr, &s);
					lv2_atom_forge_string(forge, s, strlen(s));
					break;
				}
				case 'b':
				{
					osc_blob_t b;
					ptr = osc_get_blob(ptr, &b);
					lv2_atom_forge_atom(forge, b.size, forge->Chunk);
					lv2_atom_forge_raw(forge, b.payload, b.size);
					lv2_atom_forge_pad(forge, b.size);
					break;
				}
				
				case 'h':
				{
					int64_t h;
					ptr = osc_get_int64(ptr, &h);
					lv2_atom_forge_long(forge, h);
					break;
				}
				case 'd':
				{
					double d;
					ptr = osc_get_double(ptr, &d);
					lv2_atom_forge_double(forge, d);
					break;
				}
				case 't':
				{
					osc_time_t t;
					ptr = osc_get_timetag(ptr, &t);
					lv2_atom_forge_long(forge, t);
					break;
				}
				
				case 'c':
				{
					char c;
					ptr = osc_get_char(ptr, &c);
					lv2_atom_forge_int(forge, c);
					break;
				}
				case 'm':
				{
					uint8_t *m;
					ptr = osc_get_midi(ptr, &m);
					lv2_atom_forge_atom(forge, 4, forge->Chunk);
					lv2_atom_forge_raw(forge, m, 4);
					lv2_atom_forge_pad(forge, 4);
					break;
				}
				
				case 'T':
				{
					lv2_atom_forge_bool(forge, 1);
					break;
				}
				case 'F':
				{
					lv2_atom_forge_bool(forge, 0);
					break;
				}
				case 'N':
				{
					lv2_atom_forge_atom(forge, 0, 0);
					break;
				}
				case 'I':
				{
					lv2_atom_forge_float(forge, INFINITY);
					break;
				}

				default:
					break;
			}
		}
		lv2_atom_forge_pop(forge, &tup_frame);
	}
	lv2_atom_forge_pop(forge, &obj_frame);
}

static inline osc_data_t *
_deatomize(osc_forge_t *oforge, const LV2_Atom *atom, osc_data_t *ptr, osc_data_t *end)
{
	LV2_Atom_Forge *forge = &oforge->forge;

	if(atom->type != forge->Object)
		return ptr;

	const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;

	if(obj->body.otype != oforge->uris.message)
		return ptr;
		
	const LV2_Atom_String *path = NULL;
	const LV2_Atom_String *fmt = NULL;
	const LV2_Atom_Tuple *tup = NULL;
	LV2_Atom_Object_Query q[] = {
		{ oforge->uris.path, (const LV2_Atom **)&path },
		{ oforge->uris.format, (const LV2_Atom **)&fmt },
		{ oforge->uris.body, (const LV2_Atom **)&tup },
		LV2_ATOM_OBJECT_QUERY_END
	};
	lv2_atom_object_query(obj, q);

	ptr = osc_set_path(ptr, end, LV2_ATOM_BODY_CONST(path));
	ptr = osc_set_fmt(ptr, end, LV2_ATOM_BODY_CONST(fmt));

	// iterate over arguments
	LV2_Atom* iter = lv2_atom_tuple_begin(tup);
	for(const char *type = LV2_ATOM_BODY_CONST(fmt);
		*type; type++, iter = lv2_atom_tuple_next(iter))
	{
		switch(*type)
		{
			case 'i':
			{
				assert(iter->type == forge->Int);
				ptr = osc_set_int32(ptr, end, ((LV2_Atom_Int *)iter)->body);
				break;
			}
			case 'f':
			{
				assert(iter->type == forge->Float);
				ptr = osc_set_float(ptr, end, ((LV2_Atom_Float *)iter)->body);
				break;
			}
			case 's':
			case 'S':
			{
				assert(iter->type == forge->String);
				ptr = osc_set_string(ptr, end, LV2_ATOM_BODY_CONST(iter));
				break;
			}
			case 'b':
			{
				assert(iter->type == forge->Chunk);
				ptr = osc_set_blob(ptr, end, iter->size, LV2_ATOM_BODY(iter));
				break;
			}
			
			case 'h':
			{
				assert(iter->type == forge->Long);
				ptr = osc_set_int64(ptr, end, ((LV2_Atom_Long *)iter)->body);
				break;
			}
			case 'd':
			{
				assert(iter->type == forge->Double);
				ptr = osc_set_double(ptr, end, ((LV2_Atom_Double *)iter)->body);
				break;
			}
			case 't':
			{
				assert(iter->type == forge->Long);
				ptr = osc_set_timetag(ptr, end, ((LV2_Atom_Long *)iter)->body);
				break;
			}
			
			case 'c':
			{
				assert(iter->type == forge->Int);
				ptr = osc_set_char(ptr, end, ((LV2_Atom_Int *)iter)->body);
				break;
			}
			case 'm':
			{
				assert(iter->type == forge->Chunk);
				ptr = osc_set_midi(ptr, end, LV2_ATOM_BODY(iter));
				break;
			}
			
			case 'T':
			case 'F':
				assert(iter->type == forge->Bool);
				break;
			case 'N':
				assert(iter->type == 0);
				break;
			case 'I':
				assert(iter->type == forge->Float); //TODO check for infinity value
				break;

			default:
				break;
		}
	}

	return ptr;
}

static void
_data_recv_cb(osc_stream_t *stream, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	osc_data_t *ptr = buf;
	const char *path;
	ptr = osc_get_path(ptr, &path);

	if(!strcmp(path, "/stream/resolve"))
		handle->data_resolved = 1;
	else if(!strcmp(path, "/stream/connect"))
		handle->data_connected = 1;
	else if(!strcmp(path, "/stream/disconnect"))
		handle->data_connected = 0;
	else if(handle->respond && handle->target)
		handle->respond(handle->target, size, buf);
}

static void
_data_send_cb(osc_stream_t *stream, size_t size, void *data)
{
	//TODO
}

static void
_comm_job_queue(handle_t *handle)
{
	if(inlist_count(handle->jobs)) // are there jobs to process
	{
		intro_job_t *job = INLIST_CONTAINER_GET(handle->jobs, intro_job_t);

		osc_data_t buf [1024];
		osc_data_t *ptr = buf;
		osc_data_t *end = buf + 1024;

		ptr = osc_set_path(ptr, end, job->path);
		ptr = osc_set_fmt(ptr, end, "i");
		ptr = osc_set_int32(ptr, end, 0); //TODO

		size_t size = ptr ? ptr - buf : 0;
		if(size)
			osc_stream_send(&handle->comm_stream, buf, size);
	}
	else // introspection done
	{
		char *pretty = cJSON_Print(handle->root);
		printf("%s\n", pretty);
		free(pretty);

		uint8_t tmp [1024];
		lv2_atom_forge_set_buffer(&handle->cforge.forge, tmp, 1024);
		//osc_forge_message_vararg(&handle->oforge, "/hello", "s", "world");
	}
}

static void
_comm_introspect(handle_t *handle, const char *target, const char *json)
{
	intro_job_t *job = NULL;
	intro_job_t *itr = NULL;
	INLIST_FOREACH(handle->jobs, itr)
	{
		if(!strcmp(target, itr->path))
		{
			job = itr;
			break; // job found
		}
	}
	if(!job)
		return;

	// parse JSON string
	cJSON *elmnt = cJSON_Parse(json);
	const cJSON *path = cJSON_GetObjectItem(elmnt, "path");
	const cJSON *type = cJSON_GetObjectItem(elmnt, "type");

	if(!job->parent) // is root?
		handle->root = elmnt;
	else
		cJSON_AddItemToArray(job->parent, elmnt);

	if(!strcmp(type->valuestring, "node"))
	{
		//printf("is node\n");

		cJSON *items = cJSON_DetachItemFromObject(elmnt, "items");
		cJSON *new_items = cJSON_CreateArray();
		cJSON_AddItemToObject(elmnt, "items", new_items);
		for(cJSON *itm = items->child; itm; itm = itm->next)
		{
			// add new job to job queue
			intro_job_t *new_job = calloc(1, sizeof(intro_job_t));
			sprintf(new_job->path, "%s%s!", path->valuestring, itm->valuestring);
			new_job->parent = new_items;
			handle->jobs = inlist_append(handle->jobs, INLIST_GET(new_job));
		}
		cJSON_Delete(items);
	}
	else if(!strcmp(type->valuestring, "method"))
	{
		//printf("is method\n");
	}

	// remove job from job queue
	handle->jobs = inlist_remove(handle->jobs, INLIST_GET(job));
	free(job);

	// run job queue
	_comm_job_queue(handle);
}

static int
_comm_stream_resolve(osc_time_t time, const char *path, const char *fmt,
	osc_data_t *arg, size_t size, void *data)
{
	handle_t *handle = data;

	handle->comm_resolved = 1;

	if(!handle->root)
	{
		// add new job to job queue
		intro_job_t *job = calloc(1, sizeof(intro_job_t));
		strcpy(job->path, "/!");
		job->parent = NULL;
		handle->jobs = inlist_append(handle->jobs, INLIST_GET(job));

		// run job queue
		_comm_job_queue(handle);
	}

	return 1;
}

static int
_comm_stream_connect(osc_time_t time, const char *path, const char *fmt,
	osc_data_t *arg, size_t size, void *data)
{
	handle_t *handle = data;
		
	handle->comm_connected = 1;

	return 1;
}

static int
_comm_stream_disconnect(osc_time_t time, const char *path, const char *fmt,
	osc_data_t *arg, size_t size, void *data)
{
	handle_t *handle = data;
	
	handle->comm_connected = 0;

	return 1;
}

static int
_comm_success(osc_time_t time, const char *path, const char *fmt,
	osc_data_t *arg, size_t size, void *data)
{
	handle_t *handle = data;

	osc_data_t *ptr = arg;
	int32_t uuid;
	const char *target;
	ptr = osc_get_int32(ptr, &uuid);
	ptr = osc_get_string(ptr, &target);
	
	if(strchr(target, '\0')[-1]  == '!') // is introspection response
	{
		const char *json;
		ptr = osc_get_string(ptr, &json);

		_comm_introspect(handle, target, json);
	}
	else // is query response
	{
		printf("success: %i %s\n", uuid, target);
		//TODO
	}

	return 1;
}

static int
_comm_fail(osc_time_t time, const char *path, const char *fmt,
	osc_data_t *arg, size_t size, void *data)
{
	handle_t *handle = data;
	
	osc_data_t *ptr = arg;
	int32_t uuid;
	const char *target;
	const char *error;
	ptr = osc_get_int32(ptr, &uuid);
	ptr = osc_get_string(ptr, &target);
	ptr = osc_get_string(ptr, &error);

	printf("fail: %i %s '%s'\n", uuid, target, error);

	return 1;
}

static const osc_method_t comm_methods [] = {
	{"/stream/resolve", "", _comm_stream_resolve},
	{"/stream/connect", "", _comm_stream_connect},
	{"/stream/disconnect", "", _comm_stream_disconnect},
	{"/success", NULL, _comm_success},
	{"/fail", "iss", _comm_fail},
	{NULL, NULL, NULL}
};

static void
_comm_recv_cb(osc_stream_t *stream, osc_data_t *buf, size_t size, void *data)
{
	handle_t *handle = data;

	osc_dispatch_method(0, buf, size, (osc_method_t *)comm_methods, NULL, NULL, handle);

	if(handle->respond && handle->target)
		handle->respond(handle->target, size, buf);
}

static void
_comm_send_cb(osc_stream_t *stream, size_t size, void *data)
{
	//TODO
}

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
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = (LV2_Worker_Schedule *)features[i]->data;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->sched)
	{
		fprintf(stderr, "%s: Host does not support worker:schedule\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	osc_forge_init(&handle->oforge, handle->map);
	chimaera_forge_init(&handle->cforge, handle->map);
	
	lv2_atom_forge_init(&handle->event_forge, handle->map);
	lv2_atom_forge_init(&handle->notify_forge, handle->map);

	lv2_atom_forge_init(&handle->data_forge, handle->map);
	lv2_atom_forge_init(&handle->comm_forge, handle->map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	handle_t *handle = (handle_t *)instance;

	switch(port)
	{
		case 0:
			handle->control = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->notify = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->osc_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;

	// reset forge buffer
	lv2_atom_forge_set_buffer(&handle->data_forge, handle->data_buf, BUF_SIZE);
	lv2_atom_forge_set_buffer(&handle->comm_forge, handle->comm_buf, BUF_SIZE);

	handle->data_resolved = 0;
	handle->data_connected = 0;

	handle->comm_resolved = 0;
	handle->comm_connected = 0;

	handle->root = NULL;

	handle->loop = uv_loop_new();
	
	osc_stream_init(handle->loop, &handle->comm_stream, "osc.udp4://chimaera.local:4444",
		_comm_recv_cb, _comm_send_cb, handle);
	osc_stream_init(handle->loop, &handle->data_stream, "osc.udp4://:3333",
		_data_recv_cb, _data_send_cb, handle);
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	handle_t *handle = (handle_t *)instance;

	const uint8_t trig [4] = {'t', 'r', 'g', '\0'};
		
	// prepare osc atom forge
	uint32_t capacity;
	LV2_Atom_Forge_Frame event_frame;
	LV2_Atom_Forge_Frame notify_frame;

	capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->event_forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	lv2_atom_forge_sequence_head(forge, &event_frame, 0);
	
	capacity = handle->notify->atom.size;
	LV2_Atom_Forge *notify = &handle->notify_forge;
	lv2_atom_forge_set_buffer(notify, (uint8_t *)handle->notify, capacity);
	lv2_atom_forge_sequence_head(notify, &notify_frame, 0);
		
	if(!handle->working)
	{

		if(handle->data_forge.offset > 0)
		{
			// copy forge buffer
			lv2_atom_forge_raw(forge, handle->data_buf, handle->data_forge.offset);

			// reset forge buffer
			lv2_atom_forge_set_buffer(&handle->data_forge, handle->data_buf, BUF_SIZE);
		}

		if(handle->data_forge.offset > 0)
		{
			// copy forge buffer
			lv2_atom_forge_raw(notify, handle->comm_buf, handle->comm_forge.offset);

			// reset forge buffer
			lv2_atom_forge_set_buffer(&handle->comm_forge, handle->comm_buf, BUF_SIZE);
		}
	
		// schedule new work
		if(handle->sched->schedule_work(handle->sched->handle,
			sizeof(trig), trig) == LV2_WORKER_SUCCESS)
		{
			handle->working = 1;
		}
	}

	// handle control
	LV2_ATOM_SEQUENCE_FOREACH(handle->control, ev)
	{
		const LV2_Atom *atom = &ev->body;

		//TODO
	}

	// end sequence
	lv2_atom_forge_pop(forge, &event_frame);
	lv2_atom_forge_pop(notify, &notify_frame);
}

static void
deactivate(LV2_Handle instance)
{
	handle_t *handle = (handle_t *)instance;
	
	osc_stream_deinit(&handle->data_stream);
	osc_stream_deinit(&handle->comm_stream);

	uv_loop_close(handle->loop);

	if(handle->root)
		cJSON_Delete(handle->root);
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
	if(!strcmp(uri, LV2_WORKER__interface))
		return &work_ext;
	else
		return NULL;
}

const LV2_Descriptor comm = {
	.URI						= CHIMAERA_COMM_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
