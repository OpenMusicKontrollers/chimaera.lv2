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

	chimaera_event_t cev;

	int units;

	int uw, uh, bs;
	int w, h;
	Ecore_Evas *ee;
	Evas *e;
	Evas_Object *theme;
};

// Idle interface
static int
idle_cb(LV2UI_Handle handle)
{
	UI *ui = handle;

	if(!ui)
		return -1;

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

	if(ui->ee)
		ecore_evas_resize(ui->ee, ui->w, ui->h);

	evas_object_size_hint_aspect_set(ui->theme, EVAS_ASPECT_CONTROL_BOTH,
		ui->w, ui->h);
	evas_object_size_hint_min_set(ui->theme, ui->w, ui->h);
	evas_object_resize(ui->theme, ui->w, ui->h);
  
  return 0;
}
	
static inline void
_write_event(UI *ui, chimaera_event_t *cev)
{
	uint8_t buf[256];

	lv2_atom_forge_set_buffer(&ui->cforge.forge, buf, 256);
	chimaera_event_forge(&ui->cforge, cev);

	ui->write_function(ui->controller, 0, ui->cforge.forge.size,
		ui->uris.event_transfer, buf);
}

static inline void
_get_pos(UI *ui, Evas_Coord_Point *coord, float *dx, float *dz, float *dw)
{
	Evas_Coord x, y, w, h;
	const Evas_Object *canvas = edje_object_part_object_get(ui->theme, "canvas");

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

	edje_object_part_drag_value_set(ui->theme, "magnet", x, 0.0);
	edje_object_part_drag_size_set(ui->theme, "magnet", w, z);

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
	Evas_Event_Mouse_Up *ev = event_info;

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

	edje_object_part_drag_value_set(ui->theme, "magnet", x, 0.0);
	edje_object_part_drag_size_set(ui->theme, "magnet", w, z);
	
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
		
	edje_object_signal_emit(ui->theme, "magnet,show", CHIMAERA_SIMULATOR_UI_URI);
}

static void
_mouse_out(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	UI *ui = data;
	
	edje_object_signal_emit(ui->theme, "magnet,hide", CHIMAERA_SIMULATOR_UI_URI);
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor,
	const char *plugin_uri, const char *bundle_path,
	LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	ecore_evas_init();
	edje_init();
	//edje_frametime_set(0.04);

	if(strcmp(plugin_uri, CHIMAERA_SIMULATOR_URI))
		return NULL;

	UI *ui = calloc(1, sizeof(UI));
	if(!ui)
		return NULL;

	ui->units = 3;
	ui->w = 10;
	ui->h = 10;
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

	if(descriptor == &simulator_ui) // load X11 UI?
	{
		fprintf(stdout, "%s: using X11 UI\n", descriptor->URI);

		ui->ee = ecore_evas_gl_x11_new(NULL, (Ecore_X_Window)parent,
			0, 0, ui->w, ui->h);
		if(!ui->ee)
			ui->ee = ecore_evas_software_x11_new(NULL, (Ecore_X_Window)parent,
				0, 0, ui->w, ui->h);
		if(!ui->ee)
			printf("could not start evas\n");
		ui->e = ecore_evas_get(ui->ee);
		ecore_evas_show(ui->ee);
	}
	else if(descriptor == &simulator_eo) // load Eo UI?
	{
		ui->ee = NULL;
		ui->e = evas_object_evas_get((Evas_Object *)parent);
	}

	char buf[512];
	sprintf(buf, "%s/chimaera_ui.edj", bundle_path);

	ui->theme = edje_object_add(ui->e);
	edje_object_file_set(ui->theme, buf, CHIMAERA_SIMULATOR_UI_URI"/theme");
	const char *unit_width = edje_object_data_get(ui->theme, "unit_width");
	const char *unit_height = edje_object_data_get(ui->theme, "unit_height");
	const char *border_size = edje_object_data_get(ui->theme, "border_size");
	evas_object_size_hint_weight_set(ui->theme, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ui->theme, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_size_hint_min_set(ui->theme, ui->w, ui->h);
	evas_object_size_hint_aspect_set(ui->theme, EVAS_ASPECT_CONTROL_BOTH,
		ui->w, ui->h);
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
	evas_object_resize(ui->theme, ui->w, ui->h);
	evas_object_show(ui->theme);

	ui->uw = atoi(unit_width);
	ui->uh = atoi(unit_height);
	ui->bs = atoi(border_size);

	ui->w = ui->uw*ui->units + 2*ui->bs;
	ui->h = ui->uh + 2*ui->bs;

	resize_cb(ui, ui->w, ui->h);

	if(ui->resize)
    ui->resize->ui_resize(ui->resize->handle, ui->w, ui->h);
	
	if(ui->ee) // X11 UI
		*(Evas_Object **)widget = NULL;
	else // Eo UI
		*(Evas_Object **)widget = ui->theme;

	return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
	UI *ui = handle;
	
	if(ui)
	{
		if(ui->ee) // X11 UI
			ecore_evas_hide(ui->ee);

		evas_object_del(ui->theme);

		if(ui->ee) // X11 UI
			ecore_evas_free(ui->ee);
		
		free(ui);
	}

	edje_shutdown();
	ecore_evas_shutdown();
}

static void
port_event(LV2UI_Handle handle, uint32_t i, uint32_t size, uint32_t urid,
	const void *buf)
{
	UI *ui = handle;

	// do nothing
	if(i == 1)
	{
		int sensors = *(float *)buf;
		ui->units = sensors / 16;

		ui->w = ui->uw*ui->units + 2*ui->bs;
		ui->h = ui->uh + 2*ui->bs;

		resize_cb(ui, ui->w, ui->h);

		char buf [16];
		sprintf(buf, "%i", sensors);
		edje_object_signal_emit(ui->theme, buf, CHIMAERA_SIMULATOR_UI_URI);

		if(ui->resize)
			ui->resize->ui_resize(ui->resize->handle, ui->w, ui->h);
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

const LV2UI_Descriptor simulator_eo = {
	.URI						= CHIMAERA_SIMULATOR_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= NULL
};

const LV2UI_Descriptor simulator_ui = {
	.URI						= CHIMAERA_SIMULATOR_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};
