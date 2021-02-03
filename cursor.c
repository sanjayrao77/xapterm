/*
 * cursor.c - handle cursor drawing and position
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <pty.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#define DEBUG
#include "config.h"
#include "common/conventions.h"
#include "x11info.h"
#include "pty.h"
#include "vte.h"
#include "xclient.h"

#include "cursor.h"

#define DEBUG2

static int setcolor(XColor *xc, struct x11info *x, unsigned short r, unsigned short g, unsigned short b) {
static XColor blank;
if (xc->flags) (ignore)XFreeColors(x->display,x->colormap,&xc->pixel,1,0);
*xc=blank;
xc->red=r;
xc->green=g;
xc->blue=b;
xc->flags=DoRed|DoGreen|DoBlue;
if (!XAllocColor(x->display,x->colormap,xc)) GOTOERROR;
return 0;
error:
	return -1;
}

int init_cursor(struct cursor *cursor, struct config *config, struct x11info *x) {
cursor->config.isblink=config->isblinkcursor;
cursor->config.xoff=config->xoff;
cursor->config.yoff=config->yoff;
cursor->config.cellw=config->cellw;
cursor->config.cellh=config->cellh;
cursor->config.cursorheight=_BADMAX(2,config->cursorheight);
cursor->config.cursoryoff=_BADMIN(config->cursoryoff,config->cellh - cursor->config.cursorheight);

cursor->x=x;
if (setcolor(&cursor->color,x,config->red_cursor,config->green_cursor,config->blue_cursor)) GOTOERROR;
if (!(cursor->pixmaps.backing=XCreatePixmap(x->display,x->window,cursor->config.cellw,cursor->config.cellh,x->depth))) GOTOERROR;
if (!(cursor->pixmaps.blinkon=XCreatePixmap(x->display,x->window,cursor->config.cellw,cursor->config.cellh,x->depth))) GOTOERROR;
if (!(cursor->pixmaps.blinkoff=XCreatePixmap(x->display,x->window,cursor->config.cellw,cursor->config.cellh,x->depth))) GOTOERROR;
cursor->pixmaps.width=config->cellw;
cursor->pixmaps.height=config->cellh;
return 0;
error:
	return -1;
}
void deinit_cursor(struct cursor *cursor) {
struct x11info *x=cursor->x;
if (cursor->pixmaps.backing) (ignore)XFreePixmap(x->display,cursor->pixmaps.backing);
if (cursor->pixmaps.blinkon) (ignore)XFreePixmap(x->display,cursor->pixmaps.blinkon);
if (cursor->pixmaps.blinkoff) (ignore)XFreePixmap(x->display,cursor->pixmaps.blinkoff);
if (cursor->color.flags) {
	(ignore)XFreeColors(x->display,x->colormap,&cursor->color.pixel,1,0);
}
}

int pulse_cursor(struct cursor *cursor) {
struct x11info *x=cursor->x;
if (!cursor->isplaced) return 0;
if (!cursor->config.isblink) {
	if (cursor->isdrawn) return 0;
	cursor->isdrawn=1;
	cursor->isblinkon=0;
}
if (!cursor->isblinkon) {
	cursor->isblinkon=1;
	if (!XCopyArea(x->display,cursor->pixmaps.blinkon,x->window,x->context,0,0,cursor->config.cellw,cursor->config.cellh,
			cursor->config.xoff+cursor->col*cursor->config.cellw,cursor->config.yoff+cursor->row*cursor->config.cellh)) GOTOERROR;
} else {
	cursor->isblinkon=0;
	if (!XCopyArea(x->display,cursor->pixmaps.blinkoff,x->window,x->context,0,0,cursor->config.cellw,cursor->config.cellh,
			cursor->config.xoff+cursor->col*cursor->config.cellw,cursor->config.yoff+cursor->row*cursor->config.cellh)) GOTOERROR;
}
XFlush(x->display);
return 0;
error:
	return -1;
}

int set_cursor(struct cursor *cursor, uint32_t value, Pixmap backing, unsigned int row, unsigned int col) {
if (cursor->lastvalue!=value) {
	struct x11info *x=cursor->x;
	if (!XCopyArea(x->display,backing,cursor->pixmaps.backing,x->context,0,0,cursor->config.cellw,cursor->config.cellh,0,0)) GOTOERROR;
	if (!XCopyArea(x->display,backing,cursor->pixmaps.blinkon,x->context,0,0,cursor->config.cellw,cursor->config.cellh,0,0)) GOTOERROR;
	if (!XCopyArea(x->display,backing,cursor->pixmaps.blinkoff,x->context,0,0,cursor->config.cellw,cursor->config.cellh,0,0)) GOTOERROR;
	if (!XSetForeground(x->display,x->context,cursor->color.pixel)) GOTOERROR;
	if (!XFillRectangle(x->display,cursor->pixmaps.blinkon,x->context,0,cursor->config.cursoryoff,cursor->config.cellw,cursor->config.cursorheight)) GOTOERROR;
	if (!XFillRectangle(x->display,cursor->pixmaps.blinkoff,x->context,0,cursor->config.cursoryoff+cursor->config.cursorheight-2,cursor->config.cellw,2)) GOTOERROR;
	cursor->lastvalue=value;
}

cursor->col=col;
cursor->row=row;
cursor->isblinkon=0;
cursor->isplaced=1;

if (pulse_cursor(cursor)) GOTOERROR;
return 0;
error:
	return -1;
}
int unset_cursor(struct cursor *cursor) {
struct x11info *x=cursor->x;
if (!cursor->isplaced) return 0;
cursor->isplaced=0;
cursor->isdrawn=0;
if (!XCopyArea(x->display,cursor->pixmaps.backing,x->window,x->context,0,0,cursor->config.cellw,cursor->config.cellh,
		cursor->config.xoff+cursor->col*cursor->config.cellw,cursor->config.yoff+cursor->row*cursor->config.cellh)) GOTOERROR;
XFlush(x->display);
return 0;
error:
	return -1;
}

int reset_cursor(struct cursor *cursor) {
if (unset_cursor(cursor)) GOTOERROR;
cursor->lastvalue=0;
return 0;
error:
	return -1;
}

int setcolors_cursor(struct cursor *cursor, unsigned short r, unsigned short g, unsigned short b) {
if (setcolor(&cursor->color,cursor->x,r,g,b)) GOTOERROR;
return 0;
error:
	return -1;
}

void setheight_cursor(struct cursor *cursor, unsigned int cursorheight, unsigned int cursoryoff, unsigned int isblink) {
cursor->config.cursorheight=cursorheight;
cursor->config.cursoryoff=cursoryoff;
cursor->config.isblink=isblink;
}

int reconfig_cursor(struct cursor *cursor, unsigned int xoff, unsigned int yoff, unsigned int cellw, unsigned int cellh,
		unsigned int cursorheight, unsigned int cursoryoff, unsigned int isblink) {
struct x11info *x=cursor->x;
if (reset_cursor(cursor)) GOTOERROR;

if ((cellw > cursor->pixmaps.width)||(cellh > cursor->pixmaps.height)) {
	cursor->pixmaps.width=_BADMAX(cursor->pixmaps.width,cellw);
	cursor->pixmaps.height=_BADMAX(cursor->pixmaps.height,cellh);
	if (!XFreePixmap(x->display,cursor->pixmaps.backing)) GOTOERROR;
	if (!XFreePixmap(x->display,cursor->pixmaps.blinkon)) GOTOERROR;
	if (!XFreePixmap(x->display,cursor->pixmaps.blinkoff)) GOTOERROR;
	if (!(cursor->pixmaps.backing=XCreatePixmap(x->display,x->window,cursor->pixmaps.width,cursor->pixmaps.height,x->depth))) GOTOERROR;
	if (!(cursor->pixmaps.blinkon=XCreatePixmap(x->display,x->window,cursor->pixmaps.width,cursor->pixmaps.height,x->depth))) GOTOERROR;
	if (!(cursor->pixmaps.blinkoff=XCreatePixmap(x->display,x->window,cursor->pixmaps.width,cursor->pixmaps.height,x->depth))) GOTOERROR;
}

cursor->config.xoff=xoff;
cursor->config.yoff=yoff;
cursor->config.cellw=cellw;
cursor->config.cellh=cellh;
cursor->config.cursorheight=cursorheight;
cursor->config.cursoryoff=cursoryoff;
cursor->config.isblink=isblink;

#if 0
fprintf(stderr,"%s:%d xoff:%u yoff:%u cellw:%u cellh:%u cursorheight:%u cursoryoff:%u\n",
		__FILE__,__LINE__,xoff,yoff,cellw,cellh,cursorheight,cursoryoff);
#endif
return 0;
error:
	return -1;
}
