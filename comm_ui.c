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

#include <chimaera.h>

#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Edje.h>

#include <cJSON.h>

#include <lv2_eo_ui.h>
#include <lv2_osc.h>

#define BUF_SIZE 2048
#define MAX_CONCURRENT_JOBS 20

typedef struct _intro_job_t intro_job_t;
typedef struct _intro_arg_t intro_arg_t;
typedef struct _UI UI;

struct _intro_job_t {
	char path [512];
	cJSON *parent;
};

struct _intro_arg_t {
	cJSON *node;
	cJSON *parent;
};

struct _UI {
	eo_ui_t eoui;

	LV2_URID_Map *map;
	struct {
		LV2_URID event_transfer;
	} uris;
	osc_forge_t oforge;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	LV2UI_Port_Map *port_map;
	uint32_t control_port;
	uint32_t notify_port;

	Evas_Object *pane;
	Evas_Object *intro;
	int intro_complete;

	struct {
		Elm_Genlist_Item_Class *node;
		Elm_Genlist_Item_Class *meth;
		Elm_Genlist_Item_Class *arg;
	} itc;
	
	cJSON *root;
	int jobn;
	Eina_List *jobs;

	int32_t uid;
	union {
		uint8_t buf [BUF_SIZE];
		LV2_Atom atom;
	};
};

static void
_list_expand_request(void *data, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
	Elm_Object_Item *itm = event_info;

	elm_genlist_item_expanded_set(itm, EINA_TRUE);
}

static void
_list_contract_request(void *data, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
	Elm_Object_Item *itm = event_info;

	elm_genlist_item_expanded_set(itm, EINA_FALSE);
}

static void
_intro_expanded(void *data, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
	Elm_Object_Item *itm = event_info;
	cJSON *node = elm_object_item_data_get(itm);

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->itc.node)
	{
		const cJSON *items = cJSON_GetObjectItem(node, "items");

		for(cJSON *child = items->child; child; child = child->next)
		{
			const cJSON *type = cJSON_GetObjectItem(child, "type");

			if(!strcmp(type->valuestring, "node"))
			{
				Elm_Object_Item *elmnt = elm_genlist_item_append(ui->intro, ui->itc.node,
					child, itm, ELM_GENLIST_ITEM_TREE, NULL, NULL);
			}
			else
			{
				Elm_Object_Item *elmnt = elm_genlist_item_append(ui->intro, ui->itc.meth,
					child, itm, ELM_GENLIST_ITEM_TREE, NULL, NULL);
			}
		}
	}
	else if(class == ui->itc.meth)
	{
		const cJSON *args = cJSON_GetObjectItem(node, "arguments");

		for(cJSON *child = args->child; child; child = child->next)
		{
			intro_arg_t *intro_arg = malloc(sizeof(intro_arg_t));
			if(!intro_arg)
				continue;

			intro_arg->parent = node;
			intro_arg->node = child;

			Elm_Object_Item *elmnt = elm_genlist_item_append(ui->intro, ui->itc.arg,
				intro_arg, itm, ELM_GENLIST_ITEM_NONE, NULL, NULL);
			elm_genlist_item_select_mode_set(elmnt, ELM_OBJECT_SELECT_MODE_NONE);
		}
	}
}

static void
_intro_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;

	// clear items
	elm_genlist_item_subitems_clear(itm);
}

static char * 
_intro_node_label_get(void *data, Evas_Object *obj, const char *part)
{
	cJSON *node = data;

	if(!strcmp(part, "elm.text"))
	{
		cJSON *path = cJSON_GetObjectItem(node, "path");

		if(path)
			return strdup(path->valuestring);
	}
	else if(!strcmp(part, "elm.text.sub"))
	{
		cJSON *description = cJSON_GetObjectItem(node, "description");

		if(description)
			return strdup(description->valuestring);
	}
	
	return NULL;
}

