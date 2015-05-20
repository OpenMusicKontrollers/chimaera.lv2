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

LV2_SYMBOL_EXPORT const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &dummy_in;
		case 1:
			return &tuio2_in;
		case 2:
			return &filter;
		case 3:
			return &control_out;
		case 4:
			return &midi_out;
		case 5:
			return &osc_out;
		case 6:
			return &mapper;
		case 7:
			return &simulator;
		case 8:
			return &visualizer;
		case 9:
			return &dump_in;
		case 10:
			return &comm;
		default:
			return NULL;
	}
}
