/*
 * xclipboard.c - manage the X11 clipboard
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
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <X11/Xlib.h>
#define DEBUG
#include "common/conventions.h"
#include "x11info.h"

#include "xclipboard.h"

int init_xclipboard(struct xclipboard *xclip, struct x11info *xi) {
xclip->display=xi->display;
xclip->window=xi->window;
if (!(xclip->utf8=XInternAtom(xclip->display,"UTF8_STRING",False))) GOTOERROR;
if (!(xclip->string=XInternAtom(xclip->display,"STRING",False))) GOTOERROR;
if (!(xclip->incr=XInternAtom(xclip->display,"INCR",False))) GOTOERROR;
if (!(xclip->paste.xseldata=XInternAtom(xclip->display,"XSEL_DATA",False))) GOTOERROR;
return 0;
error:
	return -1;
}

void deinit_xclipboard(struct xclipboard *xclip) {
if (!xclip->display) return;
iffree(xclip->copy.value);
}

int finish_copy_xclipboard(struct xclipboard *xclip) {
xclip->isowner=1;
if (!(XSetSelectionOwner(xclip->display,xclip->copy.selection,xclip->window,CurrentTime))) GOTOERROR;
return 0;
error:
	return -1;
}

int add_copy_xclipboard(struct xclipboard *xclip, unsigned char *str, unsigned int len) {
if (xclip->copy.valuelen+len>xclip->copy.valuemax) return -1;
memcpy(xclip->copy.value+xclip->copy.valuelen,str,len);
xclip->copy.valuelen+=len;
return 0;
}

int reserve_copy_xclipboard(struct xclipboard *xclip, char *str_selection, unsigned int len) {
if (strlen(str_selection)>MAX_SELECTION_XCLIPBOARD) GOTOERROR;
if (strcmp(str_selection,xclip->copy.str_selection)) {
	Atom selection;
	if (!(selection=XInternAtom(xclip->display,str_selection,False))) GOTOERROR;
	xclip->copy.selection=selection;
	strcpy(xclip->copy.str_selection,str_selection);
}
if (len>xclip->copy.valuemax) {
	unsigned char *temp;
	unsigned int max;
	max=len+1024;
	if (!(temp=realloc(xclip->copy.value,max))) GOTOERROR;
	xclip->copy.value=temp;
	xclip->copy.valuemax=max;
}
xclip->copy.valuelen=0;
return 0;
error:
	return -1;
}

int copy_xclipboard(struct xclipboard *xclip, char *str_selection, unsigned char *value, unsigned int len) {
if (strlen(str_selection)>MAX_SELECTION_XCLIPBOARD) GOTOERROR;
if (strcmp(str_selection,xclip->copy.str_selection)) {
	Atom selection;
	if (!(selection=XInternAtom(xclip->display,str_selection,False))) GOTOERROR;
	xclip->copy.selection=selection;
	strcpy(xclip->copy.str_selection,str_selection);
}
if (len>xclip->copy.valuemax) {
	unsigned char *temp;
	unsigned int max;
	max=len+1024;
	if (!(temp=realloc(xclip->copy.value,max))) GOTOERROR;
	xclip->copy.value=temp;
	xclip->copy.valuemax=max;
}
xclip->copy.valuelen=len;
memcpy(xclip->copy.value,value,len);
xclip->isowner=1;
if (!(XSetSelectionOwner(xclip->display,xclip->copy.selection,xclip->window,CurrentTime))) GOTOERROR;
return 0;
error:
	return -1;
}

static int reject_selection(struct xclipboard *xclip, XSelectionRequestEvent *sre) {
XSelectionEvent se;

se.type=SelectionNotify;
se.requestor=sre->requestor;
se.selection=sre->selection;
se.target=sre->target;
se.property=None;
se.time=sre->time;
if (!XSendEvent(xclip->display,se.requestor,True,NoEventMask,(XEvent*)&se)) GOTOERROR;
return 0;
error:
	return -1;
}

static int send_selection(struct xclipboard *xclip, XSelectionRequestEvent *sre) {
XSelectionEvent se;

if (!(XChangeProperty(xclip->display,sre->requestor,sre->property,xclip->utf8,8,PropModeReplace,xclip->copy.value,
		xclip->copy.valuelen))) GOTOERROR;

se.type=SelectionNotify;
se.requestor=sre->requestor;
se.selection=sre->selection;
se.target=sre->target;
se.property=sre->property;
se.time=sre->time;
if (!XSendEvent(xclip->display,se.requestor,True,NoEventMask,(XEvent*)&se)) GOTOERROR;
XFlush(xclip->display);
return 0;
error:
	return -1;
}

int onselectionrequest_xclipboard(struct xclipboard *xclip, XSelectionRequestEvent *e) {
if (e->selection!=xclip->copy.selection) {
	if (reject_selection(xclip,e)) GOTOERROR;
} else if ((e->target!=xclip->utf8)&&(e->target!=xclip->string)) {
#if 0
	{
		char *name;
		name=XGetAtomName(xclip->display,e->target);
		fprintf(stderr,"Got target of type %s\n",name);
		if (name) XFree(name);
	}
#endif
	if (reject_selection(xclip,e)) GOTOERROR;
} else if (e->property==None) {
	if (reject_selection(xclip,e)) GOTOERROR;
} else {
	if (send_selection(xclip,e)) GOTOERROR;
}
return 0;
error:
	return -1;
}

int paste_xclipboard(unsigned int *newlen_out, struct xclipboard *xclip, char *str_selection, int timeout) {
XEvent e;
if (strlen(str_selection)>MAX_SELECTION_XCLIPBOARD) GOTOERROR;
if (xclip->isowner) {
	if (!strcmp(str_selection,xclip->copy.str_selection)) {
		xclip->isloopback=1; // avoid deadlock of pasting our own copy
		*newlen_out=xclip->copy.valuelen;
		return 0;
	}
}

xclip->isloopback=0;
if (strcmp(str_selection,xclip->paste.str_selection)) {
	Atom selection;
	if (!(selection=XInternAtom(xclip->display,str_selection,False))) GOTOERROR;
	xclip->paste.selection=selection;
	strcpy(xclip->paste.str_selection,str_selection);
}
{
	Window w;
	w=XGetSelectionOwner(xclip->display,xclip->paste.selection);
	if (!w) { *newlen_out=0; return 0; } // no owner => no point in requesting it
}
if (!XConvertSelection(xclip->display,xclip->paste.selection,xclip->utf8,xclip->paste.xseldata,
		xclip->window,CurrentTime)) GOTOERROR;
while (1) {
	XSelectionEvent *se;
	if (waitforevent_x11info(xclip->display,(XEvent*)&e,SelectionNotify,__LINE__,timeout)) GOTOERROR;
	if (!e.type) { *newlen_out=0; return 0; } // timeout
	se=&e.xselection;
	if (se->property!=xclip->paste.xseldata) continue; // wrong reply
	if (se->property==None) { *newlen_out=0; return 0; } // no data or couldn't get data
	break;
}
{
	Atom type;
	int format;
	unsigned long nitems,bytesafter;
	unsigned char *ret;
	if (Success!=XGetWindowProperty(xclip->display,xclip->window,xclip->paste.xseldata,0,0,False,AnyPropertyType,
			&type,&format,&nitems,&bytesafter,&ret)) GOTOERROR;
	if (ret) XFree(ret);
	if (type==xclip->incr) { *newlen_out=0; return 0; } // INCR is unsupported
	if ((type!=xclip->utf8)&&(type!=xclip->string)) GOTOERROR;
	if (format!=8) GOTOERROR;
	*newlen_out=bytesafter;
}
return 0;
error:
	return -1;
}

int getpaste_xclipboard(struct xclipboard *xclip, unsigned char *dest, unsigned int destlen) {
Atom type;
int format;
unsigned long nitems,bytesafter;
unsigned char *ret;

if (!destlen) return 0;

if (xclip->isloopback) {
	if (destlen!=xclip->copy.valuelen) GOTOERROR;
	memcpy(dest,xclip->copy.value,destlen);
	return 0;
}
if (Success!=XGetWindowProperty(xclip->display,xclip->window,xclip->paste.xseldata,0,destlen,True,
		AnyPropertyType,&type,&format,&nitems,&bytesafter,&ret)) GOTOERROR;
if (format!=8) GOTOERROR;
if (nitems!=destlen) GOTOERROR;
if (bytesafter) GOTOERROR;
memcpy(dest,ret,destlen);
if (ret) XFree(ret);
// XDeleteProperty(xclip->display,xclip->window,xclip->paste.xseldata); // WGWP does this above
return 0;
error:
	return -1;
}

void onselectionclear_xclipboard(struct xclipboard *xclip) {
xclip->isowner=0;
}