static char * 
_intro_meth_label_get(void *data, Evas_Object *obj, const char *part)
{
	cJSON *node = data;

	if(!strcmp(part, "elm.text"))
	{
		cJSON *path = cJSON_GetObjectItem(node, "path");

		if(path)
			return strdup(path->valuestring);
	}
	
	return NULL;
}

static void
_intro_node_del(void *data, Evas_Object *obj)
{
	cJSON *node = data;
	UI *ui = evas_object_data_get(obj, "ui");

	if(ui->root && (node == ui->root) )
	{
		cJSON_Delete(ui->root);
		ui->root = NULL;
	}
}

const char *value_key = "_value"; //TODO move up

static void
_arg_changed(UI *ui, intro_arg_t *arg)
{
	const cJSON *path = cJSON_GetObjectItem(arg->parent, "path");
	cJSON *args = cJSON_GetObjectItem(arg->parent, "arguments");
	const int argn = cJSON_GetArraySize(args);
	char fmt [argn + 2];

	osc_forge_t *oforge = &ui->oforge;
	LV2_Atom_Forge *forge = &oforge->forge;
	lv2_atom_forge_set_buffer(forge, (void *)ui->buf, BUF_SIZE);

	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Frame tup_frame;

	// fill format
	char *ptr = fmt;
	*ptr++ = 'i';
	for(cJSON *node = args->child; node; node = node->next)
	{
		const cJSON *type = cJSON_GetObjectItem(node, "type");
		*ptr++ = type->valuestring[0];
	}
	*ptr = '\0';

	printf("_arg_changed: %s %s\n", path->valuestring, fmt);

	osc_forge_message_push(oforge, forge, &obj_frame, &tup_frame,
		path->valuestring, fmt);

	// uid
	osc_forge_int32(oforge, forge, ui->uid++);

	for(cJSON *node = args->child; node; node = node->next)
	{
		const cJSON *type = cJSON_GetObjectItem(node, "type");
		const cJSON *write = cJSON_GetObjectItem(node, "write");
		const cJSON *value = cJSON_GetObjectItem(node, value_key);

		if(!write->valueint)
			continue; // parameter not writable, skip it

		switch(type->valuestring[0])
		{
			case 'i':
				osc_forge_int32(oforge, forge, value->valueint);
				break;
			case 'f':
				osc_forge_float(oforge, forge, value->valuedouble);
				break;
			case 's':
				osc_forge_string(oforge, forge, value->valuestring);
				break;
		}
	}

	osc_forge_message_pop(oforge, forge, &obj_frame, &tup_frame);
	
	ui->write_function(ui->controller, ui->control_port, sizeof(LV2_Atom) + ui->atom.size,
		ui->uris.event_transfer, ui->buf);
}

static void
_check_changed(void *data, Evas_Object *obj, void *event)
{
	intro_arg_t *arg = data;
	UI *ui = evas_object_data_get(obj, "ui");

	int val = elm_check_state_get(obj);

	cJSON_DeleteItemFromObject(arg->node, value_key);
	cJSON_AddBoolToObject(arg->node, value_key, val);

	_arg_changed(ui, arg);
}

static void
_sldr_changed(void *data, Evas_Object *obj, void *event)
{
	intro_arg_t *arg = data;
	UI *ui = evas_object_data_get(obj, "ui");

	double val = elm_slider_value_get(obj);

	const cJSON *type = cJSON_GetObjectItem(arg->node, "type");
	if(type->valuestring[0] == 'i')
		val = floor(val); // convert to integer

	cJSON_DeleteItemFromObject(arg->node, value_key);
	cJSON_AddNumberToObject(arg->node, value_key, val);

	_arg_changed(ui, arg);
}

