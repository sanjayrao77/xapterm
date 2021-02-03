/*
 * cursor.h
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
struct cursor {
	struct {
		unsigned int isblink:1;
		unsigned int xoff,yoff;
		unsigned int cellw,cellh;
		unsigned int cursorheight,cursoryoff;
	} config;
	struct x11info *x;
	XColor color;
	
	int isplaced:1;
	int isblinkon:1;
	int isdrawn:1;
	unsigned int col,row;

	uint32_t lastvalue;
	struct {
		unsigned int width,height;
		Pixmap blinkon,blinkoff;
		Pixmap backing;
	} pixmaps;
};

int init_cursor(struct cursor *cursor, struct config *config, struct x11info *x);
void deinit_cursor(struct cursor *cursor);
int pulse_cursor(struct cursor *cursor);
int set_cursor(struct cursor *cursor, uint32_t value, Pixmap backing, unsigned int row, unsigned int col);
int unset_cursor(struct cursor *cursor);
int reset_cursor(struct cursor *cursor);
int setcolors_cursor(struct cursor *cursor, unsigned short r, unsigned short g, unsigned short b);
int reconfig_cursor(struct cursor *cursor, unsigned int xoff, unsigned int yoff, unsigned int cellw, unsigned int cellh,
		unsigned int cursorheight, unsigned int cursoryoff, unsigned int isblink);
void setheight_cursor(struct cursor *cursor, unsigned int cursorheight, unsigned int cursoryoff, unsigned int isblink);
