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

typedef struct _UI UI;

struct _UI {
	LV2_URID_Map *map;
	struct {
		LV2_URID event_transfer;
	} uris;
	chimaera_forge_t cforge;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;
	LV2UI_Resize *resize;

	int uw, uh, bs;
	int w, h;

	chimaera_dump_t dump;

	char theme_path[512];

	volatile int dump_needs_update;
	volatile int event_needs_update;

	Ecore_Evas *ee;
	Evas *e;
	Evas_Object *theme;
	Evas_Object *hbox;
};

static void _dump_update(UI *ui);
static void _event_update(UI *ui);

// Idle interface
static int
idle_cb(LV2UI_Handle handle)
{
	UI *ui = handle;

	if(!ui)
		return -1;

	if(ui->dump_needs_update)
	{
		_dump_update(ui);
		ui->dump_needs_update = 0;
	}

	if(ui->event_needs_update)
	{
		_event_update(ui);
		ui->event_needs_update = 0;
	}

	ecore_main_loop_iterate();
	
	return 0;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = idle_cb
};

// Show Interface
static int
_show_cb(LV2UI_Handle handle)
{
	UI *ui = handle;

	if(!ui)
		return -1;

	ecore_evas_show(ui->ee);

	return 0;
}

static int
_hide_cb(LV2UI_Handle handle)
{
	UI *ui = handle;

	if(!ui)
		return -1;

	ecore_evas_hide(ui->ee);

	return 0;
}

static const LV2UI_Show_Interface show_ext = {
	.show = _show_cb,
	.hide = _hide_cb
};

// Resize Interface
static int
resize_cb(LV2UI_Feature_Handle handle, int w, int h)
{
	UI *ui = handle;

	if(!ui)
		return -1;

	ui->w = w;
	ui->h = h;

	ecore_evas_resize(ui->ee, ui->w, ui->h);
	evas_object_resize(ui->theme, ui->w, ui->h);
  
  return 0;
}