static void
_segm_changed(void *data, Evas_Object *obj, void *event)
{
	intro_arg_t *arg = data;
	UI *ui = evas_object_data_get(obj, "ui");

	const Elm_Object_Item *elmnt = elm_segment_control_item_selected_get(obj);
	const char *val = elm_object_item_part_text_get(elmnt, "default");
	
	const cJSON *type = cJSON_GetObjectItem(arg->node, "type");

	cJSON_DeleteItemFromObject(arg->node, value_key);
	switch(type->valuestring[0])
	{
		case 'i':
			cJSON_AddNumberToObject(arg->node, value_key, atoi(val));
			break;
		case 'f':
			cJSON_AddNumberToObject(arg->node, value_key, atof(val));
			break;
		case 's':
			cJSON_AddStringToObject(arg->node, value_key, val);
			break;
	}

	_arg_changed(ui, arg);
}

static void
_entry_changed(void *data, Evas_Object *obj, void *event)
{
	intro_arg_t *arg = data;
	UI *ui = evas_object_data_get(obj, "ui");

	const char *val = elm_entry_entry_get(obj);
	//TODO get utf8 vs. markup

	cJSON_DeleteItemFromObject(arg->node, value_key);
	cJSON_AddStringToObject(arg->node, value_key, val);

	_arg_changed(ui, arg);
}

