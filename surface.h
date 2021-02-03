/*
 * surface.h
 * Copyright (C) 2021 Sanjay Rao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
void deinit_surface_xclient(struct surface_xclient *surface);
int init_surface_xclient(struct surface_xclient *s, unsigned int rows, unsigned int columns, uint32_t bvalue, unsigned int sbcount);
int resize_surface_xclient(struct surface_xclient *s, struct x11info *x, unsigned int oldrows, unsigned int oldcolumns,
		unsigned int newrows, unsigned int newcolumns, uint32_t blankvalue, unsigned int sbcount, unsigned int cellw, unsigned int cellh,
		long fillcolor, int isremap);
