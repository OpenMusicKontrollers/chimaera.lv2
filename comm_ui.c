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
#include <lv2_osc.h>

typedef struct _UI UI;

struct _UI {
	eo_ui_t eoui;

	LV2_URID_Map *map;
	struct {
		LV2_URID event_transfer;
	} uris;
	osc_forge_t oforge;
	chimaera_forge_t cforge;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	chimaera_event_t cev;

	Evas_Object *pane;
};
	
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

	return ui->pane;
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
  }

	if(!ui->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(ui);
		return NULL;
	}

	ui->uris.event_transfer = ui->map->map(ui->map->handle, LV2_ATOM__eventTransfer);
	chimaera_forge_init(&ui->cforge, ui->map);
	osc_forge_init(&ui->oforge, ui->map);

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

	if(i == 1) // notify
	{
		//TODO
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
