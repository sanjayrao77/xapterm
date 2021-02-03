/*
 * config.h
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
#define TERMVER_CONFIG	"xapterm-0.1"
#define TERMXTITLE_CONFIG	"xapterm"

#define UNDERLINEBIT_VALUE	(1<<29)
// #define BLINKBIT_VALUE	(1<<30)
#define SELECTINVERSION_VALUE	(1<<31)
#define UCS4_MASK_VALUE	(0x1fffff)
#define FGINDEX_MASK_VALUE	(0xf<<25)
#define BGINDEX_MASK_VALUE	(0xf<<21)

#define MAX_EXPORTTERM_CONFIG	31
#define MAX_TYPEFACE_CONFIG		31
struct config {
	char **cmdline;
	char exportterm[MAX_EXPORTTERM_CONFIG+1];
	char typeface[MAX_TYPEFACE_CONFIG+1];
	unsigned int xwidth,xheight;
	unsigned int cellw,cellh;
	unsigned int columns,rows;
	unsigned int scrollbackcount;
	unsigned int cursorheight,cursoryoff;
	int font0shift,font0line,fontulline,fontullines; // <0 => auto

// these are calculated
	unsigned int cellstride,linestride,rowbytes,cellbytes;
	unsigned int rowwidth,colheight; // these differ from xwidth,xheight by padding
	unsigned char bgbgra[4];
	unsigned int xoff,yoff;

	unsigned short red_cursor,green_cursor,blue_cursor;
	unsigned int isfullscreen:1;
	unsigned int isdarkmode:1;
	unsigned int isblinkcursor:1;
	struct colors_config {
		struct color_config { unsigned char r,g,b; } colors[16];
	} darkmode,lightmode;

// below this, config.apply() ignores but OnInitBegin can modify
	unsigned int charcache;
	struct { // this is readonly
		unsigned int height,width,heightmm,widthmm;
	} screen;
	unsigned int depth;
	unsigned int isnostart:1;
};

void reset_config(struct config *c);
void recalc_config(struct config *c);