static void
_dump_fill(UI *ui)
{
	evas_object_table_clear(ui->hbox, EINA_TRUE);
	
	for(int i=0; i<ui->dump.sensors; i++)
	{
		Evas_Object *north = edje_object_add(ui->e);
		edje_object_file_set(north, ui->theme_path, CHIMAERA_VISUALIZER_UI_URI"/sensor");
		evas_object_size_hint_weight_set(north, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(north, EVAS_HINT_FILL, EVAS_HINT_FILL);
		edje_object_part_drag_value_set(north, "sensor", 0.5, 1.f);
		edje_object_part_drag_size_set(north, "sensor", 1.f, 0.f);
		evas_object_show(north);
		evas_object_table_pack(ui->hbox, north, i, 0, 1, 1);
		
		Evas_Object *south = edje_object_add(ui->e);
		edje_object_file_set(south, ui->theme_path, CHIMAERA_VISUALIZER_UI_URI"/sensor");
		evas_object_size_hint_weight_set(south, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(south, EVAS_HINT_FILL, EVAS_HINT_FILL);
		edje_object_part_drag_value_set(south, "sensor", 0.5, 0.f);
		edje_object_part_drag_size_set(south, "sensor", 1.f, 0.f);
		evas_object_show(south);
		evas_object_table_pack(ui->hbox, south, i, 1, 1, 1);

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
		Evas_Object *north = evas_object_table_child_get(ui->hbox, i, 0);
		Evas_Object *south = evas_object_table_child_get(ui->hbox, i, 1);
		double rel = values[i];
		rel *= 1.f / 0x7ff;
		double north_size = rel < 0.f ? -rel : 0.f;
		double south_size = rel < 0.f ? 0.f : rel;
		uint8_t red = rel < 0.f ? (-rel)*0xbb : rel*0xbb;
		
		//printf("%i %i %lf %lf %lf\n", i, values[i], rel, north_size, south_size);

		evas_object_color_set(north, 0xbb, 0xbb-red, 0xbb-red, 0xff);
		evas_object_color_set(south, 0xbb, 0xbb-red, 0xbb-red, 0xff);

		edje_object_part_drag_size_set(north, "sensor", 1.f, north_size);
		edje_object_part_drag_size_set(south, "sensor", 1.f, south_size);
	}
}

static void
_event_update(UI *ui)
{
	//TODO
}
	
static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri, const char *bundle_path, LV2UI_Write_Function write_function, LV2UI_Controller controller, LV2UI_Widget *widget, const LV2_Feature *const *features)
{
	ecore_evas_init();
	edje_init();
	//edje_frametime_set(0.04);

	if(strcmp(plugin_uri, CHIMAERA_VISUALIZER_URI))
		return NULL;

	UI *ui = calloc(1, sizeof(UI));
	if(!ui)
		return NULL;

	ui->dump.sensors = 160;
	ui->uw = 142;
	ui->uh = 92;
	ui->bs = 12;
	ui->w = ui->uw*ui->dump.sensors/16 + 2*ui->bs;
	ui->h = ui->uh*2 + 2*ui->bs;
	ui->write_function = write_function;
	ui->controller = controller;

	void *parent = NULL;
	ui->resize = NULL;
	
	int i, j;
	for(i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_UI__parent))
			parent = features[i]->data;
		else if (!strcmp(features[i]->URI, LV2_UI__resize))
			ui->resize = (LV2UI_Resize *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__map))
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

	ui->ee = ecore_evas_gl_x11_new(NULL, (Ecore_X_Window)parent, 0, 0, ui->w, ui->h);
	if(!ui->ee)
		ui->ee = ecore_evas_software_x11_new(NULL, (Ecore_X_Window)parent, 0, 0, ui->w, ui->h);
	if(!ui->ee)
		printf("could not start evas\n");
	ecore_evas_data_set(ui->ee, "ui", ui);
	ui->e = ecore_evas_get(ui->ee);
	ecore_evas_show(ui->ee);

	sprintf(ui->theme_path, "%s/chimaera_ui.edj", bundle_path);

	ui->theme = edje_object_add(ui->e);
	edje_object_file_set(ui->theme, ui->theme_path, CHIMAERA_VISUALIZER_UI_URI"/theme");
	const char *border_size = edje_object_data_get(ui->theme, "border_size");
	evas_object_pointer_mode_set(ui->theme, EVAS_OBJECT_POINTER_MODE_NOGRAB);
	evas_object_resize(ui->theme, ui->w, ui->h);
	evas_object_show(ui->theme);

	ui->hbox = evas_object_table_add(ui->e);
	evas_object_table_homogeneous_set(ui->hbox, EINA_TRUE);
	evas_object_table_padding_set(ui->hbox, 4, 0);
	evas_object_table_align_set(ui->hbox, 0.5, 0.5);
	evas_object_size_hint_weight_set(ui->hbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->hbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ui->hbox);
	edje_object_part_swallow(ui->theme, "canvas", ui->hbox);

	_dump_fill(ui);
	
	ui->bs = atoi(border_size);

	if(ui->resize)
    ui->resize->ui_resize(ui->resize->handle, ui->w, ui->h);

	return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
	UI *ui = handle;
	
	if(ui)
	{
		ecore_evas_hide(ui->ee);
	
		edje_object_part_unswallow(ui->theme, ui->hbox);
		evas_object_del(ui->hbox);

		evas_object_del(ui->theme);

		ecore_evas_free(ui->ee);
		
		free(ui);
	}

	edje_shutdown();
	ecore_evas_shutdown();
}

static void
port_event(LV2UI_Handle handle, uint32_t i, uint32_t size, uint32_t urid, const void *buf)
{
	UI *ui = handle;

	// do nothing
	if(i == 1)
	{
		uint32_t sensors = *(float *)buf;

		if(sensors != ui->dump.sensors)
		{
			ui->dump.sensors = sensors;

			ui->w = ui->uw*ui->dump.sensors/16 + 2*ui->bs;
			ui->h = ui->uh*2 + 2*ui->bs;
			if(ui->resize)
				ui->resize->ui_resize(ui->resize->handle, ui->w, ui->h);

			resize_cb(ui, ui->w, ui->h);
			_dump_fill(ui);
		}
	}
	else if( (i == 0) && (urid == ui->uris.event_transfer) )
	{
		const LV2_Atom *atom = buf;
			
		if(chimaera_dump_check_type(&ui->cforge, atom))
		{
			chimaera_dump_deforge(&ui->cforge, buf, &ui->dump);
			ui->dump_needs_update = 1;
		}
		else if(chimaera_event_check_type(&ui->cforge, atom))
		{
			chimaera_event_t cev;

			chimaera_event_deforge(&ui->cforge, buf, &cev);
			ui->event_needs_update = 1;
		}
	}
}

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__showInterface))
		return &show_ext;
		
	return NULL;
}

const LV2UI_Descriptor visualizer_ui = {
	.URI						= CHIMAERA_VISUALIZER_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};
