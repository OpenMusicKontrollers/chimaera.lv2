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

struct _UI {
	eo_ui_t eoui;

	LV2_URID_Map *map;
	struct {
		LV2_URID event_transfer;
	} uris;
	chimaera_forge_t cforge;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	chimaera_dump_t dump;

	char theme_path[512];

	volatile int dump_needs_update;
	volatile int event_needs_update;

	Evas_Object *tab;
};

static void
_dump_fill(UI *ui)
{
	elm_table_clear(ui->tab, EINA_TRUE);
	
	for(int i=0; i<ui->dump.sensors; i++)
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

		ui->dump.values[i] = 0;
	}

	ui->dump_needs_update = 1;
}

static void
_dump_update(UI *ui)
{
	int32_t *values = ui->dump.values;

	for(int i=0; i<ui->dump.sensors; i++)
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
	//TODO
}

static Evas_Object *
_content_get(eo_ui_t *eoui)
{
	UI *ui = (void *)eoui - offsetof(UI, eoui);

	ui->tab = elm_table_add(eoui->win);
	elm_table_homogeneous_set(ui->tab, EINA_TRUE);
	elm_table_align_set(ui->tab, 0.5, 0.5);

	_dump_fill(ui);

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

	eo_ui_driver_t driver;
	if(descriptor == &visualizer_eo)
		driver = EO_UI_DRIVER_EO;
	else if(descriptor == &visualizer_ui)
		driver = EO_UI_DRIVER_UI;
	else if(descriptor == &visualizer_x11)
		driver = EO_UI_DRIVER_X11;
	else if(descriptor == &visualizer_kx)
		driver = EO_UI_DRIVER_KX;
	else
		return NULL;

	UI *ui = calloc(1, sizeof(UI));
	if(!ui)
		return NULL;

	eo_ui_t *eoui = &ui->eoui;
	eoui->driver = driver;
	eoui->content_get = _content_get;
	eoui->w = 400,
	eoui->h = 400;

	ui->dump.sensors = 160;
	ui->write_function = write_function;
	ui->controller = controller;
	
	int i, j;
	for(i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			ui->map = (LV2_URID_Map *)features[i]->data;
  }

	if(!ui->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(ui);
		return NULL;
	}

	ui->uris.event_transfer = ui->map->map(ui->map->handle, LV2_ATOM__eventTransfer);
	chimaera_forge_init(&ui->cforge, ui->map);

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

	// do nothing
	if(i == 1)
	{
		uint32_t sensors = *(float *)buf;

		if(sensors != ui->dump.sensors)
		{
			ui->dump.sensors = sensors;
			_dump_fill(ui);
		}
	}
	else if( (i == 3) && (urid == ui->uris.event_transfer) )
	{
		const LV2_Atom *atom = buf;

		if(chimaera_dump_check_type(&ui->cforge, atom))
		{
			chimaera_dump_deforge(&ui->cforge, buf, &ui->dump);
			_dump_update(ui);
		}
		else if(chimaera_event_check_type(&ui->cforge, atom))
		{
			chimaera_event_t cev;

			chimaera_event_deforge(&ui->cforge, buf, &cev);
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
