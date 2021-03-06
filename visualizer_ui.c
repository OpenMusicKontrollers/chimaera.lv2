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

#include <lv2_eo_ui.h>

typedef struct _UI UI;
typedef struct _ref_t ref_t;

struct _ref_t {
	chimaera_event_t cev;
	Evas_Object *obj;
};

struct _UI {
	eo_ui_t eoui;

	LV2_URID_Map *map;
	struct {
		LV2_URID event_transfer;
	} uris;
	chimaera_forge_t cforge;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	LV2UI_Port_Map *port_map;
	uint32_t sensor_port;
	uint32_t notify_port;

	uint32_t sensors;
	int32_t values [160];

	chimaera_dict_t dict [CHIMAERA_DICT_SIZE];
	ref_t ref [CHIMAERA_DICT_SIZE];

	char theme_path[512];

	volatile int dump_needs_update;
	volatile int event_needs_update;

	Evas_Object *tab;
};

static void
_dump_fill(UI *ui)
{
	elm_table_clear(ui->tab, EINA_TRUE);

	for(unsigned i=0; i<ui->sensors; i++)
	{
		Evas_Object *edj;

		Evas_Object *north = elm_layout_add(ui->tab);
		elm_layout_file_set(north, ui->theme_path,
			CHIMAERA_VISUALIZER_UI_URI"/sensor");
		evas_object_size_hint_weight_set(north, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(north, EVAS_HINT_FILL, EVAS_HINT_FILL);
		edj = elm_layout_edje_get(north);
		edje_object_part_drag_value_set(edj, "sensor", 0.5, 1.f);
		edje_object_part_drag_size_set(edj, "sensor", 1.f, 0.f);
		evas_object_show(north);
		elm_table_pack(ui->tab, north, i, 0, 1, 1);

		Evas_Object *south = elm_layout_add(ui->tab);
		elm_layout_file_set(south, ui->theme_path,
			CHIMAERA_VISUALIZER_UI_URI"/sensor");
		evas_object_size_hint_weight_set(south, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(south, EVAS_HINT_FILL, EVAS_HINT_FILL);
		edj = elm_layout_edje_get(south);
		edje_object_part_drag_value_set(edj, "sensor", 0.5, 0.f);
		edje_object_part_drag_size_set(edj, "sensor", 1.f, 0.f);
		evas_object_show(south);
		elm_table_pack(ui->tab, south, i, 1, 1, 1);

		ui->values[i] = 0;
	}

	ui->dump_needs_update = 1;
}

static void
_dump_update(UI *ui)
{
	int32_t *values = ui->values;

	for(unsigned i=0; i<ui->sensors; i++)
	{
		Evas_Object *north = elm_table_child_get(ui->tab, i, 0);
		Evas_Object *south = elm_table_child_get(ui->tab, i, 1);
		double rel = values[i];
		rel *= 1.f / 0x7ff;
		double north_size = rel < 0.f ? -rel : 0.f;
		double south_size = rel < 0.f ? 0.f : rel;
		uint8_t red = rel < 0.f ? (-rel)*0xbb : rel*0xbb;

		//printf("%i %i %lf %lf %lf\n", i, values[i], rel, north_size, south_size);

		evas_object_color_set(north, 0xbb, 0xbb-red, 0xbb-red, 0xff);
		evas_object_color_set(south, 0xbb-red, 0xbb, 0xbb-red, 0xff);

		Evas_Object *edj;
		edj = elm_layout_edje_get(north);
		edje_object_part_drag_size_set(edj, "sensor", 1.f, north_size);
		edj = elm_layout_edje_get(south);
		edje_object_part_drag_size_set(edj, "sensor", 1.f, south_size);
	}
}

static void
_event_update(UI *ui)
{
	int x, y, w, h;
	evas_object_geometry_get(ui->tab, &x, &y, &w, &h);

	uint32_t sid;
	ref_t *ref;
	CHIMAERA_DICT_FOREACH(ui->dict, sid, ref)
	{
		int sign = ref->cev.pid == 0x100 ? 1 : -1;
		int abs_x = x + w * ref->cev.x - 12;
		int abs_y = y + h/2 + (h/8 + 3*h/8 * ref->cev.z) * sign  - 12;

		evas_object_move(ref->obj, abs_x, abs_y);
	}
}

static Evas_Object *
_content_get(eo_ui_t *eoui)
{
	UI *ui = (void *)eoui - offsetof(UI, eoui);

	ui->tab = elm_table_add(eoui->win);
	elm_table_homogeneous_set(ui->tab, EINA_TRUE);
	elm_table_align_set(ui->tab, 0.5, 0.5);

	_dump_fill(ui);

	// create indicators
	for(int i=0; i<CHIMAERA_DICT_SIZE; i++)
	{
		ref_t *ref = &ui->ref[i];

		ref->obj = elm_layout_add(ui->tab);
		elm_layout_file_set(ref->obj, ui->theme_path,
			CHIMAERA_VISUALIZER_UI_URI"/indicator");
		evas_object_resize(ref->obj, 24, 24);
	}

	return ui->tab;
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor,
	const char *plugin_uri, const char *bundle_path,
	LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(strcmp(plugin_uri, CHIMAERA_VISUALIZER_URI))
		return NULL;

	eo_ui_driver_t driv;
	if(descriptor == &visualizer_eo)
		driv = EO_UI_DRIVER_EO;
	else if(descriptor == &visualizer_ui)
		driv = EO_UI_DRIVER_UI;
	else if(descriptor == &visualizer_x11)
		driv = EO_UI_DRIVER_X11;
	else if(descriptor == &visualizer_kx)
		driv = EO_UI_DRIVER_KX;
	else
		return NULL;

	UI *ui = calloc(1, sizeof(UI));
	if(!ui)
		return NULL;

	eo_ui_t *eoui = &ui->eoui;
	eoui->driver = driv;
	eoui->content_get = _content_get;
	eoui->w = 1280,
	eoui->h = 720;

	ui->sensors = 160;
	ui->write_function = write_function;
	ui->controller = controller;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			ui->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__portMap))
			ui->port_map = (LV2UI_Port_Map *)features[i]->data;
  }

	if(!ui->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(ui);
		return NULL;
	}
	if(!ui->port_map)
	{
		fprintf(stderr, "%s: Host does not support ui:portMap\n", descriptor->URI);
		free(ui);
		return NULL;
	}

	// query port index of "control" port
	ui->sensor_port = ui->port_map->port_index(ui->port_map->handle, "sensors");
	ui->notify_port = ui->port_map->port_index(ui->port_map->handle, "notify");

	ui->uris.event_transfer = ui->map->map(ui->map->handle, LV2_ATOM__eventTransfer);
	chimaera_forge_init(&ui->cforge, ui->map);
	CHIMAERA_DICT_INIT(ui->dict, ui->ref);

	sprintf(ui->theme_path, "%s/chimaera_ui.edj", bundle_path);
	if(eoui_instantiate(eoui, descriptor, plugin_uri, bundle_path, write_function,
		controller, widget, features))
	{
		free(ui);
		return NULL;
	}

	return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
	UI *ui = handle;

	eoui_cleanup(&ui->eoui);
	free(ui);
}

