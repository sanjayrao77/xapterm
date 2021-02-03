/*
 * xftchar.c - do font drawing
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
#include <inttypes.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xft/Xft.h>
#include <time.h>
#include <netinet/in.h>
#define DEBUG
#include "common/conventions.h"
#include "common/safemem.h"
#include "config.h"
#include "x11info.h"

#include "xftchar.h"

static inline void getdims_xftchar(struct xftchar *xc, unsigned char ch) {
XGlyphInfo ext;
(void)XftTextExtents8(xc->x->display,xc->font,&ch,1,&ext);
fprintf(stderr,"struct XGlyphInfo { width:%u, height:%u, x:%d, y:%d, xOff:%d, yOff:%d } %c\n",
		ext.width, ext.height, ext.x, ext.y, ext.xOff, ext.yOff, ch);
} 

static void seteffective(struct xftchar *xc) {
XftFont *f;
f=xc->font;
if (!f) {
	WHEREAMI;
	xc->effective.font0shift= xc->effective.font0line= xc->effective.fontulline= xc->effective.fontullines= 0;
	return;
}
if (xc->config.font0shift>=0) xc->effective.font0shift=xc->config.font0shift;
else {
	xc->effective.font0shift=0;
//	if (f->max_advance_width<xc->width) xc->config.font0shift=(xc->width-f->max_advance_width)/2;
}
if (xc->config.font0line>=0) xc->effective.font0line=xc->config.font0line;
else {
	xc->effective.font0line=f->ascent;
	if (f->height<xc->height) xc->effective.font0line+=(xc->height-f->height)/2;
//	fprintf(stderr,"%s:%d setting font0line:%d\n",__FILE__,__LINE__,xc->effective.font0line);
}
if (xc->config.fontulline>=0) xc->effective.fontulline=xc->config.fontulline;
else {
	xc->effective.fontulline=f->ascent+f->descent/4;
}
if (xc->config.fontullines>=0) xc->effective.fontullines=xc->config.fontullines;
else {
	xc->effective.fontullines=f->descent/4;
}
if (xc->effective.fontulline+xc->effective.fontullines>xc->height) {
	if (xc->effective.fontulline<xc->height) {
		xc->effective.fontullines=xc->height-xc->effective.fontulline;
	} else {
		xc->effective.fontullines=0;
	}
}
}

int init_xftchar(struct xftchar *xc, struct config *config, struct x11info *x) {
xc->config.font0shift=config->font0shift;
xc->config.font0line=config->font0line;
xc->config.fontulline=config->fontulline;
xc->config.fontullines=config->fontullines;
// xc->config.cellstride=config->cellstride;

xc->x=x;
xc->width=config->cellw;
xc->height=config->cellh;
#if 0
xc->font=XftFontOpen(x->display,x->screen,
		XFT_FAMILY,XftTypeString,"Monospace",
		XFT_SIZE,XftTypeDouble,17.0,
		NULL);
#endif
// TODO query the font here to get the actual font
xc->font=XftFontOpenName(x->display,x->screen,config->typeface);
#if 0
xc->font=XftFontOpenName(x->display,x->screen,"monospace-14");
#endif
if (!xc->font) GOTOERROR;
#if 0
{
	fprintf(stderr,"%s:%d XftFont ascent:%d descent:%d height:%d max_advance_width:%d\n",__FILE__,__LINE__,
			xc->font->ascent,
			xc->font->descent,
			xc->font->height,
			xc->font->max_advance_width);
}
#endif

(void)seteffective(xc);

#if 1
xc->pixmap=XCreatePixmap(x->display,x->window,xc->width,xc->height,x->depth);
if (!xc->pixmap) GOTOERROR;
xc->draw=XftDrawCreate(x->display,x->window,x->visual,x->colormap);
if (!xc->draw) GOTOERROR;
(void)XftDrawChange(xc->draw,xc->pixmap);
// for some reason, this doesn't work:
// xc->draw=XftDrawCreateAlpha(x->display,xc->pixmap,x->depth);
#else
// this method works but it's visible to the user, we use the top-right character for temporary drawing
// it's here because it was used first but replaced with the XftDrawCreate/XftDrawChange approach
xc->draw=XftDrawCreate(x->display,x->window,x->visual,x->colormap);
#endif
if (!xc->draw) GOTOERROR;
// if (!(xc->data=MALLOC(xc->width*xc->height*4))) GOTOERROR;
return 0;
error:
	return -1;
}

int setcolor_xftchar(struct xftchar *xc, unsigned int index, unsigned short red, unsigned short green, unsigned short blue) {
XRenderColor xrc;
if (xc->colors[index].isallocated) (void)XftColorFree(xc->x->display,xc->x->visual,xc->x->colormap,&xc->colors[index].xftc);
xrc.blue=blue;
xrc.green=green;
xrc.red=red;
xrc.alpha=65535;
if (!XftColorAllocValue(xc->x->display,xc->x->visual,xc->x->colormap,&xrc,&xc->colors[index].xftc)) GOTOERROR;
xc->colors[index].isallocated=1;
return 0;
error:
	return -1;
}

int changefont_xftchar(struct xftchar *xc, char *fontname) {
struct x11info *x=xc->x;
if (xc->font) {
	XftFontClose(x->display,xc->font);
}
if (!(xc->font=XftFontOpenName(x->display,x->screen,fontname))) GOTOERROR;
(void)seteffective(xc);
// fprintf(stderr,"Opened font: %s\n",fontname);
return 0;
error:
	return -1;
}

void deinit_xftchar(struct xftchar *xc) {
if (!xc->x) return;
if (xc->font) XftFontClose(xc->x->display,xc->font);
if (xc->pixmap) XFreePixmap(xc->x->display,xc->pixmap);
if (xc->draw) XftDrawDestroy(xc->draw);
// IFFREE(xc->data);
{
	int index;
	for (index=0;index<16;index++) {
		if (!xc->colors[index].isallocated) continue;
		(void)XftColorFree(xc->x->display,xc->x->visual,xc->x->colormap,&xc->colors[index].xftc);
	}
}
}

void drawchar2_xftchar(Pixmap dest, struct xftchar *xc, unsigned int ucs4, unsigned int bgindex, unsigned int fgindex, int isunderline) {
XftColor *bg_xftc,*fg_xftc;

(void)XftDrawChange(xc->draw,dest);
// xc->isotherpixmap=1; // for drawchar_xftchar

bg_xftc=&xc->colors[bgindex].xftc;
fg_xftc=&xc->colors[fgindex].xftc;

(void)XftDrawRect(xc->draw,bg_xftc,0,0,xc->width,xc->height);
#if 0
{
	XGlyphInfo extent;
	(void)XftTextExtents32(xc->x->display,xc->font,&ucs4,1,&extent);
	fprintf(stderr,"%s:%d extent width:%u height:%u x:%d y:%d xOff:%d yOff:%d\n",__FILE__,__LINE__,
		extent.width,
		extent.height,
		extent.x,
		extent.y,
		extent.xOff,
		extent.yOff);
}
#endif
(void)XftDrawString32(xc->draw,fg_xftc,xc->font,xc->effective.font0shift,xc->effective.font0line,&ucs4,1);
if (isunderline) (void)XftDrawRect(xc->draw,fg_xftc,0,xc->effective.fontulline,xc->width,xc->effective.fontullines);
}

#if 0
int drawchar_xftchar(struct xftchar *xc, unsigned char *utf8, unsigned int utf8len,
		unsigned char *bg_bgr, unsigned char *fg_bgr) {
XRenderColor xrc;
XftColor xftc;

if (xc->isotherpixmap) (void)XftDrawChange(xc->draw,xc->pixmap);

xrc.blue=(bg_bgr[0]<<8); xrc.green=(bg_bgr[1]<<8); xrc.red=(bg_bgr[2]<<8); xrc.alpha=65535;
// xrc.red= xrc.green= xrc.blue= xrc.alpha=65535;
if (!XftColorAllocValue(xc->x->display,xc->x->visual,xc->x->colormap,&xrc,&xftc)) GOTOERROR;
#if 1
(void)XftDrawRect(xc->draw,&xftc,0,0,xc->width,xc->height);
#else
(void)XftDrawRect(xc->draw,&xftc,1920-xc->width,0,xc->width,xc->height);
#endif
(void)XftColorFree(xc->x->display,xc->x->visual,xc->x->colormap,&xftc);

xrc.blue=(fg_bgr[0]<<8); xrc.green=(fg_bgr[1]<<8); xrc.red=(fg_bgr[2]<<8); xrc.alpha=65535;
if (!XftColorAllocValue(xc->x->display,xc->x->visual,xc->x->colormap,&xrc,&xftc)) GOTOERROR;
#if 1
// getdims_xftchar(xc,ch);
// (void)XftDrawString8(xc->draw,&xftc,xc->font,0,18,&ch,1);
(void)XftDrawStringUtf8(xc->draw,&xftc,xc->font,xc->config.font0shift,xc->config.font0line,utf8,utf8len);
#else
(void)XftDrawString8(xc->draw,&xftc,xc->font,1920-14+2,18,&ch,1);
#endif
// (void)XftDrawRect(xc->draw,&xftc,1,14,8,9);
(void)XftColorFree(xc->x->display,xc->x->visual,xc->x->colormap,&xftc);

{
	XImage *xi;
#if 1
	xi=XGetImage(xc->x->display,xc->pixmap,0,0,xc->width,xc->height,AllPlanes,ZPixmap);
#else
	xi=XGetImage(xc->x->display,xc->x->window,1920-xc->width,0,xc->width,xc->height,AllPlanes,ZPixmap);
#endif
	if (!xi) GOTOERROR;
// fprintf(stderr,"xi, bytes_per_line:%d height:%d\n",xi->bytes_per_line,xi->height);
	memcpy(xc->data,xi->data,xc->width*xc->height*4);
	XDestroyImage(xi);
}

return 0;
error:
	return -1;
}
#endif

#if 0
void underline_xftchar(struct xftchar *xc, unsigned char *bgra) {
unsigned char *data,*lastdata;

data=xc->data+xc->config.fontulline*xc->config.cellstride;
lastdata=data+xc->config.cellstride*xc->config.fontullines;
while (1) {
	memcpy(data,bgra,4);
	data+=4;
	if (data==lastdata) break;
}
}
#endif

#if 0
int underline2_xftchar(Pixmap dest, struct xftchar *xc, unsigned char *bgra) {
XRenderColor xrc;
XftColor xftc;

xrc.blue=(bgra[0]<<8); xrc.green=(bgra[1]<<8); xrc.red=(bgra[2]<<8); xrc.alpha=65535;
if (!XftColorAllocValue(xc->x->display,xc->x->visual,xc->x->colormap,&xrc,&xftc)) GOTOERROR;
(void)XftDrawRect(xc->draw,&xftc,0,xc->config.fontulline,xc->width,xc->config.fontullines);
(void)XftColorFree(xc->x->display,xc->x->visual,xc->x->colormap,&xftc);
return 0;
error:
	return -1;
}
#endif

int resize_xftchar(struct xftchar *xc, unsigned int width, unsigned int height) {
struct x11info *x=xc->x;
XFreePixmap(x->display,xc->pixmap);
xc->pixmap=XCreatePixmap(x->display,x->window,width,height,x->depth);
if (!xc->pixmap) GOTOERROR;
(void)XftDrawChange(xc->draw,xc->pixmap);

xc->width=width;
xc->height=height;
return 0;
error:
	return -1;
}

void setparams_xftchar(struct xftchar *xc, int font0shift, int font0line, int fontulline, int fontullines) {
xc->config.font0shift=font0shift;
xc->config.font0line=font0line;
xc->config.fontulline=fontulline;
xc->config.fontullines=fontullines;
(void)seteffective(xc);
}

int queryfont_xftchar(struct queryfont_xftchar *qf, struct x11info *xi, char *name) {
XftFont *font=NULL;
FcPattern *fpin=NULL,*fpout=NULL;
FcResult fcr;

fpin=XftNameParse(name);
if (!fpin) goto nada;
fpout=XftFontMatch(xi->display,xi->screen,fpin,&fcr);

#if 0
WHEREAMI;
FcPatternPrint(fpin);
if (fpout) FcPatternPrint(fpout);
#endif

font=XftFontOpenName(xi->display,xi->screen,name);
if (!font) goto nada;

qf->ismatch=(fcr==FcResultMatch)?1:0;
qf->ascent=font->ascent;
qf->descent=font->descent;
qf->width=font->max_advance_width;

FcPatternDestroy(fpin);
if (fpout) FcPatternDestroy(fpout);
XftFontClose(xi->display,font);
return 0;
nada:
	if (font) XftFontClose(xi->display,font);
	if (fpin) FcPatternDestroy(fpin);
	if (fpout) FcPatternDestroy(fpout);
	qf->ismatch=qf->ascent=qf->descent=qf->width=0;
	return 0;
}
