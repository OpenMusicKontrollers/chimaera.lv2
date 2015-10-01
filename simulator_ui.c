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

	LV2UI_Port_Map *port_map;
	uint32_t control_port;
	uint32_t sensor_port;

	chimaera_event_t cev;
	
	char theme_path[512];

	int units;
	Evas_Object *theme;
};
	
static inline void
_write_event(UI *ui, chimaera_event_t *cev)
{
	uint8_t buf[512];

	lv2_atom_forge_set_buffer(&ui->cforge.forge, buf, 512);
	chimaera_event_forge(&ui->cforge, cev);

	ui->write_function(ui->controller, ui->control_port, ui->cforge.forge.offset,
		ui->uris.event_transfer, buf);
}

static inline void
_get_pos(UI *ui, Evas_Coord_Point *coord, float *dx, float *dz, float *dw)
{
	Evas_Coord x, y, w, h;
	Evas_Object *edj = elm_layout_edje_get(ui->theme);
	const Evas_Object *canvas = edje_object_part_object_get(edj, "canvas");
	evas_object_geometry_get(canvas, &x, &y, &w, &h);

	*dx = (coord->x - x) / (float)w;
	*dz = (coord->y - y) / (float)h;

	if(*dx < 0.f)
		*dx = 0.f;
	if(*dx > 1.f)
		*dx = 1.f;
	if(*dz < 0.f)
		*dz = 0.f;
	if(*dz > 1.f)
		*dz = 1.f;

	*dw = 4.f / (float)w;
}

static void
_mouse_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
	Evas_Event_Mouse_Down *ev = event_info;
	float x, z, w;

	_get_pos(ui, &ev->canvas, &x, &z, &w);

	Evas_Object *edj = elm_layout_edje_get(ui->theme);
	edje_object_part_drag_value_set(edj, "magnet", x, 0.0);
	edje_object_part_drag_size_set(edj, "magnet", w, z);

	chimaera_event_t *cev = &ui->cev;

	cev->state = CHIMAERA_STATE_ON;
	cev->sid += 1;
	cev->gid = 0;
	cev->pid = ev->button == 1 ? 128 : 256;
	cev->x = x;
	cev->z = z;
	cev->X = 0.f;
	cev->Z = 0.f;

	_write_event(ui, cev);
	
	cev->state = CHIMAERA_STATE_SET;
}

static void
_mouse_up(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	UI *ui = data;

	chimaera_event_t *cev = &ui->cev;
	
	cev->state = CHIMAERA_STATE_OFF;

	_write_event(ui, cev);
	
	cev->state = CHIMAERA_STATE_IDLE;
}

static void
_mouse_move(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
	Evas_Event_Mouse_Move *ev = event_info;
	float x, z, w;

	_get_pos(ui, &ev->cur.canvas, &x, &z, &w);

	Evas_Object *edj = elm_layout_edje_get(ui->theme);
	edje_object_part_drag_value_set(edj, "magnet", x, 0.0);
	edje_object_part_drag_size_set(edj, "magnet", w, z);
	
	chimaera_event_t *cev = &ui->cev;

	if(cev->state == CHIMAERA_STATE_SET)
	{
		cev->x = x;
		cev->z = z;
		
		_write_event(ui, cev);
	}
}

static void
_mouse_in(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
		
	elm_layout_signal_emit(ui->theme, "magnet,show", CHIMAERA_SIMULATOR_UI_URI);
}

static void
_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
	
	elm_layout_signal_emit(ui->theme, "magnet,hide", CHIMAERA_SIMULATOR_UI_URI);
}

static Evas_Object *
_content_get(eo_ui_t *eoui)
{
	UI *ui = (void *)eoui - offsetof(UI, eoui);

	ui->theme = elm_layout_add(eoui->win);
	elm_layout_file_set(ui->theme, ui->theme_path, CHIMAERA_SIMULATOR_UI_URI"/theme");
	evas_object_event_callback_add(ui->theme,
		EVAS_CALLBACK_MOUSE_DOWN, _mouse_down, ui);
	evas_object_event_callback_add(ui->theme,
		EVAS_CALLBACK_MOUSE_UP, _mouse_up, ui);
	evas_object_event_callback_add(ui->theme,
		EVAS_CALLBACK_MOUSE_MOVE, _mouse_move, ui);
	evas_object_event_callback_add(ui->theme,
		EVAS_CALLBACK_MOUSE_IN, _mouse_in, ui);
	evas_object_event_callback_add(ui->theme,
		EVAS_CALLBACK_MOUSE_OUT, _mouse_out, ui);
	evas_object_pointer_mode_set(ui->theme, EVAS_OBJECT_POINTER_MODE_NOGRAB);

	return ui->theme;
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor,
	const char *plugin_uri, const char *bundle_path,
	LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(strcmp(plugin_uri, CHIMAERA_SIMULATOR_URI))
		return NULL;

	eo_ui_driver_t driv;
	if(descriptor == &simulator_eo)
		driv = EO_UI_DRIVER_EO;
	else if(descriptor == &simulator_ui)
		driv = EO_UI_DRIVER_UI;
	else if(descriptor == &simulator_x11)
		driv = EO_UI_DRIVER_X11;
	else if(descriptor == &simulator_kx)
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

	ui->units = 3;
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
	ui->control_port = ui->port_map->port_index(ui->port_map->handle, "event_in");
	ui->sensor_port = ui->port_map->port_index(ui->port_map->handle, "sensors");

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

	if(i == ui->sensor_port)
	{
		int sensors = *(float *)buf;
		ui->units = sensors / 16;

		char buf2 [16];
		sprintf(buf2, "%i", sensors);
		elm_layout_signal_emit(ui->theme, buf2, CHIMAERA_SIMULATOR_UI_URI);
	}
}

const LV2UI_Descriptor simulator_eo = {
	.URI						= CHIMAERA_SIMULATOR_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_eo_extension_data
};

const LV2UI_Descriptor simulator_ui = {
	.URI						= CHIMAERA_SIMULATOR_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_ui_extension_data
};

const LV2UI_Descriptor simulator_x11 = {
	.URI						= CHIMAERA_SIMULATOR_X11_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_x11_extension_data
};

const LV2UI_Descriptor simulator_kx = {
	.URI						= CHIMAERA_SIMULATOR_KX_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_kx_extension_data
};
