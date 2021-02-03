
/*
 * config.c - manage configuration parameters
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "common/conventions.h"

#include "config.h"

void recalc_config(struct config *c) {
struct color_config *bg;

// we can crash if these are 0 (loop optimizations), maybe 1, set to 2 to be safe
if (c->rows<2) c->rows=2;
if (c->columns<2) c->columns=2;

c->cellstride=4*c->cellw;
c->linestride=4*c->xwidth;
c->rowbytes=c->linestride*c->cellh;
c->cellbytes=c->cellstride*c->cellh;
c->rowwidth=c->columns*c->cellw;
c->colheight=c->rows*c->cellh;

c->xoff=(c->xwidth-c->rowwidth)/2;
c->yoff=(c->xheight-c->colheight)/2;

c->cursorheight=_BADMIN(c->cursorheight,c->cellh);
c->cursoryoff=_BADMIN(c->cursoryoff,c->cellh-c->cursorheight);

if (c->font0shift>0) c->font0shift=_BADMIN(c->font0shift,c->cellw);
if (c->font0line>0) c->font0line=_BADMIN(c->font0line,c->cellh);
if (c->fontulline>0) c->fontulline=_BADMIN(c->fontulline,c->cellh-1);
// if (c->fontullines>0) c->fontullines=_BADMIN(c->fontullines,c->cellh); // this will be checked later anyway

if (c->isdarkmode) bg=&c->darkmode.colors[0];
else bg=&c->lightmode.colors[0];
c->bgbgra[0]=bg->b;
c->bgbgra[1]=bg->g;
c->bgbgra[2]=bg->r;
c->bgbgra[3]=255;
}

void reset_config(struct config *c) {
c->cmdline=NULL;
#if 0
// ansi might be a more honest default but it's not as useful
strcpy(c->exportterm,"ansi");
#warning defaulting to TERM=ansi
#else
strcpy(c->exportterm,"linux");
#endif
strcpy(c->typeface,"monospace-17");
c->xwidth=960;
c->xheight=540;
c->cellw=14;
c->cellh=27;
c->columns=68;
c->rows=20;
c->scrollbackcount=1000;
c->cursorheight=11;
c->cursoryoff=15;

c->font0shift=-1;
c->font0line=-1;
c->fontulline=-1;
c->fontullines=-1;

c->red_cursor=0xffff;
c->green_cursor=0;
c->blue_cursor=0;
c->isfullscreen=0;

c->isdarkmode=1;
c->isblinkcursor=1;

#define SETCOLOR(c,red,green,blue) do { c.r=red; c.g=green; c.b=blue; } while (0)
SETCOLOR(c->lightmode.colors[0],0xEE,0xE8,0xD5); // background: dark white
SETCOLOR(c->lightmode.colors[1],0xDC,0x32,0x2F); // dark red
SETCOLOR(c->lightmode.colors[2],0x85,0x99,0x00); // dark green
SETCOLOR(c->lightmode.colors[3],0xB5,0x89,0x00); // dark yellow
SETCOLOR(c->lightmode.colors[4],0x26,0x8B,0xD2); // dark blue
SETCOLOR(c->lightmode.colors[5],0xD3,0x36,0x82); // dark magenta
SETCOLOR(c->lightmode.colors[6],0x2A,0xA1,0x98); // dark cyan
SETCOLOR(c->lightmode.colors[7],0x07,0x36,0x42); // dark white: background
SETCOLOR(c->lightmode.colors[8],0xFD,0xF6,0xE3); // bright black: foreground
SETCOLOR(c->lightmode.colors[9],0xCB,0x4B,0x16); // bright red
SETCOLOR(c->lightmode.colors[10],0x93,0xA1,0xA1); // bright green: bright cyan
SETCOLOR(c->lightmode.colors[11],0x83,0x94,0x96); // bright yellow: bright blue
SETCOLOR(c->lightmode.colors[12],0x65,0x7B,0x83); // bright blue: bright yellow
SETCOLOR(c->lightmode.colors[13],0x6C,0x71,0xC4); // bright magenta
SETCOLOR(c->lightmode.colors[14],0x58,0x6E,0x75); // bright cyan: bright green
SETCOLOR(c->lightmode.colors[15],0x00,0x2B,0x36); // foreground: bright black

SETCOLOR(c->darkmode.colors[0],0x07,0x36,0x42); // background
SETCOLOR(c->darkmode.colors[1],0xDC,0x32,0x2F); // dark red
SETCOLOR(c->darkmode.colors[2],0x85,0x99,0x00); // dark green
SETCOLOR(c->darkmode.colors[3],0xB5,0x89,0x00); // dark yellow
SETCOLOR(c->darkmode.colors[4],0x26,0x8B,0xD2); // dark blue
SETCOLOR(c->darkmode.colors[5],0xD3,0x36,0x82); // dark magenta
SETCOLOR(c->darkmode.colors[6],0x2A,0xA1,0x98); // dark cyan
SETCOLOR(c->darkmode.colors[7],0xEE,0xE8,0xD5); // dark white
SETCOLOR(c->darkmode.colors[8],0x00,0x2B,0x36); // bright black
SETCOLOR(c->darkmode.colors[9],0xCB,0x4B,0x16); // bright red
SETCOLOR(c->darkmode.colors[10],0x58,0x6E,0x75); // bright green
SETCOLOR(c->darkmode.colors[11],0x65,0x7B,0x83); // bright yellow
SETCOLOR(c->darkmode.colors[12],0x83,0x94,0x96); // bright blue
SETCOLOR(c->darkmode.colors[13],0x6C,0x71,0xC4); // bright magenta
SETCOLOR(c->darkmode.colors[14],0x93,0xA1,0xA1); // bright cyan
SETCOLOR(c->darkmode.colors[15],0xFD,0xF6,0xE3); // foreground

c->charcache=200; // 2000 has worked, 100 seems ok

(void)recalc_config(c);
}
