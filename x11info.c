/*
 * x11info.c - manage basic X11 tasks
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
// #include <X11/extensions/XShm.h>
#include <time.h>
#include <netinet/in.h>
#define DEBUG
#include "common/conventions.h"

#include "x11info.h"

static inline void fillframe(unsigned char *frame,unsigned char *bgra, unsigned int pixels) {
while (1) {
	memcpy(frame,bgra,4);
	pixels--;
	if (!pixels) break;
	frame+=4;
}
}
#if 0
int fillpaint_image_x11info(struct image_x11info *ix, unsigned char *bgra, unsigned int stop, unsigned int dtop, unsigned int height) {
(void)fillframe(ix->frame+stop*(ix->width*4),bgra,height*ix->width);
return paint2_image_x11info(ix,0,stop,0,dtop,ix->width,height,1);
}
#endif

#if 0
int init_image_x11info(struct image_x11info *ix, struct x11info *x, unsigned int w, unsigned int h, unsigned char *bgbgra) {
ix->x=x;
ix->width=w;
ix->height=h;
#if 0
if (tryshm && x->hasxshmext) {
	ix->image=XShmCreateImage(x->display,
			x->visual,
			x->depth,ZPixmap,NULL,&ix->shminfo,w,h);
	if (!ix->image) GOTOERROR;
	if (ix->image->bytes_per_line!=4*w) GOTOERROR;
	if (ix->image->height!=h) GOTOERROR;
	if (0>(ix->shminfo.shmid=shmget(IPC_PRIVATE,ix->image->bytes_per_line * ix->image->height, IPC_CREAT|0777))) GOTOERROR;
	ix->shminfo.shmaddr=NULL;
	ix->isxshm=1;
	if (!(ix->shminfo.shmaddr=ix->image->data=shmat(ix->shminfo.shmid,NULL,0))) GOTOERROR;
	ix->frame=(unsigned char *)ix->shminfo.shmaddr;
	ix->shminfo.readOnly=True;
	if (!XShmAttach(x->display,&ix->shminfo)) GOTOERROR;
//	if (!XInitImage(ix->image)) GOTOERROR; // this confuses XDestroyImage do a bad free
	XFlush(x->display);
} else {
#endif
	if (x->Bpp!=4) GOTOERROR;
	if (!(ix->frame=malloc(w*h*4))) GOTOERROR;
	ix->image=XCreateImage(x->display,
			x->visual,
			x->depth,ZPixmap,0,(char *)ix->frame,w,h,
			32,0);
	if (!ix->image) GOTOERROR;
	if (!XInitImage(ix->image)) GOTOERROR;
#if 0
}
#endif
(void)fillframe(ix->frame,bgbgra,w*h);
return 0;
error:
	return -1;
}
#endif

#if 0
void deinit_image_x11info(struct image_x11info *ix) {
#if 0
if (ix->isxshm) {
	XShmDetach(ix->x->display,&ix->shminfo);
}
#endif
if (ix->image) XDestroyImage(ix->image);
else iffree(ix->frame);
#if 0
if (ix->isxshm) {
	if (ix->shminfo.shmaddr) shmdt(ix->shminfo.shmaddr);
	shmctl(ix->shminfo.shmid,IPC_RMID,0);
}
#endif
}
#endif

#if 0
int paint_image_x11info(struct image_x11info *ix, unsigned int left, unsigned int top) {
#if 0
if (ix->isxshm) {
	int evtype;
	if (!XShmPutImage(ix->x->display,ix->x->window,ix->x->context,ix->image,0,0,left,top,ix->width,ix->height,True)) GOTOERROR;
	evtype=XShmGetEventBase(ix->x->display)+ShmCompletion;
	if (waitforevent(ix->x,evtype,__LINE__,1)) GOTOERROR;
} else {
#endif
	if (XPutImage(ix->x->display,ix->x->window,
			ix->x->context,
			ix->image,
			0,0,left,top,ix->width,ix->height)) GOTOERROR;
	XFlush(ix->x->display);
// }
return 0;
error:
	return -1;
}

int paint2_image_x11info(struct image_x11info *ix, unsigned int sleft, unsigned int stop, unsigned int dleft, unsigned int dtop,
		unsigned int width, unsigned int height) {
#if 0
if (ix->isxshm) {
	if (!XShmPutImage(ix->x->display,ix->x->window,ix->x->context,ix->image,sleft,stop,dleft,dtop,width,height,False)) GOTOERROR;
} else {
#endif
	if (XPutImage(ix->x->display,ix->x->window,
			ix->x->context,
			ix->image,
			sleft,stop,dleft,dtop,width,height)) GOTOERROR;
// }
return 0;
error:
	return -1;
}
#endif
#if 0
int paint2_image_x11info(struct image_x11info *ix, unsigned int sleft, unsigned int stop, unsigned int dleft, unsigned int dtop,
		unsigned int width, unsigned int height, int iswait) {
if (ix->isxshm) {
	if (!XShmPutImage(ix->x->display,ix->x->window,ix->x->context,ix->image,sleft,stop,dleft,dtop,width,height,iswait)) GOTOERROR;
	if (iswait) {
		int evtype;
		evtype=XShmGetEventBase(ix->x->display)+ShmCompletion;
		if (waitforevent(ix->x,evtype,__LINE__,1)) GOTOERROR;
	}
} else {
	if (XPutImage(ix->x->display,ix->x->window,
			ix->x->context,
			ix->image,
			sleft,stop,dleft,dtop,width,height)) GOTOERROR;
	if (iswait) XFlush(ix->x->display);
}
return 0;
error:
	return -1;
}
#endif

#if 0
static char *errorcodetostring(int error_code, char *def) {
switch (error_code) {
	case BadAccess: return "BadAccess";
	case BadAlloc: return "BadAlloc";
	case BadAtom: return "BadAtom";
	case BadColor: return "BadColor";
	case BadCursor: return "BadCursor";
	case BadDrawable: return "BadDrawable";
	case BadFont: return "BadFont";
	case BadGC: return "BadGC";
	case BadIDChoice: return "BadIDChoice";
	case BadImplementation: return "BadImplementation";
	case BadLength: return "BadLength";
	case BadMatch: return "BadMatch";
	case BadName: return "BadName";
	case BadPixmap: return "BadPixmap";
	case BadRequest: return "BadRequest";
	case BadValue: return "BadValue";
	case BadWindow: return "BadWindow";
}
return def;
}
#endif

#if 0
static unsigned long goterror_global;

static int nullerrorhandler(Display *d, XErrorEvent *ev) {
#if 0
fprintf(stderr,"Received error from X server (%d:%s) serial:%lu\n",
		ev->error_code,errorcodetostring(ev->error_code,"Unknown"),
		ev->serial
		);
#endif
goterror_global=ev->serial;
return 0;
}
#endif

char *evtypetostring_x11info(int type, char *def) {
switch (type) {
	case KeyPress: return "KeyPress";
	case KeyRelease: return "KeyRelease";
	case ButtonPress: return "ButtonPress";
	case ButtonRelease: return "ButtonRelease";
	case MotionNotify: return "MotionNotify";
	case EnterNotify: return "EnterNotify";
	case LeaveNotify: return "LeaveNotify";
	case FocusIn: return "FocusIn";
	case FocusOut: return "FocusOut";
	case KeymapNotify: return "KeymapNotify";
	case Expose: return "Expose";
	case GraphicsExpose: return "GraphicsExpose";
	case NoExpose: return "NoExpose";
	case VisibilityNotify: return "VisibilityNotify";
	case CreateNotify: return "CreateNotify";
	case DestroyNotify: return "DestroyNotify";
	case UnmapNotify: return "UnmapNotify";
	case MapNotify: return "MapNotify";
	case MapRequest: return "MapRequest";
	case ReparentNotify: return "ReparentNotify";
	case ConfigureNotify: return "ConfigureNotify";
	case ConfigureRequest: return "ConfigureRequest";
	case GravityNotify: return "GravityNotify";
	case ResizeRequest: return "ResizeRequest";
	case CirculateNotify: return "CirculateNotify";
	case CirculateRequest: return "CirculateRequest";
	case PropertyNotify: return "PropertyNotify";
	case SelectionClear: return "SelectionClear";
	case SelectionRequest: return "SelectionRequest";
	case SelectionNotify: return "SelectionNotify";
	case ColormapNotify: return "ColormapNotify";
	case ClientMessage: return "ClientMessage";
	case MappingNotify: return "MappingNotify";
	case GenericEvent: return "GenericEvent";
}
return def;
}

int waitforevent_x11info(Display *display, XEvent *dest, int type, int line, int seconds) {
int fd;
time_t toolong;

if (XCheckTypedEvent(display,type,dest)) return 0;
fd=ConnectionNumber(display);
toolong=time(NULL)+seconds;
while (1) {
	struct timeval tv;
	fd_set rset;

	FD_ZERO(&rset);
	FD_SET(fd,&rset);
	tv.tv_sec=1;
	tv.tv_usec=0;
	if (0>select(fd+1,&rset,NULL,NULL,&tv)) GOTOERROR;

	if (XCheckTypedEvent(display,type,dest)) return 0;
	if ((seconds) && (time(NULL)>toolong)) { dest->type=0; return 0; }
}
return 0;
error:
	return -1;
}

#if 0
static int waitforevent(struct x11info *x, int type, int line, int seconds) {
// seconds: <0 => wait then error on timeout, 0=> wait forever, >0 => wait then give up
XEvent e;
int fd;
time_t toolong;

if (XCheckTypedEvent(x->display,type,&e)) return 0;
fd=ConnectionNumber(x->display);
toolong=time(NULL)+((seconds>0)?seconds:-seconds);
while (1) {
	struct timeval tv;
	fd_set rset;

	FD_ZERO(&rset);
	FD_SET(fd,&rset);
	tv.tv_sec=1;
	tv.tv_usec=0;
	if (0>select(fd+1,&rset,NULL,NULL,&tv)) GOTOERROR;

	if (XCheckTypedEvent(x->display,type,&e)) return 0;

	if ((seconds) && (time(NULL)>toolong)) {
		if (seconds<0) {
			fprintf(stderr,"%s:%d Timed out waiting for event %s.\n",__FILE__,line,evtypetostring_x11info(type,"Unknown"));
			GOTOERROR;
		}
		return 0;
	}
}
return 0;
error:
	return -1;
}
#endif

#if 0
static int waitforevent(struct x11info *x, int type, int line, int ispatient) {
int fd;
fd_set rset;
struct timeval tv;

XFlush(x->display);

fd=ConnectionNumber(x->display);
while (1) {
	XEvent e;
	FD_ZERO(&rset);
	FD_SET(fd,&rset);
	tv.tv_sec=10;
	tv.tv_usec=0;
	
	if (!XEventsQueued(x->display,QueuedAlready)) switch (select(fd+1,&rset,NULL,NULL,&tv)) {
		case 0:
			if (!ispatient) {
				fprintf(stderr,"%s:%d Timed out waiting for event %s.\n",__FILE__,line,evtypetostring_x11info(type,"Unknown"));
				GOTOERROR;
			}
			break;
		case -1: GOTOERROR;
	}
	if (!XEventsQueued(x->display,QueuedAfterReading)) continue;
	if (XNextEvent(x->display,&e)) GOTOERROR;
	if (e.type==FocusIn) x->isfocused=1;
	else if (e.type==FocusOut) x->isfocused=0;
	if (e.type==type) {
#if 0
		fprintf(stderr,"%s:%d Matched event type, %d:%s\n",__FILE__,line,e.type,evtypetostring_x11info(e.type,"Unknown"));
#endif
		break;
	}
#if 0
	fprintf(stderr,"%s:%d Ignored event type, %d:%s\n",__FILE__,line,e.type,evtypetostring_x11info(e.type,"Unknown"));
#endif
}
return 0;
error:
	return -1;
}
#endif

int halfinit_x11info(struct x11info *x, char *display) {
if (!(x->display=XOpenDisplay(display))) {
	if (!(x->display=XOpenDisplay(":10.0"))) GOTOERROR; // try default ssh tunnel
}
x->screen=DefaultScreen(x->display);

x->defscreen.height=DisplayHeight(x->display,x->screen);
x->defscreen.heightmm=DisplayHeightMM(x->display,x->screen);
x->defscreen.width=DisplayWidth(x->display,x->screen);
x->defscreen.widthmm=DisplayWidthMM(x->display,x->screen);

x->depth=DefaultDepth(x->display,x->screen);
return 0;
error:
	return -1;
}

int init_x11info(struct x11info *x, unsigned int width, unsigned int height, char *display, unsigned char *bgra_bg, int isfs,
		char *wintitle) {
unsigned int awidth,aheight;

if (!x->display) {
	if (halfinit_x11info(x,display)) GOTOERROR;
}

// x->depth=24;
// x->Bpp=4;

x->width=width;
x->height=height;

if (x->depth==DefaultDepth(x->display,x->screen)) {
	x->visual=DefaultVisual(x->display,x->screen);
	x->colormap=DefaultColormap(x->display,x->screen);
} else {
	XVisualInfo *xvip,template;
	int *depths,count,i;
	depths=XListDepths(x->display,x->screen,&count);
	for (i=0;i<count;i++) if (x->depth==depths[i]) break;
	if (i==count) {
		fprintf(stderr,"Chosen depth (%u) unsupported (",x->depth);
		for (i=0;i<count;i++) fprintf(stderr,"%s%d",(i)?",":"",depths[i]);
		fputs(")\n",stderr);
		if (depths) XFree(depths);
		GOTOERROR;
	}
	if (depths) XFree(depths);

	template.depth=x->depth;
	xvip=XGetVisualInfo(x->display,VisualDepthMask,&template,&count);
	if (!xvip) {
		fprintf(stderr,"%s:%d No visuals found for depth:%u\n",__FILE__,__LINE__,x->depth);
		XFree(xvip);
		GOTOERROR;
	}
	x->visual=xvip->visual;
	XFree(xvip);
	if (!(x->colormap=XCreateColormap(x->display,XDefaultRootWindow(x->display),x->visual,AllocNone))) GOTOERROR;
}

#if 0
if (tryshm) {
	char *disable;
	disable=getenv("_X11_NO_MITSHM");
	if (disable) {
		if (!strcmp(disable,"1")) tryshm=0;
	}
	if (tryshm) x->hasxshmext=XShmQueryExtension(x->display);
	if (x->hasxshmext) {
		x->shmcompletiontype=XShmGetEventBase(x->display)+ShmCompletion;
	}
}
#endif

x->attr.colormap=x->colormap;
x->attr.border_pixel=WhitePixel(x->display,x->screen);
{
	XColor xc;
	unsigned int r=bgra_bg[2],g=bgra_bg[1],b=bgra_bg[0];
	xc.red=(r<<8)|r; xc.green=(g<<8)|g; xc.blue=(b<<8)|b;
	xc.flags=DoRed|DoGreen|DoBlue;
	if (!XAllocColor(x->display,x->colormap,&xc)) GOTOERROR; // we'll never free this explicitly
	x->attr.background_pixel=xc.pixel;
}
x->attr.event_mask= ExposureMask| ButtonPressMask | ButtonReleaseMask | Button1MotionMask |
		Button3MotionMask | KeyPressMask | KeyReleaseMask | FocusChangeMask  | StructureNotifyMask;
awidth=width;aheight=height;
if (isfs) {
	awidth=x->defscreen.width/2;
	aheight=x->defscreen.height/2;
}
x->window=XCreateWindow(x->display,DefaultRootWindow(x->display),0,0,
		awidth,aheight,0,x->depth,InputOutput,x->visual,CWBackPixel|CWBorderPixel|CWColormap|CWEventMask,
		&x->attr);
if (!x->window) GOTOERROR;
if (!XStoreName(x->display,x->window,wintitle)) GOTOERROR;
#if 0
if (!XSelectInput(x->display,x->window,ExposureMask|
			ButtonPressMask |
			ButtonReleaseMask |
			Button1MotionMask |
			Button3MotionMask |
			KeyPressMask |
			KeyReleaseMask |
			FocusChangeMask  |
			StructureNotifyMask
			)) GOTOERROR;
#endif

if (isfs) {
	Atom property;
	struct {
		unsigned long flags,functions,decorations;
		long inputMode;
		unsigned long status;
	} hints;
	hints.flags=2;hints.decorations=0;
	property=XInternAtom(x->display,"_MOTIF_WM_HINTS",True);
	if (!XChangeProperty(x->display,x->window,property,property,32,PropModeReplace,(unsigned char *)&hints,5)) GOTOERROR;
}


if (!XMapWindow(x->display,x->window)) GOTOERROR;
{
	XEvent e;

	if (waitforevent_x11info(x->display,&e,MapNotify,__LINE__,10)) GOTOERROR;
	if (!e.type) GOTOERROR;
	if (waitforevent_x11info(x->display,&e,Expose,__LINE__,10)) GOTOERROR;
	if (!e.type) GOTOERROR;
	if (waitforevent_x11info(x->display,&e,FocusIn,__LINE__,10)) GOTOERROR;
	if (!e.type) GOTOERROR;
	x->isfocused=1;

	if (isfs && ((awidth!=width)||(aheight!=height))) {
		if (!XResizeWindow(x->display,x->window,width,height)) GOTOERROR;
		if (waitforevent_x11info(x->display,&e,ConfigureNotify,__LINE__,10)) GOTOERROR;
		if (!e.type) GOTOERROR;
		if (!XMoveWindow(x->display,x->window,0,0)) GOTOERROR;
		if (waitforevent_x11info(x->display,&e,ConfigureNotify,__LINE__,10)) GOTOERROR;
		if (!e.type) GOTOERROR;
	}
}

x->context=XCreateGC(x->display,x->window,0,&x->vls);
if (!x->context) GOTOERROR;

x->vls.graphics_exposures=False;
if (!XChangeGC(x->display,x->context,GCGraphicsExposures,&x->vls)) GOTOERROR;

XSetForeground(x->display,x->context,
	WhitePixel(x->display,x->screen));
XSetBackground(x->display,x->context,
	BlackPixel(x->display,x->screen));

#if 0
x->font=XLoadFont(x->display,"fixed");
if (!x->font) GOTOERROR;
XSetFont(x->display,x->context,x->font);
#endif
XFlushGC(x->display,x->context);

(ignore)XDefineCursor(x->display,x->window,XCreateFontCursor(x->display,150));
XFlush(x->display);

#if 0
{
	Pixmap p;
	unsigned int ui=0;
	while (1) {
		p=XCreatePixmap(x->display,x->window,CELLW,CELLH,24);
		if (!p) {
			fprintf(stderr,"ui:%u\n",ui);
			GOTOERROR;
		}
		XSync(x->display,False);
		ui++;
		if (ui==1000) break;
	}
}

#endif

return 0;
error:
	return -1;
}

void deinit_x11info(struct x11info *x) {
if (x->context) XFreeGC(x->display,x->context);
if (x->window) XDestroyWindow(x->display,x->window);
}

#if 0
int checkforkey_x11info(unsigned int *keycode_out, struct x11info *x) {
XEvent e;
if (XCheckTypedEvent(x->display,KeyPress,&e)) {
	if (keycode_out) *keycode_out= e.xkey.keycode;
	return 1;
}
return 0;
}
int waitforkey_x11info(struct x11info *x) {
if (waitforevent(x,KeyPress,__LINE__,1)) GOTOERROR;
return 0;
error:
	return -1;
}
#endif


#if 0
int testforshm_x11info(int *isfound_out, struct x11info *x) {
XImage *image=NULL;
XShmSegmentInfo shminfo;
int isfound=1;

if (!x->hasxshmext) { *isfound_out=0; return 0; }
shminfo.shmaddr=NULL;
shminfo.shmid=-1;

XSetErrorHandler(nullerrorhandler);
goterror_global=0;
image=XShmCreateImage(x->display,x->visual,x->depth,ZPixmap,NULL,&shminfo,1,1);
if (!image) GOTOERROR;
if (0>(shminfo.shmid=shmget(IPC_PRIVATE,image->bytes_per_line*image->height,IPC_CREAT|0777))) GOTOERROR;
if (!(shminfo.shmaddr=image->data=shmat(shminfo.shmid,NULL,0))) GOTOERROR;
if (!XShmAttach(x->display,&shminfo)) GOTOERROR;
XSync(x->display,False);
XShmDetach(x->display,&shminfo);
XDestroyImage(image); image=NULL;
shmdt(shminfo.shmaddr); shminfo.shmaddr=NULL;
shmctl(shminfo.shmid,IPC_RMID,0); shminfo.shmid=-1;
XSync(x->display,False);

if (goterror_global) isfound=0;
XSetErrorHandler(NULL);

*isfound_out=isfound;
return 0;
error:
	if (shminfo.shmaddr) XShmDetach(x->display,&shminfo);
	if (image) XDestroyImage(image);
	if (shminfo.shmaddr) shmdt(shminfo.shmaddr);
	if (shminfo.shmid>=0) shmctl(shminfo.shmid,IPC_RMID,0);
	XSetErrorHandler(NULL);
	return -1;
}
#endif


int resizewindow_x11info(struct x11info *x, unsigned int width, unsigned int height) {
XEvent e;
if (!XResizeWindow(x->display,x->window,width,height)) GOTOERROR;
if (waitforevent_x11info(x->display,&e,ConfigureNotify,__LINE__,10)) GOTOERROR;
if (!e.type) GOTOERROR;
return 0;
error:
	return -1;
}

int movewindow_x11info(struct x11info *xi, int x, int y) {
XEvent e;
if (!XMoveWindow(xi->display,xi->window,x,y)) GOTOERROR;
if (waitforevent_x11info(xi->display,&e,ConfigureNotify,__LINE__,10)) GOTOERROR;
if (!e.type) GOTOERROR;
return 0;
error:
	return -1;
}
