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

LV2_SYMBOL_EXPORT const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &simulator_eo;
		case 1:
			return &simulator_ui;
		case 2:
			return &simulator_x11;
		case 3:
			return &simulator_kx;

		case 4:
			return &visualizer_eo;
		case 5:
			return &visualizer_ui;
		case 6:
			return &visualizer_x11;
		case 7:
			return &visualizer_kx;

		case 8:
			return &comm_eo;
		case 9:
			return &comm_ui;
		case 10:
			return &comm_x11;
		case 11:
			return &comm_kx;

		default:
			return NULL;
	}
}