static void
port_event(LV2UI_Handle handle, uint32_t i, uint32_t size, uint32_t urid,
	const void *buf)
{
	UI *ui = handle;

	if(i == ui->sensor_port)
	{
		uint32_t sensors = *(float *)buf;

		if(sensors != ui->sensors)
		{
			ui->sensors = sensors;
			_dump_fill(ui);
		}
	}
	else if( (i == ui->notify_port) && (urid == ui->uris.event_transfer) )
	{
		const LV2_Atom *atom = buf;

		if(chimaera_dump_check_type(&ui->cforge, atom))
		{
			uint32_t sensors;
			const int32_t *values = chimaera_dump_deforge(&ui->cforge, atom, &sensors);

			for(unsigned j=0; j<ui->sensors; j++)
				ui->values[j] = values[j];

			_dump_update(ui);
		}
		else if(chimaera_event_check_type(&ui->cforge, atom))
		{
			chimaera_event_t cev;

			chimaera_event_deforge(&ui->cforge, buf, &cev);

			switch(cev.state)
			{
				case CHIMAERA_STATE_ON:
				{
					ref_t *ref = chimaera_dict_add(ui->dict, cev.sid);
					if(!ref)
						break;

					// clone event data
					ref->cev.state = cev.state;
					ref->cev.sid = cev.sid;
					ref->cev.gid = cev.gid;
					ref->cev.pid = cev.pid;
					ref->cev.x = cev.x;
					ref->cev.z = cev.z;
					ref->cev.X = cev.X;
					ref->cev.Z = cev.Z;

					char buf2 [64];
					sprintf(buf2, "%i/%i", ref->cev.sid, ref->cev.gid);
					elm_object_part_text_set(ref->obj, "elm.text", buf2);
					evas_object_show(ref->obj);

					break;
				}
				case CHIMAERA_STATE_OFF:
				{
					ref_t *ref = chimaera_dict_ref(ui->dict, cev.sid);
					if(!ref)
						break;

					evas_object_hide(ref->obj);
					chimaera_dict_del(ui->dict, cev.sid);

					break;
				}
				case CHIMAERA_STATE_SET:
				{
					ref_t *ref = chimaera_dict_ref(ui->dict, cev.sid);
					if(!ref)
						break;

					// clone event data
					ref->cev.state = cev.state;
					ref->cev.sid = cev.sid;
					ref->cev.gid = cev.gid;
					ref->cev.pid = cev.pid;
					ref->cev.x = cev.x;
					ref->cev.z = cev.z;
					ref->cev.X = cev.X;
					ref->cev.Z = cev.Z;

					break;
				}
				case CHIMAERA_STATE_IDLE:
				{
					uint32_t sid;
					ref_t *ref;
					CHIMAERA_DICT_FOREACH(ui->dict, sid, ref)
						evas_object_hide(ref->obj);
					chimaera_dict_clear(ui->dict);

					break;
				}
			}

			_event_update(ui);
		}
	}
}

const LV2UI_Descriptor visualizer_eo = {
	.URI						= CHIMAERA_VISUALIZER_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_eo_extension_data
};

const LV2UI_Descriptor visualizer_ui = {
	.URI						= CHIMAERA_VISUALIZER_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_ui_extension_data
};

const LV2UI_Descriptor visualizer_x11 = {
	.URI						= CHIMAERA_VISUALIZER_X11_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_x11_extension_data
};

const LV2UI_Descriptor visualizer_kx = {
	.URI						= CHIMAERA_VISUALIZER_KX_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_kx_extension_data
};