static Evas_Object * 
_intro_arg_content_get(void *data, Evas_Object *obj, const char *part)
{
	intro_arg_t *arg = data;
	cJSON *parent = arg->parent;
	cJSON *node = arg->node;

	if(!strcmp(part, "elm.swallow.content"))
	{
		const cJSON *type = cJSON_GetObjectItem(node, "type");
		const cJSON *write = cJSON_GetObjectItem(node, "write");
		const cJSON *read = cJSON_GetObjectItem(node, "read");
		const cJSON *range = cJSON_GetObjectItem(node, "range");
		const cJSON *values = cJSON_GetObjectItem(node, "values");
		const cJSON *description = cJSON_GetObjectItem(node, "description");
		const cJSON *value = cJSON_GetObjectItem(node, value_key);

		Evas_Object *vbox = elm_grid_add(obj);
		evas_object_size_hint_weight_set(vbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(vbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(vbox);

		Evas_Object *readable = elm_check_add(vbox);
		elm_check_state_set(readable, read->valueint);
		elm_object_text_set(readable, "read");
		elm_object_disabled_set(readable, EINA_TRUE);
		evas_object_size_hint_align_set(readable, 0.f, EVAS_HINT_FILL);
		evas_object_show(readable);
		elm_grid_pack(vbox, readable, 0, 0, 10, 100);

		Evas_Object *writable = elm_check_add(vbox);
		elm_check_state_set(writable, write->valueint);
		elm_object_text_set(writable, "write");
		elm_object_disabled_set(writable, EINA_TRUE);
		evas_object_size_hint_align_set(writable, 0.f, EVAS_HINT_FILL);
		evas_object_show(writable);
		elm_grid_pack(vbox, writable, 10, 0, 10, 100);

		Evas_Object *label = elm_label_add(vbox);
		elm_object_text_set(label, description ? description->valuestring : NULL);
		evas_object_size_hint_weight_set(label, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(label, 1.f, EVAS_HINT_FILL);
		evas_object_show(label);
		elm_grid_pack(vbox, label, 20, 0, 30, 100);

		Evas_Object *grab = NULL;

		switch(type->valuestring[0])
		{
			case 'i':
			{
				if(range)
				{
					const cJSON *val = range->child;
					int min = val->valueint;
					val = val->next;
					int max = val->valueint;
					val = val->next;
					int step = val->valueint;

					if( (min == 0) && (max == 1) )
					{
						grab = elm_check_add(vbox);
						elm_check_state_set(grab, value->valueint);
						evas_object_smart_callback_add(grab, "changed", _check_changed, arg);
					}
					else
					{
						grab = elm_slider_add(vbox);
						elm_slider_min_max_set(grab, min, max);
						elm_slider_value_set(grab, value->valueint);
						elm_slider_step_set(grab, step);
						elm_slider_indicator_show_set(grab, EINA_FALSE);
						elm_slider_unit_format_set(grab, "%.0f");
						evas_object_smart_callback_add(grab, "changed", _sldr_changed, arg);
					}
				}
				else if(values)
				{
					grab = elm_segment_control_add(vbox);

					for(const cJSON *val = values->child; val; val = val->next)
					{
						char buf [32];
						sprintf(buf, "%i", val->valueint);
						Elm_Object_Item *elmnt = elm_segment_control_item_add(grab, NULL, buf);

						if(val->valueint == value->valueint)
							elm_segment_control_item_selected_set(elmnt, EINA_TRUE);
					}
					
					evas_object_smart_callback_add(grab, "changed", _segm_changed, arg);
				}

				break;
			}
			case 'f':
			{
				if(range)
				{
					const cJSON *val = range->child;
					float min = val->valuedouble;
					val = val->next;
					float max = val->valuedouble;
					val = val->next;
					float step = val->valuedouble;

					grab = elm_slider_add(vbox);
					elm_slider_min_max_set(grab, min, max);
					elm_slider_value_set(grab, value->valuedouble);
					elm_slider_step_set(grab, step);
					elm_slider_indicator_show_set(grab, EINA_FALSE);
					elm_slider_unit_format_set(grab, "%.4f");
					evas_object_smart_callback_add(grab, "changed", _sldr_changed, arg);
				}
				else if(values)
				{
					grab = elm_segment_control_add(vbox);

					for(const cJSON *val = values->child; val; val = val->next)
					{
						char buf [32];
						sprintf(buf, "%f", val->valuedouble);
						Elm_Object_Item *elmnt = elm_segment_control_item_add(grab, NULL, buf);

						if(val->valuedouble == value->valuedouble)
							elm_segment_control_item_selected_set(elmnt, EINA_TRUE);
					}
					
					evas_object_smart_callback_add(grab, "changed", _segm_changed, arg);
				}

				break;
			}
			case 's':
			{
				if(values)
				{
					grab = elm_segment_control_add(vbox);

					for(const cJSON *val = values->child; val; val = val->next)
					{
						const char *buf = val->valuestring;
						Elm_Object_Item *elmnt = elm_segment_control_item_add(grab, NULL, buf);

						if(!strcmp(val->valuestring, value->valuestring))
							elm_segment_control_item_selected_set(elmnt, EINA_TRUE);
					}

					evas_object_smart_callback_add(grab, "changed", _segm_changed, arg);
				}
				else
				{
					grab = elm_entry_add(vbox);
					elm_entry_entry_set(grab, value->valuestring);
					elm_entry_single_line_set(grab, EINA_TRUE);
					evas_object_smart_callback_add(grab, "changed", _entry_changed, arg);
				}

				break;
			}
		}

		if(grab)
		{
			elm_object_disabled_set(grab, write->valueint ? EINA_FALSE : EINA_TRUE);
			evas_object_data_set(grab, "ui", evas_object_data_get(obj, "ui"));
			evas_object_size_hint_weight_set(grab, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(grab, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(grab);
			elm_grid_pack(vbox, grab, 50, 0, 50, 100);
		}

		return vbox;
	}
	
	return NULL;
}

static void
_intro_arg_del(void *data, Evas_Object *obj)
{
	intro_arg_t *intro_arg = data;

	free(intro_arg);
}
	
static Evas_Object *
_content_get(eo_ui_t *eoui)
{
	UI *ui = (void *)eoui - offsetof(UI, eoui);

	ui->pane = elm_panes_add(eoui->win);
	elm_panes_horizontal_set(ui->pane, EINA_FALSE);
	elm_panes_content_left_size_set(ui->pane, 0.35);

	Evas_Object *left_frame = elm_frame_add(ui->pane);
	elm_object_text_set(left_frame, "Connection");
	evas_object_show(left_frame);
	elm_object_part_content_set(ui->pane, "left", left_frame);

	Evas_Object *right_frame = elm_frame_add(ui->pane);
	elm_object_text_set(right_frame, "Introspection");
	evas_object_show(right_frame);
	elm_object_part_content_set(ui->pane, "right", right_frame);


	ui->intro = elm_genlist_add(right_frame);
	//elm_genlist_homogeneous_set(ui->intro, EINA_TRUE);
	/*
	evas_object_smart_callback_add(ui->intro, "activated",
		_intro_activated, ui);
	*/
	evas_object_smart_callback_add(ui->intro, "expand,request",
		_list_expand_request, ui);
	evas_object_smart_callback_add(ui->intro, "contract,request",
		_list_contract_request, ui);
	evas_object_smart_callback_add(ui->intro, "expanded",
		_intro_expanded, ui);
	evas_object_smart_callback_add(ui->intro, "contracted",
		_intro_contracted, ui);
	evas_object_data_set(ui->intro, "ui", ui);
	evas_object_size_hint_weight_set(ui->intro, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->intro, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->intro);
	elm_object_content_set(right_frame, ui->intro);

	ui->itc.node = elm_genlist_item_class_new();
	ui->itc.node->item_style = "double_label";
	ui->itc.node->func.text_get = _intro_node_label_get;
	ui->itc.node->func.content_get = NULL;
	ui->itc.node->func.state_get = NULL;
	ui->itc.node->func.del = _intro_node_del;
	//FIXME elm_genlist_item_class_free();

	ui->itc.meth = elm_genlist_item_class_new();
	ui->itc.meth->item_style = "double_label";
	ui->itc.meth->func.text_get = _intro_meth_label_get;
	ui->itc.meth->func.content_get = NULL;
	ui->itc.meth->func.state_get = NULL;
	ui->itc.meth->func.del = NULL;
	//FIXME elm_genlist_item_class_free();

	ui->itc.arg = elm_genlist_item_class_new();
	ui->itc.arg->item_style = "full";
	ui->itc.arg->func.text_get = NULL;
	ui->itc.arg->func.content_get = _intro_arg_content_get;
	ui->itc.arg->func.state_get = NULL;
	ui->itc.arg->func.del = _intro_arg_del;
	//FIXME elm_genlist_item_class_free();

	return ui->pane;
}

static inline void
_comm_message_send(UI *ui, const char *path, const char *fmt, ...)
{
	LV2_Atom_Forge *forge = &ui->oforge.forge;
	lv2_atom_forge_set_buffer(forge, (void *)ui->buf, BUF_SIZE);

	va_list args;
	va_start(args, fmt);

	osc_forge_message_varlist(&ui->oforge, forge, path, fmt, args);

	va_end(args);

	ui->write_function(ui->controller, ui->control_port, sizeof(LV2_Atom) + ui->atom.size,
		ui->uris.event_transfer, ui->buf);
}

static void
_comm_job_queue(UI *ui)
{
	int n = eina_list_count(ui->jobs);

	ui->jobn = n < MAX_CONCURRENT_JOBS ? n : MAX_CONCURRENT_JOBS;

	if(ui->jobn)
	{
		for(int i=0; i< ui->jobn; i++) // are there jobs to process
		{
			intro_job_t *job = eina_list_nth(ui->jobs, i);

			// request next introspect item
			_comm_message_send(ui, job->path, "i", ui->uid++);
		}
	}
	else // introspection done
	{
		//char *pretty = cJSON_Print(ui->root);
		//printf("%s\n", pretty);
		//free(pretty);

		if(!ui->intro_complete)
		{
			Elm_Object_Item *elmnt = elm_genlist_item_append(ui->intro, ui->itc.node,
				ui->root, NULL, ELM_GENLIST_ITEM_TREE, NULL, NULL);
			elm_genlist_item_expanded_set(elmnt, EINA_TRUE);

			ui->intro_complete = 1;
		}
	}
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor,
	const char *plugin_uri, const char *bundle_path,
	LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(strcmp(plugin_uri, CHIMAERA_COMM_URI))
		return NULL;

	eo_ui_driver_t driver;
	if(descriptor == &comm_eo)
		driver = EO_UI_DRIVER_EO;
	else if(descriptor == &comm_ui)
		driver = EO_UI_DRIVER_UI;
	else if(descriptor == &comm_x11)
		driver = EO_UI_DRIVER_X11;
	else if(descriptor == &comm_kx)
		driver = EO_UI_DRIVER_KX;
	else
		return NULL;

	UI *ui = calloc(1, sizeof(UI));
	if(!ui)
		return NULL;

	eo_ui_t *eoui = &ui->eoui;
	eoui->driver = driver;
	eoui->content_get = _content_get;
	eoui->w = 1280,
	eoui->h = 720;

	ui->write_function = write_function;
	ui->controller = controller;
	
	int i, j;
	for(i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			ui->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__portMap))
			ui->port_map = (LV2UI_Port_Map *)features[i]->data;
  }
	if(!ui->port_map)
	{
		fprintf(stderr, "%s: Host does not support ui:portMap\n", descriptor->URI);
		free(ui);
		return NULL;
	}

	// query port index of "control" port
	ui->control_port = ui->port_map->port_index(ui->port_map->handle, "control");
	ui->notify_port = ui->port_map->port_index(ui->port_map->handle, "notify");

	if(!ui->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(ui);
		return NULL;
	}

	ui->uris.event_transfer = ui->map->map(ui->map->handle, LV2_ATOM__eventTransfer);
	osc_forge_init(&ui->oforge, ui->map);

	if(eoui_instantiate(eoui, descriptor, plugin_uri, bundle_path, write_function,
		controller, widget, features))
	{
		free(ui);
		return NULL;
	}

	ui->uid = random();

	// request introspection
	if(!ui->root)
	{
		// add new job to job queue
		intro_job_t *job = calloc(1, sizeof(intro_job_t));
		strcpy(job->path, "/!");
		job->parent = NULL;
		ui->jobs = eina_list_append(ui->jobs, job);

		// run job queue
		_comm_job_queue(ui);
	}

	return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
	UI *ui = handle;

	eoui_cleanup(&ui->eoui);
	elm_genlist_item_class_free(ui->itc.node);
	elm_genlist_item_class_free(ui->itc.meth);
	elm_genlist_item_class_free(ui->itc.arg);
	free(ui);
}

static intro_job_t *
_job_find(UI *ui, const char *target)
{
	intro_job_t *itr = NULL;
	Eina_List *l;
	EINA_LIST_FOREACH(ui->jobs, l, itr)
	{
		if(!strcmp(target, itr->path))
			return itr; // job found
	}

	return NULL; // job not found
}

static void
_comm_introspect(UI *ui, const char *target, const char *json)
{
	intro_job_t *job = _job_find(ui, target);
	if(!job)
		return;

	// parse JSON string
	cJSON *elmnt = cJSON_Parse(json);
	const cJSON *path = cJSON_GetObjectItem(elmnt, "path");
	const cJSON *type = cJSON_GetObjectItem(elmnt, "type");

	if(!job->parent) // is root?
		ui->root = elmnt;
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
			ui->jobs = eina_list_append(ui->jobs, new_job);
		}
		cJSON_Delete(items);
	}
	else if(!strcmp(type->valuestring, "method"))
	{
		//printf("is method\n");

		cJSON *args = cJSON_GetObjectItem(elmnt, "arguments");
		for(cJSON *arg= args->child; arg; arg = arg->next)
		{
			const cJSON *read = cJSON_GetObjectItem(arg, "read");
			if(read->valueint) // has at least one readable argument
			{
				// request current value for this methods arfuments
				intro_job_t *new_job = calloc(1, sizeof(intro_job_t));
				sprintf(new_job->path, "%s", path->valuestring); //TODO does this always work?
				new_job->parent = elmnt;
				ui->jobs = eina_list_append(ui->jobs, new_job);

				break;
			}
		}
	}

	// remove job from job queue
	ui->jobs = eina_list_remove(ui->jobs, job);
	ui->jobn -= 1;
	free(job);
}

static void
_comm_query(UI *ui, const char *target, const char *fmt, const LV2_Atom *itr)
{
	intro_job_t *job = _job_find(ui, target);
	if(!job)
		return;

	//printf("_comm_query: %s %s\n", target, fmt);

	cJSON *node = job->parent;
	const cJSON *args = cJSON_GetObjectItem(node, "arguments");
	for(cJSON *arg = args->child; arg; arg = arg->next)
	{
		const cJSON *type = cJSON_GetObjectItem(arg, "type");

		cJSON_DeleteItemFromObject(arg, value_key);

		itr = lv2_atom_tuple_next(itr);
		switch(type->valuestring[0]) // TODO check with fmt
		{
			case 'i':
				cJSON_AddNumberToObject(arg, value_key, ((LV2_Atom_Int *)itr)->body);
				break;
			case 'f':
				cJSON_AddNumberToObject(arg, value_key, ((LV2_Atom_Float *)itr)->body);
				break;
			case 's':
				cJSON_AddStringToObject(arg, value_key, LV2_ATOM_CONTENTS_CONST(LV2_Atom_String, itr));
				break;
		}
	}

	//TODO update GUI accordingly

	// remove job from job queue
	ui->jobs = eina_list_remove(ui->jobs, job);
	ui->jobn -= 1;
	free(job);
}

static void
_comm_recv(uint64_t timestamp, const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	UI *ui = data;

	if(!strcmp(path, "/success"))
	{
		const LV2_Atom *itr = lv2_atom_tuple_begin(body);
		int32_t uid = ((LV2_Atom_Int *)itr)->body;

		itr = lv2_atom_tuple_next(itr);
		const char *tar = LV2_ATOM_CONTENTS_CONST(LV2_Atom_String, itr);
	
		if(strchr(tar, '\0')[-1]  == '!') // is introspection response
		{
			itr = lv2_atom_tuple_next(itr);
			const char *json = LV2_ATOM_CONTENTS_CONST(LV2_Atom_String, itr);

			_comm_introspect(ui, tar, json);
		}
		else // is query response
		{
			//fprintf(stdout, "%s: %i '%s'\n", path, uid, tar);

			_comm_query(ui, tar, &fmt[2], itr);
		}
	}
	else if(!strcmp(path, "/fail"))
	{
		const LV2_Atom *itr = lv2_atom_tuple_begin(body);
		int32_t uid = ((LV2_Atom_Int *)itr)->body;

		itr = lv2_atom_tuple_next(itr);
		const char *tar = LV2_ATOM_CONTENTS_CONST(LV2_Atom_String, itr);
		
		itr = lv2_atom_tuple_next(itr);
		const char *err = LV2_ATOM_CONTENTS_CONST(LV2_Atom_String, itr);

		fprintf(stderr, "%s: %i '%s': %s\n", path, uid, tar, err);
	
		intro_job_t *job = _job_find(ui, tar);
		if(job)
		{
			// remove job from job queue
			ui->jobs = eina_list_remove(ui->jobs, job);
			ui->jobn -= 1;
			free(job);
		}
	}

	// run job queue
	if(ui->jobn == 0) // last batch processed?
		_comm_job_queue(ui);
}

static void
port_event(LV2UI_Handle handle, uint32_t i, uint32_t size, uint32_t urid,
	const void *buf)
{
	UI *ui = handle;

	if(i == ui->notify_port) // notify
	{
		osc_atom_unpack(&ui->oforge, buf, _comm_recv, ui);
	}
}

const LV2UI_Descriptor comm_eo = {
	.URI						= CHIMAERA_COMM_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_eo_extension_data
};

const LV2UI_Descriptor comm_ui = {
	.URI						= CHIMAERA_COMM_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_ui_extension_data
};

const LV2UI_Descriptor comm_x11 = {
	.URI						= CHIMAERA_COMM_X11_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_x11_extension_data
};

const LV2UI_Descriptor comm_kx = {
	.URI						= CHIMAERA_COMM_KX_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_kx_extension_data
};
