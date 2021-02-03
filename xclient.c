/*
 * xclient.c - main loop, X11 events and top level drawing
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
#include <ctype.h>
#include <time.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>
#define DEBUG
#include "config.h" 
#include "common/conventions.h"
#include "common/safemem.h"
#include "common/blockmem.h"
#include "common/texttap.h"
#include "pty.h"
#include "x11info.h"
#include "xftchar.h"
#include "charcache.h"
#include "event.h"
#include "vte.h"
#include "cursor.h"
#include "keysym.h"
#include "xclipboard.h"

#include "xclient.h"
#include "surface.h"

#define DEBUG2

static int redrawrect(struct xclient *xc, unsigned int ex, unsigned int ey, unsigned int ew, unsigned int eh);
static int setcursor(struct xclient *xc, unsigned int row, unsigned int col);
static int clearandredrawselection(struct xclient *xc);

static inline void memset4(unsigned int *dest, unsigned int v, unsigned int count) {
unsigned int *lastdest;
lastdest=dest+count;
while (1) {
	*dest=v;
	dest++;
	if (dest==lastdest) break;
}
}

static inline int ismoved(struct xclient *xc, int x1, int y1, int x2, int y2) {
unsigned int sq;
sq=(unsigned int)((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
// fprintf(stderr,"%s:%d movepixels: %u sq:%u\n",__FILE__,__LINE__,xc->config.movepixels,sq);
if (sq>xc->config.movepixels) return 1;
return 0;
}

static inline int isdoubleclick(struct xclient *xc, unsigned int button, unsigned int x, unsigned int y, Time stamp) {
// called on button press
struct halfclick_xclient *lhc;
struct click_xclient *lc;
lhc=&xc->surface.selection.lasthalfclick;
lc=&xc->surface.selection.lastclick;
#if 0
if (old > new) { // wraparound, old is near ~0 and new is very small
	if (~0-old+(new-1) > 500) return 0; // -1 is because 0 is skipped, so equiv to (new-old>502), ignore 2ms
}
#endif
if (stamp-lc->pressstamp>500) return 0; // this is vulnerable to false positives if a user waits 49 days between clicks
if (lhc->ispress) return 0;
if (lhc->buttonnumber!=button) return 0;
if (ismoved(xc,lhc->x,lhc->y,x,y)) return 0;
return 1;
}


static inline void uc4toutf8(unsigned int *destlen_out, unsigned char *dest, unsigned int value) {
/*
0x10FFFF : 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
0x00FFFF : 1110xxxx 10xxxxxx 10xxxxxx
0x0007FF : 110xxxxx 10xxxxxx
0x00007F : 0xxxxxxx
*/
if (value>0xFFFF) {
	dest[0]=128|64|32|16|(value>>18);
	dest[1]=128|(63&(value>>12));
	dest[2]=128|(63&(value>>6));
	dest[3]=128|(63&(value));
	*destlen_out=4;
} else if (value>0x7FF) {
	dest[0]=128|64|32|(value>>12);
	dest[1]=128|(63&(value>>6));
	dest[2]=128|(63&(value));
	*destlen_out=3;
} else if (value>0x7F) {
	dest[0]=128|64|(value>>6);
	dest[1]=128|(63&(value));
	*destlen_out=2;
} else {
	dest[0]=value&127;
	*destlen_out=1;
}
}
#if 0
static inline void valuetoutf8(unsigned char *dest, unsigned int *destlen_out, unsigned int value) {
// this is slightly faster than calling uc4toutf8
if (value&((1<<21)-1)&(~127)) { // lower 21 bits but not lowest 7
	value=value&((1<<21)-1); // 2097151
	dest[0]=(128|64|32|16)|(value>>18);
	dest[1]=(128)|((value&((1<<18)-1))>>12);
	dest[2]=(128)|((value&((1<<12)-1))>>6);
	dest[3]=(128)|((value&((1<<6)-1))>>0);
	*destlen_out=4;
} else {
	dest[0]=value&127;
	*destlen_out=1;
}
}
#endif

static inline int sendletters(struct vte *vte, unsigned char *letters, unsigned int len) {
int isdrop;
if (writeorqueue_vte(&isdrop,vte,letters,len)) GOTOERROR;
return 0;
error:
	return -1;
}

static uint32_t getuc4(KeySym ks, unsigned int mods) {
switch (ks) {
	case XK_BackSpace: return 127; // 0xff08
	case XK_Return: return 13; // 0xff0d 
	case XK_Escape: return 27; // 0xff1b
	case XK_Tab: return 9; // 0xff09
}
return getuc4_keysym(ks);
}

static int isignoredkey(KeySym ks) {
switch (ks) {
	case XK_Shift_L: // 0xffe1
	case XK_Shift_R: // 0xffe2
#if 0
	case XK_Meta_L: // 0xffe7
	case XK_Meta_R: // 0xffe8
	case XK_Alt_L: // 0xffe9
	case XK_Alt_R: // 0xffea
	case XK_Super_L: // 0xffeb
	case XK_Super_R: // 0xffec
	case XK_Hyper_L: // 0xffed
	case XK_Hyper_R: // 0xffee
#endif
		return 1;
}
return 0;
}

static int pause_handlekeypress(struct xclient *xc, XEvent *e) {
struct x11info *x=xc->baggage.x;
uint32_t uc4=0;
KeySym ks;
unsigned int mods;

mods=((e->xkey.state&(ShiftMask|LockMask))!=0);

ks=XkbKeycodeToKeysym(x->display,e->xkey.keycode,0,mods);
if (isignoredkey(ks)) return 0;

mods|= 2*((e->xkey.state&ControlMask)!=0);
mods|= 4*((e->xkey.state&Mod1Mask)!=0);
if (xc->config.isappcursor) mods|=16;
if (xc->scrollback.linesback) mods|= 32;

uc4=getuc4(ks,mods);
if ((!uc4)||(mods&4)) {
	if (xc->hooks.keysym(xc->baggage.script,(unsigned int)ks,mods)) GOTOERROR;
	return 0;
}

if (xc->scrollback.linesback) {
	switch (ks) {
		case XK_KP_Enter: case XK_Escape: case XK_Return:
			if (scrollback_xclient(xc,-xc->scrollback.linesback)) GOTOERROR;
			break;
		case XK_KP_Down: case XK_KP_Space: case XK_Down: case XK_space:
			if (scrollback_xclient(xc,-1)) GOTOERROR;
			break;
		case XK_KP_Up: case XK_Up:
			if (scrollback_xclient(xc,1)) GOTOERROR;
			break;
		default:
			if (xc->hooks.keysym(xc->baggage.script,(unsigned int)ks,mods)) GOTOERROR;
			break;
	}
	return 0;
}

if (mods&2) {
	if ( (uc4>='a') && (uc4<='z') ) {
		switch (uc4) {
			case 'q': if (xc->hooks.control_q(xc->baggage.script,'q')) GOTOERROR; break;
			case 's': if (xc->hooks.control_s(xc->baggage.script,'s')) GOTOERROR; break;
			default:
				if (xc->hooks.control(xc->baggage.script,uc4)) GOTOERROR;
		}
	} else {
		if (xc->hooks.keysym(xc->baggage.script,(unsigned int)ks,mods)) GOTOERROR;
	}
} else {
	if (xc->hooks.key(xc->baggage.script,uc4)) GOTOERROR;
}
return 0;
error:
	return -1;
}

static inline int islettersoverride(int *isdone_inout, struct vte *v, KeySym ks, unsigned int mods) {
char *letters=NULL;
int isapp;

if (mods&6) return 0;
isapp=v->keyboardstates.iskeypadappmode;
if (mods&8) isapp=0;
switch (ks) {
	case XK_KP_Space:		letters=(isapp)?"\eO ":" "; break;
	case XK_KP_Tab:			letters=(isapp)?"\eOI":"\t"; break;
	case XK_KP_Enter:		letters=(isapp)?"\eOM":"\n"; break;
	case XK_KP_F1:			letters="\eOP"; break;
	case XK_KP_F2:			letters="\eOQ"; break;
	case XK_KP_F3:			letters="\eOR"; break;
	case XK_KP_F4:			letters="\eOS"; break;
	case XK_KP_7:
	case XK_KP_Home:		letters=(isapp)?"\eOH":"7"; break;
	case XK_KP_4:
	case XK_KP_Left:		letters=(isapp)?"\e[D":"4"; break;
	case XK_KP_8:
	case XK_KP_Up:			letters=(isapp)?"\e[A":"8"; break;
	case XK_KP_6:
	case XK_KP_Right:		letters=(isapp)?"\e[C":"6"; break;
	case XK_KP_2:
	case XK_KP_Down:		letters=(isapp)?"\e[B":"2"; break;
//	case XK_KP_Prior:
	case XK_KP_9:
	case XK_KP_Page_Up:	letters=(isapp)?"\e[5~":"9"; break;
//	case XK_KP_Next:
	case XK_KP_3:
	case XK_KP_Page_Down:	letters=(isapp)?"\e[6~":"3"; break;
	case XK_KP_1:
	case XK_KP_End:			letters=(isapp)?"\eOF":"1"; break;
	case XK_KP_5:
	case XK_KP_Begin:		letters=(isapp)?"\e[E":"5"; break;
	case XK_KP_0:
	case XK_KP_Insert:	letters=(isapp)?"\e[2~":"0"; break;
	case XK_KP_Delete:	letters=(isapp)?"\e[3~":"."; break;
	case XK_KP_Equal:		letters=(isapp)?"\eOX":"="; break;
	case XK_KP_Multiply:	letters=(isapp)?"\eOj":"*"; break;
	case XK_KP_Add:			letters=(isapp)?"\eOk":"+"; break;
//	case XK_KP_Separator:	letters=","; break; // comment this out to send it to script to decide
	case XK_KP_Subtract:	letters=(isapp)?"\eOm":"-"; break;
	case XK_KP_Decimal:		letters="."; break;
	case XK_KP_Divide:		letters=(isapp)?"\eOo":"/"; break;
	default: return 0;
}
if (sendletters(v,(unsigned char *)letters,strlen(letters))) return -1;
*isdone_inout=1;
return 0;
}

static int handlekeyrelease(struct xclient *xc, XEvent *e) {
struct x11info *x=xc->baggage.x;
uint32_t uc4=0;
KeySym ks;
unsigned int mods;

#if 0
fprintf(stderr,"%s:%d keyrelease: keycode:%u state:%u\n",__FILE__,__LINE__,e->xkey.keycode,e->xkey.state);
#endif

mods=((e->xkey.state&(ShiftMask|LockMask))!=0);

ks=XkbKeycodeToKeysym(x->display,e->xkey.keycode,0,mods);
if (isignoredkey(ks)) return 0;

mods|= 2*((e->xkey.state&ControlMask)!=0);
mods|= 4*((e->xkey.state&Mod1Mask)!=0);
mods|= 8*((e->xkey.state&Mod2Mask)!=0);
if (xc->config.isappcursor) mods|=16;

uc4=getuc4(ks,mods);
if ((!uc4)||(mods&4)) {
	if (xc->hooks.unkeysym(xc->baggage.script,(unsigned int)ks,mods)) GOTOERROR;
}
return 0;
error:
	return -1;
}
static int handlekeypress(struct xclient *xc, XEvent *e) {
struct x11info *x=xc->baggage.x;
struct vte *vte=xc->baggage.vte;
uint32_t uc4=0;
KeySym ks;
unsigned int mods;

#if 0
fprintf(stderr,"%s:%d keypress: keycode:%u state:%u\n",__FILE__,__LINE__,e->xkey.keycode,e->xkey.state);
#endif

mods=((e->xkey.state&(ShiftMask|LockMask))!=0);

ks=XkbKeycodeToKeysym(x->display,e->xkey.keycode,0,mods);
if (isignoredkey(ks)) return 0;

mods|= 2*((e->xkey.state&ControlMask)!=0);
mods|= 4*((e->xkey.state&Mod1Mask)!=0);
mods|= 8*((e->xkey.state&Mod2Mask)!=0);
if (xc->config.isappcursor) mods|=16;

{
	int isdone=0;
	if (islettersoverride(&isdone,vte,ks,mods)) GOTOERROR;
	if (isdone) return 0;
}

uc4=getuc4(ks,mods);
if ((!uc4)||(mods&4)) {
	if (xc->hooks.keysym(xc->baggage.script,(unsigned int)ks,mods)) GOTOERROR;
	return 0;
}

if (mods&2) {
	if ( (uc4>='a') && (uc4<='z') ) {
		unsigned char ch;
		switch (uc4) {
			case 'q': if (xc->hooks.control_q(xc->baggage.script,'q')) GOTOERROR; break;
			case 's': if (xc->hooks.control_s(xc->baggage.script,'s')) GOTOERROR; break;
			default:
				ch=(unsigned char)(uc4-'a'+1);
				if (sendletters(vte,&ch,1)) GOTOERROR;
		}
	} else {
		if (xc->hooks.keysym(xc->baggage.script,(unsigned int)ks,mods)) GOTOERROR;
	}
} else {
	unsigned char buff4[4];
	unsigned int len;
	(void)uc4toutf8(&len,buff4,uc4);
	if (sendletters(vte,buff4,len)) GOTOERROR;
}
return 0;
error:
	return -1;
}

#if 0
static unsigned char *getpastebuffer(struct xclient *xc, unsigned int len) {
unsigned char *temp;
if (xc->paste.max_buffer>=len) return xc->paste.buffer;
len+=8192;
temp=realloc(xc->paste.buffer,len);
if (!temp) GOTOERROR;
xc->paste.buffer = xc->tofree.pastebuffer = temp;
xc->paste.max_buffer=len;
return temp;
error:
	return NULL;
}

static int dopaste(struct xclient *xc) {
unsigned int plen;
unsigned char *buffer;
int isdrop;
if (paste_xclient(&plen,xc,(unsigned char *)"PRIMARY",7,10)) GOTOERROR;
if (!plen) return 0;
if (!(buffer=getpastebuffer(xc,plen))) GOTOERROR;
if (getpaste_xclient(xc,buffer,plen)) GOTOERROR;
if (writeorqueue_vte(&isdrop,xc->baggage.vte,buffer,plen)) GOTOERROR;
if (isdrop) fprintf(stderr,"Paste text was too large to send (%u)\n",plen);
return 0;
error:
	return -1;
}
#endif

static inline unsigned int colfrompixel(struct xclient *xc, unsigned int x) {
unsigned int col;
col=(x-xc->config.xoff)/xc->config.cellw;
if (col>xc->config.columnsm1) return xc->config.columnsm1;
return col;
}
static inline unsigned int rowfrompixel(struct xclient *xc, unsigned int y) {
unsigned int row;
row=(y-xc->config.yoff)/xc->config.cellh;
if (row>xc->config.rowsm1) return xc->config.rowsm1;
return row;
}

static int setstartstop_selection(struct xclient *xc, unsigned int ox, unsigned int oy, unsigned int nx, unsigned int ny) {
return setselection_xclient(xc,RAW_MODE_SELECTION_SURFACE_XCLIENT, rowfrompixel(xc,oy), colfrompixel(xc,ox),
		rowfrompixel(xc,ny), colfrompixel(xc,nx));
}

static int distance_rowcol(struct xclient *xc, unsigned int row1, unsigned int col1, unsigned int row2, unsigned int col2) {
return (row2-row1)*xc->config.columns+(col2-col1);
}

static int isafter_rowcol(unsigned int row1, unsigned int col1, unsigned int row2, unsigned int col2) {
if (row1>row2) return 1;
if (row1<row2) return 0;
if (col1>col2) return 1;
return 0;
}

static inline int isbreak_value(uint32_t value) {
switch (value&0x1fffff) {
	case '.': case ';': case ':':
	case '\f': case '\v': case '\t': case '\r': case '\n': case ' ': return 1;
}
return 0;
}

static void selectword(unsigned int *left_out, unsigned int *right_out, uint32_t *backing, unsigned int columnsm1, unsigned int col) {
unsigned int left,right;

left=right=col;

if (!isbreak_value(backing[col])) {
	while (1) {
		if (!left) break;
		left--;
		if (isbreak_value(backing[left])) { left++; break; }
	}
	while (1) {
		if (right==columnsm1) break;
		right++;
		if (isbreak_value(backing[right])) { right--; break; }
	}
}

*left_out=left;
*right_out=right;
}

static void getleftright(unsigned int *left_out, unsigned int *right_out, struct xclient *xc, unsigned int mode,
		unsigned int row, unsigned int col) {
switch (mode) {
	case WORD_MODE_SELECTION_SURFACE_XCLIENT:
		(void)selectword(left_out,right_out,xc->surface.lines[row].backing,xc->config.columnsm1,col);
		break;
	case LINE_MODE_SELECTION_SURFACE_XCLIENT:
		*left_out=0; *right_out=xc->config.columnsm1;		
		break;
	default:
		*left_out = *right_out = col;
		break;
}
}

static int upgrade_selection(struct xclient *xc, unsigned int x, unsigned int y) {
unsigned int row,col,left,right;
col=colfrompixel(xc,x);
row=rowfrompixel(xc,y);
switch (xc->surface.selection.mode) {
	case NONE_MODE_SELECTION_SURFACE_XCLIENT:
	case RAW_MODE_SELECTION_SURFACE_XCLIENT:
		(void)getleftright(&left,&right,xc,WORD_MODE_SELECTION_SURFACE_XCLIENT,row,col);
		if (setselection_xclient(xc,WORD_MODE_SELECTION_SURFACE_XCLIENT,row,left,row,right)) GOTOERROR;
		break;
	case WORD_MODE_SELECTION_SURFACE_XCLIENT:
		(void)getleftright(&left,&right,xc,LINE_MODE_SELECTION_SURFACE_XCLIENT,row,col);
		if (setselection_xclient(xc,LINE_MODE_SELECTION_SURFACE_XCLIENT,row,left,row,right)) GOTOERROR;
		break;
	case LINE_MODE_SELECTION_SURFACE_XCLIENT:
		if (setselection_xclient(xc,ALL_MODE_SELECTION_SURFACE_XCLIENT,0,0,xc->config.rowsm1,xc->config.columnsm1)) GOTOERROR;
		break;
	case ALL_MODE_SELECTION_SURFACE_XCLIENT:
		if (clearandredrawselection(xc)) GOTOERROR;
		break;
}
return 0;
error:
	return -1;
}

static int refine_selection(struct xclient *xc, unsigned int x, unsigned int y) {
unsigned int row,col,left,right;
unsigned int mode;

// if (!xc->surface.selection.mode) return 0; // this shouldn't happen
mode=xc->surface.selection.mode;
if (mode==ALL_MODE_SELECTION_SURFACE_XCLIENT) mode=RAW_MODE_SELECTION_SURFACE_XCLIENT;

col=colfrompixel(xc,x);
row=rowfrompixel(xc,y);
(void)getleftright(&left,&right,xc,xc->surface.selection.mode,row,col);

if (isafter_rowcol(row,col, xc->surface.selection.stop.row, xc->surface.selection.stop.col)) {
	return setselection_xclient(xc,mode, xc->surface.selection.start.row, xc->surface.selection.start.col, row,right);
} else if (isafter_rowcol(xc->surface.selection.start.row, xc->surface.selection.start.col,row,col)) {
	return setselection_xclient(xc,mode, row,left,xc->surface.selection.stop.row, xc->surface.selection.stop.col);
} else {
	if (distance_rowcol(xc,xc->surface.selection.start.row, xc->surface.selection.start.col,row,col)
			< distance_rowcol(xc,row,col,xc->surface.selection.stop.row, xc->surface.selection.stop.col)) {
		return setselection_xclient(xc,mode, row,left,xc->surface.selection.stop.row, xc->surface.selection.stop.col);
	} else {
		return setselection_xclient(xc,mode, xc->surface.selection.start.row, xc->surface.selection.start.col,row,right);
	}
}
return 0;
}

#define PRESS_TYPE_BUTTONHOOK	1
#define MOVE_TYPE_BUTTONHOOK	2
#define CLICK_TYPE_BUTTONHOOK	3
#define RELEASE_TYPE_BUTTONHOOK	4
static int buttonhook(struct xclient *xc, unsigned int button, unsigned int type, XButtonEvent *e) {
// ismoved()=>MOVE_TYPE, _RELEASE_TYPE=>ismoved(), CLICK_=>!ismoved()
unsigned int mods;
mods=((e->state&(ShiftMask|LockMask))!=0);
mods|= 2*((e->state&ControlMask)!=0);
mods|= 4*((e->state&Mod1Mask)!=0);
if (xc->scrollback.linesback) mods|= 32;
if (xc->ismousegrabbed) mods|=64;
if (xc->hooks.pointer(xc->baggage.script,type,mods,button,rowfrompixel(xc,e->y),colfrompixel(xc,e->x))) GOTOERROR;
return 0;
error:
	return -1;
}

static int handlebutton1update(struct xclient *xc, unsigned int x, unsigned int y, Time stamp) {
// motion should set stamp=CurrentTime (reserved by X, ==0)
struct halfclick_xclient *lhc;
struct click_xclient *lc;

lhc=&xc->surface.selection.lasthalfclick;
lc=&xc->surface.selection.lastclick;
if (lhc->ispress && (lhc->buttonnumber==1)) {
	if (ismoved(xc,lhc->x,lhc->y,x,y)) {
		if (xc->surface.selection.mode<2) {
			if (setstartstop_selection(xc,lhc->x,lhc->y,x,y)) GOTOERROR;
		} else if (xc->surface.selection.mode!=ALL_MODE_SELECTION_SURFACE_XCLIENT) {
			if (refine_selection(xc,x,y)) GOTOERROR;
		}
	} else {
		(ignore)setpointer_xclient(xc,0);
	}
	if (stamp!=CurrentTime) { lc->buttonnumber=1; lc->x=x; lc->y=y; lc->pressstamp=lhc->stamp; }
}
if (stamp!=CurrentTime) { lhc->ispress=0; lhc->buttonnumber=1; lhc->x=x; lhc->y=y; lhc->stamp=stamp; }
return 0;
error:
	return -1;
}

static int handlebutton3update(struct xclient *xc, unsigned int x, unsigned int y, Time stamp, XButtonEvent *e) {
struct halfclick_xclient *lhc;
struct click_xclient *lc;

lhc=&xc->surface.selection.lasthalfclick;
lc=&xc->surface.selection.lastclick;

if (lhc->ispress && (lhc->buttonnumber==3)) {
	if (lc->buttonnumber==1) {
		if (!xc->surface.selection.mode) {
			if (setstartstop_selection(xc,lc->x,lc->y,x,y)) GOTOERROR;
		} else {
			if (refine_selection(xc,x,y)) GOTOERROR;
		}
	} else {
		if ((lc->buttonnumber==3)&&(xc->surface.selection.mode)) {
			if (refine_selection(xc,x,y)) GOTOERROR;
		} else if (e) {
			if (buttonhook(xc,3,CLICK_TYPE_BUTTONHOOK,e)) GOTOERROR;
		}
	}
	if (stamp!=CurrentTime) { lc->buttonnumber=3; lc->x=x; lc->y=y; lc->pressstamp=lhc->stamp; }
}
if (stamp!=CurrentTime) { lhc->ispress=0; lhc->buttonnumber=3; lhc->x=x; lhc->y=y; lhc->stamp=stamp; }
return 0;
error:
	return -1;
}

static int grabbed_handlebuttonrelease(struct xclient *xc, XButtonEvent *e) {
struct halfclick_xclient *lhc;

lhc=&xc->surface.selection.lasthalfclick;

if (buttonhook(xc,(unsigned int)e->button,RELEASE_TYPE_BUTTONHOOK,e)) GOTOERROR;
if (lhc->ispress && (lhc->buttonnumber==e->button) && (!ismoved(xc,lhc->x,lhc->y,e->x,e->y))) {
	struct click_xclient *lc;
	lc=&xc->surface.selection.lastclick;
	lc->buttonnumber=e->button; lc->x=e->x; lc->y=e->y; lc->pressstamp=e->time;
	if (buttonhook(xc,(unsigned int)e->button,CLICK_TYPE_BUTTONHOOK,e)) GOTOERROR;
}
lhc->ispress=0; lhc->buttonnumber=e->button; lhc->x=e->x; lhc->y=e->y; lhc->stamp=e->time;

return 0;
error:
	return -1;
}

static int handlebuttonrelease(struct xclient *xc, XButtonEvent *e) {
if (xc->ismousegrabbed) return grabbed_handlebuttonrelease(xc,e);
switch (e->button) {
	case 1:
		if (handlebutton1update(xc,e->x,e->y,e->time)) GOTOERROR;
		break;
	case 3:
		if (handlebutton3update(xc,e->x,e->y,e->time,e)) GOTOERROR;
		break;
	default:
		return grabbed_handlebuttonrelease(xc,e);
}
return 0;
error:
	return -1;
}

static int grabbed_handlebuttonpress(struct xclient *xc, XButtonEvent *e) {
struct halfclick_xclient *lhc;

lhc=&xc->surface.selection.lasthalfclick;
lhc->ispress=1; lhc->buttonnumber=e->button; lhc->x=e->x; lhc->y=e->y; lhc->stamp=e->time;
if (buttonhook(xc,(unsigned int)e->button,PRESS_TYPE_BUTTONHOOK,e)) GOTOERROR;
return 0;
error:
	return -1;
}

static int handlebuttonpress(struct xclient *xc, XButtonEvent *e) {
struct halfclick_xclient *lhc;
struct click_xclient *lc;

if (xc->ismousegrabbed) return grabbed_handlebuttonpress(xc,e);

lhc=&xc->surface.selection.lasthalfclick;
lc=&xc->surface.selection.lastclick;

switch (e->button) {
	case 1: // left
		if (isdoubleclick(xc,1,e->x,e->y,e->time)) {
			if (upgrade_selection(xc,e->x,e->y)) GOTOERROR;
		} else if (xc->surface.selection.mode) {
			if (lhc->ispress && (lhc->buttonnumber==3)) {
				if (buttonhook(xc,2,CLICK_TYPE_BUTTONHOOK,e)) GOTOERROR; // simulate middle
			} else {
				if (clearandredrawselection(xc)) GOTOERROR;
			}
		} else {
			(ignore)setpointer_xclient(xc,152);
		}
		lhc->ispress=1; lhc->buttonnumber=1; lhc->x=e->x; lhc->y=e->y; lhc->stamp=e->time;
		break;
	case 3: // right
		if (xc->surface.selection.mode) { // if no selection, send to python
			if (lhc->ispress && (lhc->buttonnumber==1)) {
				if (buttonhook(xc,2,CLICK_TYPE_BUTTONHOOK,e)) GOTOERROR; // simulate middle
			} else if (!lhc->ispress) {
				if (isdoubleclick(xc,3,e->x,e->y,e->time)) {
					xc->surface.selection.mode+=1;
					if (xc->surface.selection.mode==5) xc->surface.selection.mode=1;
				}
				if (refine_selection(xc,e->x,e->y)) GOTOERROR;
			}
			lhc->ispress=1; lhc->buttonnumber=3; lhc->x=e->x; lhc->y=e->y; lhc->stamp=e->time;
			break;
		} else if (lc->buttonnumber==1) {
			if (setstartstop_selection(xc,lc->x,lc->y,e->x,e->y)) GOTOERROR;
			lhc->ispress=1; lhc->buttonnumber=3; lhc->x=e->x; lhc->y=e->y; lhc->stamp=e->time;
			break;
		}
// no break
	default:
		return grabbed_handlebuttonpress(xc,e);
}
return 0;
error:
	return -1;
}

static int grabbed_handlemotion(struct xclient *xc, XMotionEvent *e) {
XButtonEvent xb;
unsigned int button=0;

xb.state=e->state;
xb.x=e->x;
xb.y=e->y;
switch (e->state) {
	case 0x100: button=1; break;
	case 0x400: button=3; break;
}
if (buttonhook(xc,button,MOVE_TYPE_BUTTONHOOK,&xb)) GOTOERROR;
return 0;
error:
	return -1;
}

static int handlemotion(struct xclient *xc, XMotionEvent *e) {
if (xc->ismousegrabbed) return grabbed_handlemotion(xc,e);
switch (e->state) {
	case 0x100: // Button1
		if (handlebutton1update(xc,e->x,e->y,CurrentTime)) GOTOERROR;
		break;
	case 0x400: // Button3
		if (handlebutton3update(xc,e->x,e->y,CurrentTime,NULL)) GOTOERROR;
		break;
	// our event mask prevents any other motion events
}
return 0;
error:
	return -1;
}

static int getcolor(XColor *xcolor_inout, struct x11info *x, unsigned short r, unsigned short g, unsigned short b) {
XColor xc;
if (xcolor_inout->flags) (ignore)XFreeColors(x->display,x->colormap,&xcolor_inout->pixel,1,0);
xc.red=r;
xc.green=g;
xc.blue=b;
xc.flags=DoRed|DoGreen|DoBlue;
if (!XAllocColor(x->display,x->colormap,&xc)) GOTOERROR;
*xcolor_inout=xc;
return 0;
error:
	return -1;
}

static int setxcolors(struct xclient *xc, struct vte *vte) {
struct x11info *x=xc->baggage.x;
unsigned int ui;
for (ui=0;ui<16;ui++) {
	struct rgb_vte *rgb;
	unsigned short r,g,b;
	rgb=&vte->colors[ui];
	r=(rgb->r<<8)|rgb->r;
	g=(rgb->g<<8)|rgb->g;
	b=(rgb->b<<8)|rgb->b;
	if (getcolor(xc->xcolors+ui,x,r,g,b)) GOTOERROR;
	if (setcolor_xftchar(xc->baggage.xftchar,ui,r,g,b)) GOTOERROR;
}
return 0;
error:
	return -1;
}

static int noop_hook(void *v) { return 0; }
static int noop2_hook(void *v, int ign) { return 0; }
static int noop3_hook(void *v, char *ign2, unsigned int ign) { return 0; }
static int noop4_hook(void *v, unsigned int ign2, unsigned int ign) { return 0; }
static int noop5_hook(void *v,unsigned int ign5,unsigned int ign4,unsigned int ign3,unsigned int ign2,unsigned int ign) { return 0; }
static char *noopstrpuint_hook(unsigned int *p, void *v) { return NULL; }

static inline void init_hooks(struct xclient *xc) {
xc->hooks.control_s=noop2_hook;
xc->hooks.control_q=noop2_hook;
xc->hooks.control=noop2_hook;
xc->hooks.key=noop2_hook;
xc->hooks.bell=noop_hook;
xc->hooks.alarmcall=noop2_hook;
xc->hooks.getinsertion=noopstrpuint_hook;
xc->hooks.checkinsertion=noop_hook;
xc->hooks.message=noop3_hook;
xc->hooks.keysym=noop4_hook;
xc->hooks.unkeysym=noop4_hook;
xc->hooks.onresize=noop4_hook;
xc->hooks.pointer=noop5_hook;
}

int init_xclient(struct xclient *xc, struct config *config, struct x11info *x, struct xftchar *xftchar,
		struct charcache *charcache, struct texttap *texttap, struct pty *pty, struct all_event *events, struct vte *vte,
		struct cursor *cursor, struct xclipboard *xclipboard, void *script) {
uint32_t blankval;
unsigned int rows,columns;

xc->config.xwidth=config->xwidth;
xc->config.xheight=config->xheight;
xc->config.xoff=config->xoff;
xc->config.yoff=config->yoff;
xc->config.cellw=config->cellw;
xc->config.cellh=config->cellh;
xc->config.cursorheight=config->cursorheight;
xc->config.cursoryoff=config->cursoryoff;
xc->config.isblink=config->isblinkcursor;
columns = xc->config.columns=config->columns;
if (columns<2) columns=2; // actual limit unknown, presumably we'll crash on some optimized loops if 0
xc->config.columnsm1=columns-1;
rows = xc->config.rows=config->rows;
if (rows<2) rows=2; // actual limit unknown
xc->config.rowsm1=rows-1;
xc->config.rowwidth=config->rowwidth;
xc->config.colheight=config->colheight;
xc->config.scrollbackcount=config->scrollbackcount;
if (xc->config.scrollbackcount==1) xc->config.scrollbackcount=2; // 1 => crashy crashy
xc->config.movepixels=(x->defscreen.height*x->defscreen.height) / (x->defscreen.heightmm*x->defscreen.heightmm);
xc->config.isautorepeat=1;

xc->baggage.x=x;
xc->baggage.xftchar=xftchar;
xc->baggage.charcache=charcache;
xc->baggage.texttap=texttap;
xc->baggage.pty=pty;
xc->baggage.events=events;
xc->baggage.vte=vte;
xc->baggage.cursor=cursor;
xc->baggage.script=script;
xc->baggage.config=config;
xc->baggage.xclipboard=xclipboard;

#if 0
blankval=32|vte->curbgcolor->bgvaluemask;
#else
blankval=32|vte->curbgcolor->bgvaluemask|vte->curfgcolor->fgvaluemask;
#endif

(void)init_hooks(xc);

if (init_surface_xclient(&xc->surface,rows,columns,blankval,xc->config.scrollbackcount)) GOTOERROR;

if (setxcolors(xc,vte)) GOTOERROR;
return 0;
error:
	return -1;
}

void deinit_xclient(struct xclient *xc) {
deinit_surface_xclient(&xc->surface);
// iffree(xc->tofree.pastebuffer);
if (xc->baggage.x) {
	struct x11info *x=xc->baggage.x;
	unsigned int ui;
	for (ui=0;ui<16;ui++) {
		if (!xc->xcolors[ui].flags) continue;
		(ignore)XFreeColors(x->display,x->colormap,&xc->xcolors[ui].pixel,1,0);
	}
}
}

static void drawvalue(Pixmap dest, struct xftchar *xftchar, struct vte *vte, unsigned int value) {
// unsigned char utf8[4];
// unsigned int utf8len;
unsigned int fgindex,bgindex,ucs4;

// (void)valuetoutf8(utf8,&utf8len,value);
fgindex=(value>>25)&0xf;
bgindex=(value>>21)&0xf;
ucs4=value&UCS4_MASK_VALUE;
(void)drawchar2_xftchar(dest,xftchar,ucs4,bgindex,fgindex,(value&UNDERLINEBIT_VALUE));
}

static inline void paintpixmap(struct xclient *xc, uint32_t *backing, unsigned int yoff, unsigned int col,
		unsigned int value, Pixmap pixmap) {
struct x11info *x=xc->baggage.x;
unsigned int cellw,cellh;
if (backing[col]==value) return;
cellw=xc->config.cellw;
cellh=xc->config.cellh;
backing[col]=value;
if (!xc->isnodraw)
	XCopyArea(x->display,pixmap,x->window,x->context,0,0,cellw,cellh,xc->config.xoff+col*cellw,xc->config.yoff+yoff);
}

static Pixmap getpixmap(struct xclient *xc, unsigned int value) {
struct xftchar *xftchar=xc->baggage.xftchar;
struct charcache *charcache=xc->baggage.charcache;
struct vte *vte=xc->baggage.vte;
Pixmap pixmap;

pixmap=find_charcache(charcache,value);
if (!pixmap) {
	struct one_charcache *occ;
	occ=add_charcache(charcache,value);
	if (!occ) GOTOERROR;
	pixmap=occ->pixmap;
	(void)drawvalue(occ->pixmap, xftchar,vte,value);
}
return pixmap;
error:
	return 0;
}

static int paintvalue(struct xclient *xc, unsigned int row, unsigned int col, unsigned int value) {
uint32_t *backing;
Pixmap pixmap;

backing=xc->surface.lines[row].backing;
if (!(pixmap=getpixmap(xc,value))) GOTOERROR;
(void)paintpixmap(xc,backing,row*xc->config.cellh,col,value,pixmap);
// fprintf(stderr,"%s:%d painted value %u (%u) to %u[%u]\n",__FILE__,__LINE__,value,value&0xff,row,col);
return 0;
error:
	return -1;
}

static int addchar_draw(struct xclient *xc, struct one_event *e) {
xc->status.lastaddchar.row=e->addchar.row;
xc->status.lastaddchar.col=e->addchar.col;
#if 0
if (!e->addchar.col) fprintf(stderr,"%s:%d addchar(%u,%u)\n",__FILE__,__LINE__,e->addchar.row,e->addchar.col);
#endif
return paintvalue(xc,e->addchar.row,e->addchar.col,e->addchar.value);
}
int addchar_xclient(struct xclient *xc, uint32_t value, unsigned int row, unsigned int col) {
return paintvalue(xc,row,col,value);
}

static int eraseinline_draw(struct xclient *xc, struct one_event *e) {
struct x11info *x=xc->baggage.x;
unsigned int value,row,col,colcount,rowcount;
unsigned long fillcolor;
uint32_t *backing;
unsigned int cellw,cellh;

cellw=xc->config.cellw;
cellh=xc->config.cellh;

value=e->eraseinline.value;
row=e->eraseinline.row;
col=e->eraseinline.col;
colcount=e->eraseinline.colcount;
rowcount=e->eraseinline.rowcount;

fillcolor=xc->xcolors[(value>>21)&0xf].pixel;
#if 0
fprintf(stderr,"%s:%d row:%u rowcount:%u col:%u colcount:%u\n",__FILE__,__LINE__,row,rowcount,col,colcount);
fillcolor=xc->xcolors[12]; // TODO
fprintf(stderr,"%s:%d %u,%u,%u,%u\n",__FILE__,__LINE__,
		xc->config.xoff+col*cellw,xc->config.yoff+row*cellh, colcount*cellw,rowcount*cellh);
#endif
if (!xc->isnodraw) {
	if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
	if (!XFillRectangle(x->display,x->window,x->context,xc->config.xoff+col*cellw,xc->config.yoff+row*cellh,
			colcount*cellw,rowcount*cellh)) GOTOERROR;
}

while (1) {
	backing=xc->surface.lines[row].backing;
	memset4(backing+col,value,colcount);
	rowcount--;
	if (!rowcount) break;
	row++;
}

return 0;
error:
	return -1;
}
static int setcursor(struct xclient *xc, unsigned int row, unsigned int col) {
uint32_t value;
Pixmap pixmap;

value=xc->surface.lines[row].backing[col];
if (!(pixmap=getpixmap(xc,value))) GOTOERROR;
if (set_cursor(xc->baggage.cursor,value,pixmap,row,col)) GOTOERROR;
return 0;
error:
	return -1;
}
static int setcursor_draw(struct xclient *xc, struct one_event *e) {
if (xc->isnodraw) {
	xc->baggage.cursor->col=e->setcursor.col;
	xc->baggage.cursor->row=e->setcursor.row;
	return 0;
}
return setcursor(xc,e->setcursor.row,e->setcursor.col);
}
static void scrollbackline(struct xclient *xc, struct line_xclient *line) {
// this assumes xc->config.scrollbackcount!=1
struct sbline_xclient *sb,*node;
unsigned int *backing;

#if 0
fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,__FUNCTION__);
#endif

if ((sb=xc->surface.scrollback.firstfree)) {
	xc->surface.scrollback.firstfree=sb->next;
	if (!xc->surface.scrollback.first) {
		xc->surface.scrollback.first=xc->surface.scrollback.last=sb;
		sb->previous=sb->next=NULL;
// fprintf(stderr,"%s:%d sb->backing:%p\n",__FILE__,__LINE__,sb->backing);
// fprintf(stderr,"%s:%d line->backing:%p\n",__FILE__,__LINE__,line->backing);
		backing=sb->backing;
		sb->backing=line->backing;
		line->backing=backing;
		return;
	}
} else if ((sb=xc->surface.scrollback.last)) {
	node=sb->previous;
	node->next=NULL;
	xc->surface.scrollback.last=node;
} else return; // no scrollback

node=xc->surface.scrollback.first;
sb->next=node;
sb->previous=NULL;
node->previous=sb;
xc->surface.scrollback.first=sb;

// fprintf(stderr,"%s:%d sb->backing:%p\n",__FILE__,__LINE__,sb->backing);
// fprintf(stderr,"%s:%d line->backing:%p\n",__FILE__,__LINE__,line->backing);
backing=sb->backing;
sb->backing=line->backing;
line->backing=backing;

// sb->len=xc->config.columns;
}

static inline int scroll1up(struct xclient *xc, unsigned int toprow, unsigned int bottomrow, uint32_t erasevalue) {
struct x11info *x=xc->baggage.x;
unsigned int numrows;
struct line_xclient line;
unsigned int yoff,yoff2,xoff;
unsigned long fillcolor;
unsigned int cellh,rowwidth;

cellh=xc->config.cellh;
rowwidth=xc->config.rowwidth;

fillcolor=xc->xcolors[(erasevalue>>21)&0xf].pixel;

numrows=bottomrow-toprow;
yoff2=toprow*cellh+xc->config.yoff;
yoff=bottomrow*cellh+xc->config.yoff;
xoff=xc->config.xoff;

if (!xc->isnodraw)
	XCopyArea(x->display,x->window,x->window,x->context,xoff,yoff2+cellh,rowwidth,yoff-yoff2,xoff,yoff2);

line=xc->surface.lines[toprow];
if (!toprow) (void)scrollbackline(xc,&line);
memmove(xc->surface.lines+toprow,xc->surface.lines+toprow+1,numrows*sizeof(struct line_xclient));
xc->surface.lines[bottomrow]=line;
if (!xc->isnodraw) {
	if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
	if (!XFillRectangle(x->display,x->window,x->context,xoff,yoff,rowwidth,cellh)) GOTOERROR;
}
memset4(line.backing,erasevalue,xc->surface.numinline);

#if 0
if (redrawrect(xc,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,xc->config.cellh*xc->config.rows)) GOTOERROR;
fprintf(stderr,"%s:%d scroll1up toprow: %u bottomrow: %u, erasevalue: 0x%02x\n",__FILE__,__LINE__,toprow,bottomrow,erasevalue);
#endif
return 0;
error:
	return -1;
}
static inline int scrollup(struct xclient *xc, unsigned int toprow, unsigned int bottomrow, uint32_t erasevalue, unsigned int scrollcount) {
struct x11info *x=xc->baggage.x;
unsigned int numrows,linestomove;
struct line_xclient *ptopline;
unsigned int topyoff,xoff;
unsigned long fillcolor;
unsigned int cellh,rowwidth,ui;

cellh=xc->config.cellh;
rowwidth=xc->config.rowwidth;

fillcolor=xc->xcolors[(erasevalue>>21)&0xf].pixel;

numrows=bottomrow-toprow+1;
if (numrows < scrollcount) {
	scrollcount=numrows;
	linestomove=0;
} else linestomove=numrows-scrollcount;
topyoff=toprow*cellh+xc->config.yoff;
xoff=xc->config.xoff;

if (linestomove) {
	if (!xc->isnodraw)
		XCopyArea(x->display,x->window,x->window,x->context,xoff,topyoff+scrollcount*cellh,rowwidth,linestomove*cellh,xoff,topyoff);
}

ptopline=xc->surface.lines+toprow;
if (!toprow) for (ui=0;ui<scrollcount;ui++) (void)scrollbackline(xc,&ptopline[ui]);
memcpy(xc->surface.sparelines,ptopline,scrollcount*sizeof(*ptopline));
memmove(ptopline,ptopline+scrollcount,linestomove*sizeof(*ptopline));
memcpy(ptopline+linestomove,xc->surface.sparelines,scrollcount*sizeof(*ptopline));
if (!xc->isnodraw) {
	if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
	if (!XFillRectangle(x->display,x->window,x->context,xoff,topyoff+linestomove*cellh,rowwidth,scrollcount*cellh)) GOTOERROR;
}
{
	unsigned int *backing;
	unsigned int firstblankrow;
	firstblankrow=toprow+linestomove;
	for (ui=0;ui<scrollcount;ui++) {
		backing=xc->surface.lines[firstblankrow+ui].backing;
		memset4(backing,erasevalue,xc->surface.numinline);
	}
}

#if 0
fprintf(stderr,"%s:%d scrollup toprow: %u bottomrow: %u, erasevalue: 0x%02x, scrollcount: %u\n",__FILE__,__LINE__,toprow,bottomrow,erasevalue,scrollcount);
#endif
return 0;
error:
	return -1;
}

static int scroll1up_draw(struct xclient *xc, struct one_event *e) {
return scroll1up(xc,e->scroll1up.toprow,e->scroll1up.bottomrow,e->scroll1up.erasevalue);
}
static int scrollup_draw(struct xclient *xc, struct one_event *e) {
return scrollup(xc,e->scrollup.toprow,e->scrollup.bottomrow,e->scrollup.erasevalue,e->scrollup.count);
}

static inline int scroll1down(struct xclient *xc, unsigned int toprow, unsigned int bottomrow, uint32_t erasevalue) {
struct x11info *x=xc->baggage.x;
unsigned int linestomove;
struct line_xclient bottomline,*ptopline;
unsigned int topyoff,xoff;
unsigned long fillcolor;
unsigned int cellh,rowwidth;

cellh=xc->config.cellh;
rowwidth=xc->config.rowwidth;

fillcolor=xc->xcolors[(erasevalue>>21)&0xf].pixel;

linestomove=bottomrow-toprow;
topyoff=toprow*cellh+xc->config.yoff;
xoff=xc->config.xoff;

if (!xc->isnodraw)
	XCopyArea(x->display,x->window,x->window,x->context,xoff,topyoff,rowwidth,linestomove*cellh,xoff,topyoff+cellh);

ptopline=xc->surface.lines+toprow;
bottomline=xc->surface.lines[bottomrow];
memmove(ptopline+1,ptopline,linestomove*sizeof(*ptopline));
xc->surface.lines[toprow]=bottomline;
if (!xc->isnodraw) {
	if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
	if (!XFillRectangle(x->display,x->window,x->context,xoff,topyoff,rowwidth,cellh)) GOTOERROR;
}
memset4(bottomline.backing,erasevalue,xc->surface.numinline);

#if 0
if (redrawrect(xc,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,xc->config.cellh*xc->config.rows)) GOTOERROR;
fprintf(stderr,"%s:%d scroll1down toprow: %u bottomrow: %u, erasevalue: 0x%02x\n",__FILE__,__LINE__,toprow,bottomrow,erasevalue);
#endif
return 0;
error:
	return -1;
}
static inline int scrolldown(struct xclient *xc, unsigned int toprow, unsigned int bottomrow, uint32_t erasevalue, unsigned int scrollcount) {
struct x11info *x=xc->baggage.x;
unsigned int numrows,linestomove;
struct line_xclient *ptopline;
unsigned int topyoff,xoff;
unsigned long fillcolor;
unsigned int cellh,rowwidth;

cellh=xc->config.cellh;
rowwidth=xc->config.rowwidth;

fillcolor=xc->xcolors[(erasevalue>>21)&0xf].pixel;

numrows=bottomrow-toprow+1;
if (numrows < scrollcount) {
	scrollcount=numrows;
	linestomove=0;
} else linestomove=numrows-scrollcount;
topyoff=toprow*cellh+xc->config.yoff;
xoff=xc->config.xoff;
if (linestomove) {
	if (!xc->isnodraw)
		XCopyArea(x->display,x->window,x->window,x->context,xoff,topyoff,rowwidth,linestomove*cellh,xoff,topyoff+scrollcount*cellh);
}

ptopline=xc->surface.lines+toprow;
memcpy(xc->surface.sparelines,ptopline+linestomove,scrollcount*sizeof(*ptopline));
memmove(ptopline+scrollcount,ptopline,linestomove*sizeof(*ptopline));
memcpy(ptopline,xc->surface.sparelines,scrollcount*sizeof(*ptopline));
if (!xc->isnodraw) {
	if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
	if (!XFillRectangle(x->display,x->window,x->context,xoff,topyoff,rowwidth,scrollcount*cellh)) GOTOERROR;
}
{
	unsigned int *backing;
	unsigned int ui;
	for (ui=0;ui<scrollcount;ui++) {
		backing=xc->surface.lines[toprow+ui].backing;
		memset4(backing,erasevalue,xc->surface.numinline);
	}
}

#if 0
fprintf(stderr,"scrolldown toprow: %u bottomrow: %u, erasevalue: 0x%02x, scrollcount:%u\n",toprow,bottomrow,erasevalue,scrollcount);
XSync(x->display,False);
if (redrawrect(xc,0,0,xc->config.xwidth,xc->config.xheight)) GOTOERROR;
XSync(x->display,False);
sleep(2);
#endif
return 0;
error:
	return -1;
}

static inline int scroll1down_draw(struct xclient *xc, struct one_event *e) {
return scroll1down(xc,e->scroll1down.toprow,e->scroll1down.bottomrow,e->scroll1down.erasevalue);
}

static inline int scrolldown_draw(struct xclient *xc, struct one_event *e) {
return scrolldown(xc,e->scrolldown.toprow,e->scrolldown.bottomrow,e->scrolldown.erasevalue,e->scrolldown.count);
}

#if 0
static inline int insertline_draw(struct xclient *xc, struct one_event *e) {
#if 0
fprintf(stderr,"%s:%d %s row:%u count:%u\n",__FILE__,__LINE__,__FUNCTION__,e->insertline.row,e->insertline.count);
#endif
return scrolldown(xc,e->insertline.row,xc->config.rowsm1,e->insertline.erasevalue,e->insertline.count);
}
static inline int deleteline_draw(struct xclient *xc, struct one_event *e) {
return scrollup(xc,e->deleteline.row,xc->config.rowsm1,e->deleteline.erasevalue,e->deleteline.count);
}
#endif

static int dch_draw(struct xclient *xc, struct one_event *e) {
struct x11info *x=xc->baggage.x;
unsigned int row,col,value,count;
uint32_t *backing;
Pixmap pixmap;
unsigned int yoff,xoff;
unsigned int cellh,cellw,columns,shiftcells;

cellh=xc->config.cellh;
cellw=xc->config.cellw;
columns=xc->config.columns;

row=e->dch.row;
col=e->dch.col;
value=e->dch.erasevalue;
count=e->dch.count;
backing=xc->surface.lines[row].backing;
yoff=row*cellh+xc->config.yoff;
xoff=xc->config.xoff;
shiftcells=columns-col-count;

memmove(backing+col,backing+col+count,shiftcells*sizeof(*backing));
if (!xc->isnodraw)
	XCopyArea(x->display,x->window,x->window,x->context,xoff+(col+count)*cellw,yoff,shiftcells*cellw,cellh,
			xoff+col*cellw,yoff);

col=columns-count;

if (!(pixmap=getpixmap(xc,value))) GOTOERROR;
while (1) {
	(void)paintpixmap(xc,backing,yoff,col,value,pixmap);
	col++;
	if (col==columns) break;
}

// fprintf(stderr,"dch row: %u col: %u, count: %u, erasevalue: 0x%02x\n",row,col,count,value);
return 0;
error:
	return -1;
}
static int ich_draw(struct xclient *xc, struct one_event *e) {
struct x11info *x=xc->baggage.x;
unsigned int row,col,value,count;
uint32_t *backing;
Pixmap pixmap;
unsigned int yoff,xoff;
unsigned int cellh,cellw,columns,lastcol,shiftcells;

cellh=xc->config.cellh;
cellw=xc->config.cellw;
columns=xc->config.columns;

row=e->ich.row;
col=e->ich.col;
value=e->ich.erasevalue;
count=e->ich.count;
backing=xc->surface.lines[row].backing;
yoff=row*cellh+xc->config.yoff;
xoff=xc->config.xoff;
shiftcells=columns-col-count;
lastcol=col+count;

memmove(backing+col+count,backing+col,shiftcells*sizeof(*backing));
if (!xc->isnodraw)
	XCopyArea(x->display,x->window,x->window,x->context,xoff+col*cellw,yoff,shiftcells*cellw,cellh,
			xoff+(col+count)*cellw,yoff);

if (!(pixmap=getpixmap(xc,value))) GOTOERROR;
while (1) {
	(void)paintpixmap(xc,backing,yoff,col,value,pixmap);
	col++;
	if (col==lastcol) break;
}

// fprintf(stderr,"ich row: %u col: %u, count: %u, erasevalue: 0x%02x\n",row,col,count,value);
return 0;
error:
	return -1;
}

static int title_draw(struct xclient *xc, struct one_event *e) {
struct x11info *x=xc->baggage.x;
XStoreName(x->display,x->window,e->title.name);
return 0;
}

static int generic_draw(struct xclient *xc, struct one_event *e) {
struct vte *v=xc->baggage.vte;
char *buffer;
int i;
buffer=e->generic.str;
i=strlen(buffer);
if (i) (ignore)writeorqueue_vte(&i,v,(unsigned char *)buffer,(unsigned int)i);
return 0;
}

static inline int message_draw(struct xclient *xc, struct one_event *e) {
(ignore)xc->hooks.message(xc->baggage.script,e->message.data,e->message.len);
return 0;
}
static inline int smessage_draw(struct xclient *xc, struct one_event *e) {
(ignore)xc->hooks.message(xc->baggage.script,e->smessage.str,strlen(e->smessage.str));
return 0;
}

int visualbell_xclient(struct xclient *xc, unsigned int color, unsigned int ms) {
struct x11info *x=xc->baggage.x;
unsigned long fillcolor;
unsigned int width,height;

if (xc->isnodraw) return 0;

width=xc->config.rowwidth;
height=xc->config.colheight;

fillcolor=xc->xcolors[color&15].pixel;
if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
if (!XFillRectangle(x->display,x->window,x->context,xc->config.xoff,xc->config.yoff,width,height)) GOTOERROR;
XFlush(x->display);
usleep(ms*1000);
if (redrawrect(xc,xc->config.xoff,xc->config.yoff,width,height)) GOTOERROR;
XFlush(x->display);
return 0;
error:
	return -1;
}

int xbell_xclient(struct xclient *xc, int percent) {
(ignore)XBell(xc->baggage.x->display,percent);
return 0;
}

static int fillpadding(struct xclient *xc, unsigned int color) {
struct x11info *x=xc->baggage.x;
unsigned long fillcolor;

if (xc->isnodraw) return 0;

fillcolor=xc->xcolors[color&15].pixel;
if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
if (xc->config.yoff) {
	if (!XFillRectangle(x->display,x->window,x->context,0,0,xc->config.xwidth,xc->config.yoff)) GOTOERROR;
}
if (xc->config.yoff+xc->config.colheight!=xc->config.xheight) {
	if (!XFillRectangle(x->display,x->window,x->context,0,xc->config.yoff+xc->config.colheight,xc->config.xwidth,
			xc->config.xheight-xc->config.yoff-xc->config.colheight)) GOTOERROR;
}
if (xc->config.xoff) {
	if (!XFillRectangle(x->display,x->window,x->context,0,0,xc->config.xoff,xc->config.xheight)) GOTOERROR;
}
if (xc->config.xoff+xc->config.rowwidth!=xc->config.xwidth) {
	if (!XFillRectangle(x->display,x->window,x->context,xc->config.xoff+xc->config.rowwidth,0,
			xc->config.xwidth-xc->config.xoff-xc->config.rowwidth,xc->config.xheight)) GOTOERROR;
}
return 0;
error:
	return -1;
}

int fillpadding_xclient(struct xclient *xc, unsigned int color, unsigned int ms) {
struct x11info *x=xc->baggage.x;
if (fillpadding(xc,color)) GOTOERROR;
XFlush(x->display);
if (ms) usleep(ms*1000);
return 0;
error:
	return -1;
}

static int bell_draw(struct xclient *xc, struct one_event *e) {
if (xc->hooks.bell(xc->baggage.script)) GOTOERROR;
return 0;
error:
	return -1;
}

static int tap_draw(struct xclient *xc, struct one_event *e) {
unsigned int count;
#if 0
fprintf(stderr,"%s:%d saw tap event: %u\n",__FILE__,__LINE__,e->tap.value);
#endif
if (addchar_texttap(&count,xc->baggage.texttap,e->tap.value)) GOTOERROR;
#ifdef DEBUG
if (!count) WHEREAMI;
#endif
return 0;
error:
	return -1;
}

static int reverse_draw(struct xclient *xc, struct one_event *e) {
unsigned int count;
uint32_t *backing;
count=xc->surface.tofree.backcount;
backing=xc->surface.tofree.backing;
while (1) {
	uint32_t u,n;
	u=*backing;
	n=u&~(FGINDEX_MASK_VALUE|BGINDEX_MASK_VALUE);
	n|=(u&FGINDEX_MASK_VALUE)>>4;
	n|=(u&BGINDEX_MASK_VALUE)<<4;
	*backing=n;
	
	count--;
	if (!count) break;
	backing++;
}

if (redrawrect(xc,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,xc->config.cellh*xc->config.rows)) GOTOERROR;
return 0;
error:
	return -1;
}

static inline int appcursor_draw(struct xclient *xc, struct one_event *e) {
xc->config.isappcursor=e->appcursor.isset;
return 0;
}

static inline int autorepeat_draw(struct xclient *xc, struct one_event *e) {
xc->config.isautorepeat=e->appcursor.isset;
return 0;
}

static int reset_draw(struct xclient *xc, struct one_event *e) {
if (cursoronoff_xclient(xc,xc->config.cursorheight,xc->config.cursoryoff)) GOTOERROR;
return 0;
error:
	return -1;
}

static int drawvteevent(struct xclient *xc, struct one_event *e) {
// fprintf(stderr,"%s:%d drawing event %u\n",__FILE__,__LINE__,e->type);
switch (e->type) {
	case ADDCHAR_TYPE_EVENT: return addchar_draw(xc,e);
	case ERASEINLINE_TYPE_EVENT: return eraseinline_draw(xc,e);
	case SETCURSOR_TYPE_EVENT: return setcursor_draw(xc,e);
	case SCROLL1UP_TYPE_EVENT: return scroll1up_draw(xc,e);
	case SCROLLUP_TYPE_EVENT: return scrollup_draw(xc,e);
	case SCROLL1DOWN_TYPE_EVENT: return scroll1down_draw(xc,e);
	case SCROLLDOWN_TYPE_EVENT: return scrolldown_draw(xc,e);
	case DCH_TYPE_EVENT: return dch_draw(xc,e);
	case TITLE_TYPE_EVENT: return title_draw(xc,e);
	case GENERIC_TYPE_EVENT: return generic_draw(xc,e);
#if 0
	case INSERTLINE_TYPE_EVENT: return insertline_draw(xc,e);
	case DELETELINE_TYPE_EVENT: return deleteline_draw(xc,e);
#endif
	case BELL_TYPE_EVENT: return bell_draw(xc,e);
	case ICH_TYPE_EVENT: return ich_draw(xc,e);
	case MESSAGE_TYPE_EVENT: return message_draw(xc,e);
	case SMESSAGE_TYPE_EVENT: return smessage_draw(xc,e);
	case TAP_TYPE_EVENT: return tap_draw(xc,e);
	case REVERSE_TYPE_EVENT: return reverse_draw(xc,e);
	case APPCURSOR_TYPE_EVENT: return appcursor_draw(xc,e);
	case AUTOREPEAT_TYPE_EVENT: return autorepeat_draw(xc,e);
	case RESET_TYPE_EVENT: return reset_draw(xc,e);
}
return 0;
}

static int drawvteevents(struct xclient *xc) {
// message_event assumes all existing events are processed before vte adds more
struct all_event *events=xc->baggage.events;
struct one_event *e;

if (unset_cursor(xc->baggage.cursor)) GOTOERROR;
e=events->first;
while (e) {
	struct one_event *next;
	next=e->next;
	events->first=next;
	if (drawvteevent(xc,e)) GOTOERROR;
	(void)recycle_event(events,e);
	e=next;
}
// XFlush(xc->baggage.x->display);
XSync(xc->baggage.x->display,False); // without this, draws can queue up fast and delay user input
return 0;
error:
	return -1;
}

static int redrawrect(struct xclient *xc, unsigned int ex, unsigned int ey, unsigned int ew, unsigned int eh) {
// this is a primitive redraw, TODO draw just the rectangle
struct x11info *x=xc->baggage.x;
Display *display;
Window window;
GC context;
unsigned int rownum,rows,firstx,firsty;
unsigned int cellh,cellw,columnsm1;

display=x->display;
window=x->window;
context=x->context;
rows=xc->config.rows;
columnsm1=xc->config.columnsm1;
firstx=xc->config.xoff;
firsty=xc->config.yoff;
cellw=xc->config.cellw;
cellh=xc->config.cellh;

{ XEvent ign; while (XCheckTypedEvent(display,Expose,&ign)); }

if (fillpadding(xc,0)) GOTOERROR;
for (rownum=0;rownum<rows;rownum++) {
	unsigned int c,*backing;
	unsigned int xo;
	Pixmap pixmap;

	backing=xc->surface.lines[rownum].backing;
	c=0;
	xo=firstx;
	while (1) {
		if (!(pixmap=getpixmap(xc,*backing))) GOTOERROR;
		XCopyArea(display,pixmap,window,context,0,0,cellw,cellh,xo,firsty);
		if (c==columnsm1) break;
		c+=1;
		xo+=cellw;
		backing+=1;
	}
	firsty+=cellh;
}

return 0;
error:
	return -1;
}

#if 0
// this should be redone, with mismatched sizes in mind (entire area could be outside lines)
static int redrawrect(struct xclient *xc, unsigned int ex, unsigned int ey, unsigned int ew, unsigned int eh) {
struct x11info *x=xc->baggage.x;
unsigned int firstrow,lastrowp1;
unsigned int firstcol,lastcol;
unsigned int rownum,firsty,firstx;
Display *display;
Window window;
GC context;
unsigned int cellh,cellw;
int isoverflow=0;

cellh=xc->config.cellh;
cellw=xc->config.cellw;

// fprintf(stderr,"%s:%d Redraw rectangle %u %u %u %u\n",__FILE__,__LINE__,ex,ey,ew,eh);

display=x->display;
window=x->window;
context=x->context;

firstrow=(ey+cellh-1-xc->config.yoff)/cellh;
lastrowp1=(ey+eh+cellh-1-xc->config.yoff)/cellh;
firstcol=(ex+cellw-1-xc->config.xoff)/cellw;
firsty=firstrow*cellh+xc->config.yoff;
firstx=firstcol*cellw+xc->config.xoff;
lastcol=(ex+ew-1-xc->config.xoff)/cellw;
if (lastcol>xc->config.columnsm1) {
	isoverflow=1;
	lastcol=xc->config.columnsm1;
	if (firstcol>lastcol) return 0;
}
if (lastrowp1>xc->config.rows) {
	isoverflow=1;
	lastrowp1=xc->config.rows;
}
if (isoverflow) { // we could be a LOT more precise about this
	unsigned long fillcolor;
	fillcolor=xc->xcolors[0];
	if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
	if (!XFillRectangle(x->display,x->window,x->context,ex,ey,ew,eh)) GOTOERROR;
}

for (rownum=firstrow;rownum<lastrowp1;rownum++) {
	unsigned int c,*backing;
	unsigned int x;
	Pixmap pixmap;

	backing=&xc->surface.lines[rownum].backing[firstcol];
	c=firstcol;
	x=firstx;
	while (1) {
		if (!(pixmap=getpixmap(xc,*backing))) GOTOERROR;
		XCopyArea(display,pixmap,window,context,0,0,cellw,cellh,x,firsty);
		if (c==lastcol) break;
		c+=1;
		x+=cellw;
		backing+=1;
	}
	firsty+=cellh;
}

return 0;
error:
	return -1;
}
#endif

static int handlexpose(struct xclient *xc, XEvent *e) {
XExposeEvent *ee;

ee=&e->xexpose;
if (redrawrect(xc,ee->x,ee->y,ee->width,ee->height)) GOTOERROR;
XFlush(xc->baggage.x->display);
return 0;
error:
	return -1;
}

static int handleconfigure(struct xclient *xc, XEvent *e_in) {
XConfigureEvent *e;
e=&e_in->xconfigure;
if ((xc->config.xwidth==e->width)&&(xc->config.xheight==e->height)) return 0;
xc->config.xwidth=e->width;
xc->config.xheight=e->height;
if (xc->hooks.onresize(xc->baggage.script,e->width,e->height)) GOTOERROR;
return 0;
error:
	return  -1;
}

static int pause_handlexevent_xclient(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
XEvent e;

XNextEvent(x->display,&e);
switch (e.type) {
	case FocusIn: x->isfocused=1; break;
	case FocusOut: x->isfocused=0; break;
	case Expose: if (handlexpose(xc,&e)) GOTOERROR; break;
	case KeyRelease: if (handlekeyrelease(xc,&e)) GOTOERROR; break;
	case KeyPress:
#if 0
			if (xc->config.isautorepeat && (X)) break;
#endif
			if (pause_handlekeypress(xc,&e)) GOTOERROR;
			break;
	case ButtonRelease:
			if (handlebuttonrelease(xc,&e.xbutton)) GOTOERROR;
			break;
	case ButtonPress:
			if (handlebuttonpress(xc,&e.xbutton)) GOTOERROR;
			break;
	case MotionNotify:
			if (handlemotion(xc,&e.xmotion)) GOTOERROR;
			break;
	case ReparentNotify: break;
	case ConfigureNotify: if (handleconfigure(xc,&e)) GOTOERROR; break;
	case SelectionClear: (void)onselectionclear_xclipboard(xc->baggage.xclipboard); break;
	case SelectionRequest:
		if (onselectionrequest_xclipboard(xc->baggage.xclipboard,&e.xselectionrequest)) GOTOERROR;
		break;
//	case NoExpose: break; // these come from XCopyArea in cursor.c
	default:
		fprintf(stderr,"%s:%d Unhandled XEvent.type:%d (%s)\n",__FILE__,__LINE__,e.type,evtypetostring_x11info(e.type,"Unknown"));
		break;
}

return 0;
error:
	return -1;
}

static int handlexevent_xclient(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
XEvent e;

XNextEvent(x->display,&e);
switch (e.type) {
	case FocusIn: x->isfocused=1; break;
	case FocusOut: x->isfocused=0; break;
	case Expose: if (handlexpose(xc,&e)) GOTOERROR; break;
	case KeyRelease: if (handlekeyrelease(xc,&e)) GOTOERROR; break;
	case KeyPress: 
#if 0
			if (xc->config.isautorepeat && (X)) break;
#endif
			if (handlekeypress(xc,&e)) GOTOERROR;
			break;
	case ButtonRelease:
			if (handlebuttonrelease(xc,&e.xbutton)) GOTOERROR;
			break;
	case ButtonPress:
			if (handlebuttonpress(xc,&e.xbutton)) GOTOERROR;
			break;
	case MotionNotify:
			if (handlemotion(xc,&e.xmotion)) GOTOERROR;
			break;
	case ReparentNotify: break;
	case ConfigureNotify: if (handleconfigure(xc,&e)) GOTOERROR; break;
	case SelectionClear: (void)onselectionclear_xclipboard(xc->baggage.xclipboard); break;
	case SelectionRequest:
		if (onselectionrequest_xclipboard(xc->baggage.xclipboard,&e.xselectionrequest)) GOTOERROR;
		break;
//	case NoExpose: break; // these come from XCopyArea
	default:
		fprintf(stderr,"%s:%d Unhandled XEvent.type:%d (%s)\n",__FILE__,__LINE__,e.type,evtypetostring_x11info(e.type,"Unknown"));
		break;
}

return 0;
error:
	return -1;
}

static int checkforscript(struct xclient *xc) {
struct vte *vte=xc->baggage.vte;
unsigned int len;
char *msg;

#if 0
if (!xc->nextalarm) fprintf(stderr,"%s:%d no nextalarm\n",__FILE__,__LINE__);
#endif
if (xc->nextalarm) {
	uint64_t t;
	t=time(NULL);
	if (t>=xc->nextalarm) {
		unsigned int t32;
		t32=(unsigned int)t;
		xc->nextalarm=0;
		xc->hooks.alarmcall(xc->baggage.script,(int)t32); // it's received as uint32_t; we're good until 2106
	}
}

if (!xc->hooks.checkinsertion(xc->baggage.script)) return 0;
if ((vte->input.mode) || (vte->readqueue.max_buffer - vte->readqueue.qlen < BUFFSIZE_INSERTION_XCLIENT)) {
	xc->ispaused=0;
	return 0;
}
msg=xc->hooks.getinsertion(&len,xc->baggage.script);
if (!msg) return 0;
if (insert_readqueue_vte(vte,msg,len)) GOTOERROR;
xc->ispaused=0;
return 0;
error:
	return -1;
}

static int pauseloop_xclient(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
struct cursor *cursor=xc->baggage.cursor;
int xfd;
xfd=ConnectionNumber(x->display);
while (1) {
	fd_set rset;
	struct timeval tv;

	if (!xc->ispaused) break;

	if (checkforscript(xc)) GOTOERROR;

	if (XEventsQueued(x->display,QueuedAlready)) {
		if (pause_handlexevent_xclient(xc)) GOTOERROR;
		continue;
	}
	tv.tv_sec=60-59*x->isfocused;
	tv.tv_usec=0;
	FD_ZERO(&rset);
	FD_SET(xfd,&rset);
	switch (select(xfd+1,&rset,NULL,NULL,&tv)) {
		case 0:
			if (x->isfocused && pulse_cursor(cursor)) GOTOERROR;
			continue;
		case -1: GOTOERROR;
	}
	if (FD_ISSET(xfd,&rset)) {
		XEventsQueued(x->display,QueuedAfterReading);
	}
}
return 0;
error:
	return -1;
}

int mainloop_xclient(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
struct vte *vte=xc->baggage.vte;
struct cursor *cursor=xc->baggage.cursor;
int xfd,maxfd,ptyfd;

maxfd=xfd=ConnectionNumber(x->display);
ptyfd=vte->writequeue.fd;
maxfd=_BADMAX(maxfd,ptyfd)+1;
(ignore)setpointer_xclient(xc,0);
if (checkforscript(xc)) GOTOERROR;
while (1) {
	fd_set rset,wset;
	struct timeval tv;
nextloop:

	if (xc->ispaused) {
		if (pauseloop_xclient(xc)) GOTOERROR;
		if (checkforscript(xc)) GOTOERROR;
		if (xc->isquit) break;
	}
	
	if (XEventsQueued(x->display,QueuedAlready)) {
		if (handlexevent_xclient(xc)) GOTOERROR;
		continue;
	}
	if (vte->readqueue.qlen) while (1) {
		if (processreadqueue_vte(vte)) GOTOERROR;
		if (drawvteevents(xc)) GOTOERROR;
		if (xc->ispaused) goto nextloop;
		if (XEventsQueued(x->display,QueuedAfterReading)) goto nextloop;
		if (!vte->readqueue.qlen) break;
	}
	tv.tv_sec=60-59*x->isfocused;
	tv.tv_usec=0;
	FD_ZERO(&rset);
	FD_SET(xfd,&rset);
	if (!vte->readqueue.qlen) FD_SET(ptyfd,&rset);
	FD_ZERO(&wset);
	if (vte->writequeue.len) FD_SET(ptyfd,&wset);
	switch (select(maxfd,&rset,&wset,NULL,&tv)) {
		case 0:
			if (xc->isnodraw && drawon_xclient(xc)) GOTOERROR;
			if (x->isfocused && pulse_cursor(cursor)) GOTOERROR;
			if (checkforscript(xc)) GOTOERROR;
			if (unmark_xclient(xc)) GOTOERROR;
			continue;
		case -1: GOTOERROR;
	}

	if (FD_ISSET(xfd,&rset)) {
		XEventsQueued(x->display,QueuedAfterReading);
	}
	if (FD_ISSET(ptyfd,&rset)) {
		if (fillreadqueue_vte(vte)) { xc->isquit=1; break; }
	}
	if (FD_ISSET(ptyfd,&wset)) {
		if (flush_vte(vte)) GOTOERROR;
	}
}
return 0;
error:
	return -1;
}

static int clearcaches(struct xclient *xc) {
struct vte *vte=xc->baggage.vte;
struct cursor *cursor=xc->baggage.cursor;

if (cursor->isplaced) xc->config.changes.iscurset=1;
if (reset_cursor(cursor)) GOTOERROR;
(void)reset_charcache(xc->baggage.charcache);
if (setxcolors(xc,vte)) GOTOERROR;
xc->config.changes.isredraw=1;
return 0;
error:
	return -1;
}

int fixcolors_xclient(struct xclient *xc) {
// call reconfig_ afterward
struct vte *vte=xc->baggage.vte;
struct config *config=xc->baggage.config;

if (config->isdarkmode) (void)setcolors_vte(vte,&config->darkmode);
else (void)setcolors_vte(vte,&config->lightmode);

if (clearcaches(xc)) GOTOERROR;
return 0;
error:
	return -1;
}

int clrscr_xclient(struct xclient *xc, uint32_t fgvaluemask, uint32_t bgvaluemask) {
struct x11info *x=xc->baggage.x;
unsigned long fillcolor;
unsigned int blankval;
struct line_xclient *line,*lastline;
unsigned int cols;

fillcolor=xc->xcolors[(bgvaluemask>>21)&15].pixel;
if (!xc->isnodraw) {
	if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
	if (!XFillRectangle(x->display,x->window,x->context,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,xc->config.colheight)) GOTOERROR;
	XFlush(x->display);
}
#if 0
blankval=32|bgvaluemask;
#else
blankval=32|bgvaluemask|fgvaluemask;
#endif

line=xc->surface.lines;
lastline=line+xc->config.rowsm1;
cols=xc->config.columns;
while (1) {
	memset4(line->backing,blankval,cols);
	if (line==lastline) break;
	line++;
}
return 0;
error:
	return -1;
}

void savebacking_xclient(struct xclient *xc) {
struct line_xclient *line,*lastline;
struct line_xclient *saved;
unsigned int colsx4;

(void)clearselection_xclient(xc);

saved=xc->surface.savedlines;
line=xc->surface.lines;
lastline=line+xc->config.rowsm1;
colsx4=xc->config.columns *4;
while (1) {
	memcpy(saved->backing,line->backing,colsx4);
	if (line==lastline) break;
	line++;
	saved++;
}
}

int restorebacking_xclient(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
struct line_xclient *line,*lastline;
struct line_xclient *saved;
unsigned int colsx4;

saved=xc->surface.savedlines;
line=xc->surface.lines;
lastline=line+xc->config.rowsm1;
colsx4=xc->config.columns *4;
while (1) {
	memcpy(line->backing,saved->backing,colsx4);
	if (line==lastline) break;
	line++;
	saved++;
}

if (redrawrect(xc,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,xc->config.colheight)) GOTOERROR;
XFlush(x->display);
return 0;
error:
	return -1;
}

int pause_xclient(struct xclient *xc) {
xc->ispaused=1;
return 0;
}
int unpause_xclient(struct xclient *xc) {
struct cursor *cursor=xc->baggage.cursor;
if (!xc->ispaused) return 0;
xc->ispaused=0;
if (setcursor(xc,cursor->row,cursor->col)) GOTOERROR;
return 0;
error:
	return -1;
}

void setalarm_xclient(struct xclient *xc, unsigned int seconds) {
uint64_t t;
t=(uint64_t)time(NULL)+seconds;
if ((!xc->nextalarm) || (t<xc->nextalarm)) xc->nextalarm=t;
}

int setcursorcolors_xclient(struct xclient *xc, unsigned short r, unsigned short g, unsigned short b) {
// call reconfig_ afterward
struct cursor *cursor=xc->baggage.cursor;

if (cursor->isplaced) xc->config.changes.iscurset=1;
if (reset_cursor(cursor)) GOTOERROR;
if (setcolors_cursor(cursor,r,g,b)) GOTOERROR;
return 0;
error:
	return -1;
}

unsigned int *fetchline_xclient(struct xclient *xc, unsigned int row, unsigned int col, unsigned int count) {
uint32_t *sp,*backing;
if (col+count>xc->config.columns) return NULL;
if (row>xc->config.rows) return NULL;
sp=xc->surface.spareline;
backing=xc->surface.lines[row].backing;
memcpy(sp,backing+col,count*sizeof(uint32_t));
return sp;
}

int send_xclient(int *isdrop_out, struct xclient *xc, unsigned char *letters, unsigned int len) {
if (!len) return 0;
if (writeorqueue_vte(isdrop_out,xc->baggage.vte,letters,len)) GOTOERROR;
return 0;
error:
	return -1;
}

int setcursor_xclient(struct xclient *xc, unsigned int row, unsigned int col) {
if (setcursor(xc,row,col)) GOTOERROR;
return 0;
error:
	return -1;
}

int fillrect_xclient(struct xclient *xc, unsigned int x, unsigned int y,
		unsigned int width, unsigned int height, unsigned int color, unsigned int ms) {
struct x11info *xi=xc->baggage.x;
unsigned long fillcolor;

if (xc->isnodraw) return 0;

fillcolor=xc->xcolors[color&15].pixel;
if (!XSetForeground(xi->display,xi->context,fillcolor)) GOTOERROR;
if (x+width>xc->config.xwidth) return 0;
if (y+height>xc->config.xheight) return 0;
if (!XFillRectangle(xi->display,xi->window,xi->context,x,y,width,height)) GOTOERROR;
XFlush(xi->display);
if (ms) usleep(ms*1000);
return 0;
error:
	return -1;
}

int restorerect_xclient(struct xclient *xc, unsigned int x, unsigned int y, unsigned int width, unsigned int height) {
struct x11info *xi=xc->baggage.x;

if (x+width>xc->config.xwidth) return 0;
if (y+height>xc->config.xheight) return 0;
if (redrawrect(xc,x,y,width,height)) GOTOERROR;
XFlush(xi->display);
return 0;
error:
	return -1;
}

int changefont_xclient(struct xclient *xc, char *fontname) {
// call reconfig_ afterward
if (clearcaches(xc)) GOTOERROR;
if (changefont_xftchar(xc->baggage.xftchar,fontname)) GOTOERROR;
return 0;
error:
	return -1;
}

int reconfig_xclient(struct xclient *xc) {
struct cursor *cursor=xc->baggage.cursor;
int isxf=0;

if (xc->config.changes.isredraw) {
	xc->config.changes.isredraw=0;
	xc->config.changes.isremap=0;
	isxf=1;
	if (redrawrect(xc,0,0,xc->config.xwidth,xc->config.xheight)) GOTOERROR;
} 
if (xc->config.changes.iscurset) {
	xc->config.changes.iscurset=0;
	if (setcursor(xc,cursor->row,cursor->col)) GOTOERROR;
}
if (isxf) XFlush(xc->baggage.x->display);
return 0;
error:
	return -1;
}

int iseventsempty_xclient(struct xclient *xc) {
if (xc->baggage.events->first) return 0;
return 1;
}

int resizewindow_xclient(struct xclient *xc, unsigned int width, unsigned int height) {
// call reconfig_ afterward
if (clearcaches(xc)) GOTOERROR;
if (resizewindow_x11info(xc->baggage.x,width,height)) GOTOERROR;
xc->config.changes.isredraw=1;
xc->config.changes.isremap=1;
xc->config.xwidth=width;
xc->config.xheight=height;
xc->config.xoff=(width-xc->config.rowwidth)/2;
xc->config.yoff=(height-xc->config.colheight)/2;
return 0;
error:
	return -1;
}

int resizesurface_xclient(struct xclient *xc, unsigned int rows, unsigned int cols) {
// call reconfig_ afterward
// to avoid redundancy, should resize window first if it's going to happen
struct vte *vte=xc->baggage.vte;
uint32_t blankvalue;
long fillcolor;
if (clearcaches(xc)) GOTOERROR;

blankvalue=32|vte->curbgcolor->bgvaluemask;
fillcolor=xc->xcolors[vte->curbgcolor->index].pixel;

if (resize_surface_xclient(&xc->surface,xc->baggage.x,xc->config.rows,xc->config.columns,
		rows,cols,blankvalue,xc->config.scrollbackcount,xc->config.cellw,xc->config.cellh,fillcolor,xc->config.changes.isremap)) GOTOERROR;

xc->config.columns=cols;
xc->config.columnsm1=cols-1;
xc->config.rows=rows;
xc->config.rowsm1=rows-1;
xc->config.rowwidth=cols*xc->config.cellw;
xc->config.colheight=rows*xc->config.cellh;

xc->config.xoff=(xc->config.xwidth-xc->config.rowwidth)/2;
xc->config.yoff=(xc->config.xheight-xc->config.colheight)/2;

xc->config.changes.isredraw=1;

if (resize_pty(xc->baggage.pty,cols,rows)) GOTOERROR;
if (resize_vte(xc->baggage.vte,rows,cols)) GOTOERROR;
return 0;
error:
	return -1;
}

int resizecell_xclient(struct xclient *xc, unsigned int cellw, unsigned int cellh) {
// call reconfig_ afterward
if (clearcaches(xc)) GOTOERROR;

xc->config.cellw=cellw;
xc->config.cellh=cellh;
xc->config.rowwidth=xc->config.columns*cellw;
xc->config.colheight=xc->config.rows*cellh;
xc->config.changes.isredraw=1;
if (resize_xftchar(xc->baggage.xftchar,cellw,cellh)) GOTOERROR;
if (resize_charcache(xc->baggage.charcache,cellw,cellh)) GOTOERROR;
return 0;
error:
	return -1;
}

int resizecursor_xclient(struct xclient *xc, unsigned int cellw, unsigned int cellh, unsigned int cursorheight,
		unsigned int cursoryoff, unsigned int isblink) {
// call reconfig_ afterward
struct cursor *cursor=xc->baggage.cursor;
if (cursor->isplaced) xc->config.changes.iscurset=1;
if (reconfig_cursor(xc->baggage.cursor, xc->config.xoff, xc->config.yoff, cellw, cellh, cursorheight, cursoryoff, isblink)) GOTOERROR;
xc->config.cellw=cellw;
xc->config.cellh=cellh;
xc->config.cursorheight=cursorheight;
xc->config.cursoryoff=cursoryoff;
xc->config.isblink=isblink;
return 0;
error:
	return -1;
}

void setparams_xclient(struct xclient *xc, int font0shift, int font0line, int fontulline, int fontullines) {
(void)setparams_xftchar(xc->baggage.xftchar,font0shift,font0line,fontulline,fontullines);
}

int mark_xclient(struct xclient *xc) {
// to avoid hidden keyloggers and such, mark the screen to show there was interference
// TODO optimize this, need to coordinate with resizing
struct x11info *x=xc->baggage.x;
long fillcolor;
unsigned int mw;

if (xc->isnodraw) return 0;
if (xc->status.ismarked) return 0;
fillcolor=xc->xcolors[15].pixel;
xc->status.ismarked=1;
if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
mw=_BADMAX(xc->config.xwidth-xc->config.xoff-xc->config.rowwidth,xc->config.xheight-xc->config.yoff-xc->config.colheight);
#if 0
fprintf(stderr,"%s:%d mw:%u\n",__FILE__,__LINE__,mw);
fprintf(stderr,"%s:%d xwidth:%u xoff:%u rowwidth:%u\n",__FILE__,__LINE__,xc->config.xwidth,xc->config.xoff,xc->config.rowwidth);
fprintf(stderr,"%s:%d xheight:%u yoff:%u colheight:%u\n",__FILE__,__LINE__,xc->config.xheight,xc->config.yoff,xc->config.colheight);
#endif
if (!XFillRectangle(x->display,x->window,x->context,xc->config.xwidth-mw,0, mw,mw)) GOTOERROR;
XFlush(x->display);
return 0;
error:
	return -1;
}
int unmark_xclient(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
long fillcolor;
unsigned int mw;
if (xc->isnodraw) return 0;
if (!xc->status.ismarked) return 0;
fillcolor=xc->xcolors[0].pixel;
xc->status.ismarked=0;
if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
mw=_BADMAX(xc->config.xwidth-xc->config.xoff-xc->config.rowwidth,xc->config.xheight-xc->config.yoff-xc->config.colheight);
if (!XFillRectangle(x->display,x->window,x->context,xc->config.xwidth-mw,0, mw,mw)) GOTOERROR;
return 0;
error:
	return -1;
}

static int drawrow(struct xclient *xc, uint32_t *backing, unsigned int row, uint32_t *oldbacking) {
struct x11info *xi=xc->baggage.x;
Display *display;
Window window;
GC context;
Pixmap pixmap;
unsigned int x,yoff,cellw,cellh;
uint32_t *lastbacking;

display=xi->display;
window=xi->window;
context=xi->context;
x=xc->config.xoff;
yoff=xc->config.yoff+xc->config.cellh*row;
cellw=xc->config.cellw;
cellh=xc->config.cellh;
lastbacking=backing+xc->config.columnsm1;

while (1) {
	if (*backing!=*oldbacking) {
		if (!(pixmap=getpixmap(xc,*backing))) GOTOERROR;
		XCopyArea(display,pixmap,window,context,0,0,cellw,cellh,x,yoff);
	}
	if (backing==lastbacking) break;
	backing+=1;
	oldbacking+=1;
	x+=cellw;
}

return 0;
error:
	return -1;
}

static int scrollback(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
struct sbline_xclient *sb;
struct line_xclient ll;
uint32_t *backing;

#if 0
sb=xc->surface.scrollback.first;
WHEREAMI;
if (drawrow(xc,sb->backing,0)) GOTOERROR;
return 0;
#endif

sb=xc->surface.scrollback.first;
xc->surface.scrollback.first=sb->next;
if (sb->next) {
	sb->next->previous=NULL;
} else {
	xc->surface.scrollback.last=NULL;
}
ll=xc->surface.lines[xc->config.rowsm1];
memmove(xc->surface.lines+1,xc->surface.lines,sizeof(struct line_xclient)*xc->config.rowsm1);
XCopyArea(x->display,x->window,x->window,x->context,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,
		xc->config.cellh*xc->config.rowsm1, xc->config.xoff,xc->config.yoff+xc->config.cellh);

// fprintf(stderr,"%s:%d sb->backing:%p\n",__FILE__,__LINE__,sb->backing);
// fprintf(stderr,"%s:%d ll.backing:%p\n",__FILE__,__LINE__,ll.backing);
backing=sb->backing;
sb->backing=ll.backing;
sb->next=xc->surface.scrollback.reverse.first;
xc->surface.scrollback.reverse.first=sb;
ll.backing=backing;
xc->surface.lines[0]=ll;

if (drawrow(xc,backing,0,xc->surface.lines[1].backing)) GOTOERROR;
XFlush(x->display);

xc->scrollback.linesback+=1;
return 0;
error:
	return -1;
}

static int rev_scrollback(struct xclient *xc) {
struct x11info *x=xc->baggage.x;
struct sbline_xclient *sb;
struct line_xclient fl;
uint32_t *backing;

sb=xc->surface.scrollback.reverse.first;
xc->surface.scrollback.reverse.first=sb->next;

fl=xc->surface.lines[0];
memmove(xc->surface.lines,xc->surface.lines+1,sizeof(struct line_xclient)*xc->config.rowsm1);
XCopyArea(x->display,x->window,x->window,x->context,xc->config.xoff,xc->config.yoff+xc->config.cellh,xc->config.rowwidth,
		xc->config.cellh*xc->config.rowsm1, xc->config.xoff,xc->config.yoff);

backing=sb->backing;
sb->backing=fl.backing;
sb->next=xc->surface.scrollback.first;
if (xc->surface.scrollback.first) {
	xc->surface.scrollback.first->previous=sb;
	xc->surface.scrollback.first=sb;
} else {
	sb->next=sb->previous=NULL;
	xc->surface.scrollback.first=xc->surface.scrollback.last=sb;
}
fl.backing=backing;
xc->surface.lines[xc->config.rowsm1]=fl;

if (drawrow(xc,backing,xc->config.rowsm1,xc->surface.lines[xc->config.rows-2].backing)) GOTOERROR;
XFlush(x->display);

xc->scrollback.linesback-=1;
return 0;
error:
	return -1;
}

static void nodraw_rev_scrollback(struct xclient *xc) {
struct sbline_xclient *sb;
struct line_xclient fl;
uint32_t *backing;

sb=xc->surface.scrollback.reverse.first;
xc->surface.scrollback.reverse.first=sb->next;

fl=xc->surface.lines[0];
memmove(xc->surface.lines,xc->surface.lines+1,sizeof(struct line_xclient)*xc->config.rowsm1);

backing=sb->backing;
sb->backing=fl.backing;
sb->next=xc->surface.scrollback.first;
if (xc->surface.scrollback.first) {
	xc->surface.scrollback.first->previous=sb;
	xc->surface.scrollback.first=sb;
} else {
	sb->next=sb->previous=NULL;
	xc->surface.scrollback.first=xc->surface.scrollback.last=sb;
}
fl.backing=backing;
xc->surface.lines[xc->config.rowsm1]=fl;

xc->scrollback.linesback-=1;
}

static int reset_scrollback(struct xclient *xc) {
while (1) {
	(void)nodraw_rev_scrollback(xc);
	if (!xc->scrollback.linesback) break;
}
if (redrawrect(xc,0,0,xc->config.xwidth,xc->config.xheight)) GOTOERROR;
return 0;
error:
	return -1;
}

int scrollback_xclient(struct xclient *xc, int delta) {
if (delta>0) {
	if (!xc->surface.scrollback.first) return 0;
	if (!xc->scrollback.linesback) {
		xc->scrollback.ispaused=xc->ispaused;
		xc->ispaused=1;
		if ((xc->scrollback.iscurset=xc->baggage.cursor->isplaced)) {
			if (reset_cursor(xc->baggage.cursor)) GOTOERROR;
		}
	}
	while (1) {
		if (scrollback(xc)) GOTOERROR;
		delta--;
		if (!delta) break;
		if (!xc->surface.scrollback.first) break;
	}
} else {
	if ((!(delta+xc->scrollback.linesback))&&(xc->scrollback.linesback>=xc->config.rowsm1)) { // go back to normal without scrolling
		if (reset_scrollback(xc)) GOTOERROR;
		XFlush(xc->baggage.x->display);
	}
	while (1) {
		if (!xc->surface.scrollback.reverse.first) {
			if (!xc->scrollback.ispaused) xc->ispaused=0;
			if (xc->scrollback.iscurset) {
				if (setcursor(xc,xc->baggage.cursor->row,xc->baggage.cursor->col)) GOTOERROR;
			}
			break;
		}
		if (!delta) break;
		if (rev_scrollback(xc)) GOTOERROR;
		delta++;
	}
}

return 0;
error:
	return -1;
}

int copy_xclient(struct xclient *xc, unsigned char *selection, unsigned int selectionlen, unsigned char *text, unsigned int textlen) {
char selname[MAX_SELECTION_XCLIPBOARD+1];
if (selectionlen>MAX_SELECTION_XCLIPBOARD) GOTOERROR;
memcpy(selname,selection,selectionlen); selname[selectionlen]='\0';
if (copy_xclipboard(xc->baggage.xclipboard,selname,text,textlen)) GOTOERROR;
return 0;
error:
	return -1;
}

int getpaste_xclient(struct xclient *xc, unsigned char *dest, unsigned int destlen) {
return getpaste_xclipboard(xc->baggage.xclipboard,dest,destlen);
}

int paste_xclient(unsigned int *newlen_out, struct xclient *xc, unsigned char *selection, unsigned int selectionlen, int timeout) {
char selname[MAX_SELECTION_XCLIPBOARD+1];
if (selectionlen>MAX_SELECTION_XCLIPBOARD) GOTOERROR;
memcpy(selname,selection,selectionlen); selname[selectionlen]='\0';
if (paste_xclipboard(newlen_out,xc->baggage.xclipboard,selname,timeout)) GOTOERROR;
return 0;
error:
	return -1;
}

static void setinversion(uint32_t *backing, unsigned int start, unsigned int stop) {
backing+=start;
stop-=start;
while (1) {
	uint32_t u,n;
	u=*backing;
	n=u&(UCS4_MASK_VALUE|UNDERLINEBIT_VALUE);
	n|=SELECTINVERSION_VALUE;
	n|=(u&FGINDEX_MASK_VALUE)>>4;
	n|=(u&BGINDEX_MASK_VALUE)<<4;
	*backing=n;
	if (!stop) break;
	stop--;
	backing++;
}
}
static void resetinversion(uint32_t *backing, unsigned int len) {
while (1) {
	uint32_t u,n;
	u=*backing;	
	if (u&SELECTINVERSION_VALUE) {
		n=u&(UCS4_MASK_VALUE|UNDERLINEBIT_VALUE);
		n|=(u&FGINDEX_MASK_VALUE)>>4;
		n|=(u&BGINDEX_MASK_VALUE)<<4;
		*backing=n;
	}
	len--;
	if (!len) break;
	backing++;
}
}

void clearselection_xclient(struct xclient *xc) {
unsigned int ui,numinline;
uint32_t **list;
if (!xc->surface.selection.mode) return;
xc->surface.selection.mode=0;
list=xc->surface.selection.backings.list;
ui=xc->surface.selection.backings.len;
numinline=xc->surface.numinline;
while (ui) {
	(void)resetinversion(*list,numinline);
	list++;
	ui--;
}
}

static int clearandredrawselection(struct xclient *xc) {
(void)clearselection_xclient(xc);
if (redrawrect(xc,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,xc->config.cellh*xc->config.rows)) GOTOERROR;
(ignore)setpointer_xclient(xc,0);
XFlush(xc->baggage.x->display);
return 0;
error:
	return -1;
}

int isvalid_setselection_xclient(struct xclient *xc, unsigned int row_start, unsigned int col_start,
	unsigned int row_stop, unsigned int col_stop) {
if (row_start >= xc->config.rows) return 0;
if (row_stop >= xc->config.rows) return 0;
if (col_start >= xc->config.columns) return 0;
if (col_stop >= xc->config.columns) return 0;
return 1;
}

int setselection_xclient(struct xclient *xc, int mode, unsigned int row_start, unsigned int col_start,
		unsigned int row_stop, unsigned int col_stop) {
// call copy_xclient to actually copy it to a clipboard
unsigned int rows,ui,ui2,columnsm1;
(void)clearselection_xclient(xc);
if (row_start >= row_stop) {
	unsigned int temp;
#define SWAP(a,b) do { temp=a; a=b; b=temp; } while (0)
	if (row_start==row_stop) {
		if (col_start > col_stop) SWAP(col_start,col_stop);
	} else {
		SWAP(col_start,col_stop);
		SWAP(row_start,row_stop);
	}
#undef SWAP
}
// start <= stop now
xc->surface.selection.mode=mode;
xc->surface.selection.start.row=row_start;
xc->surface.selection.start.col=col_start;
xc->surface.selection.stop.row=row_stop;
xc->surface.selection.stop.col=col_stop;
rows=row_stop-row_start+1;
if (rows>xc->surface.selection.backings.max) {
	unsigned int max;
	uint32_t **temp;
	max=rows+xc->config.rows;
	temp=realloc(xc->surface.selection.backings.list,max*sizeof(uint32_t*));
	if (!temp) GOTOERROR;
	xc->surface.selection.backings.list=temp;
	xc->surface.selection.backings.max=max;
}
columnsm1=xc->config.columnsm1;
ui=row_start;
ui2=0;
xc->surface.selection.backings.list[ui2]=xc->surface.lines[ui].backing;
if (rows==1) (void)setinversion(xc->surface.lines[ui].backing,col_start,col_stop);
else {
	(void)setinversion(xc->surface.lines[ui].backing,col_start,columnsm1);
	for (ui+=1;ui<row_stop;ui++) {
		ui2+=1;
		xc->surface.selection.backings.list[ui2]=xc->surface.lines[ui].backing;
		(void)setinversion(xc->surface.lines[ui].backing,0,columnsm1);
	}
	xc->surface.selection.backings.list[ui2+1]=xc->surface.lines[ui].backing;
	(void)setinversion(xc->surface.lines[ui].backing,0,col_stop);
}

xc->surface.selection.backings.len=rows;
if (redrawrect(xc,xc->config.xoff,xc->config.yoff+xc->config.cellh*row_start,xc->config.rowwidth,xc->config.cellh*rows)) GOTOERROR;
XFlush(xc->baggage.x->display);
return 0;
error:
	return -1;
}

int copyselection_xclient(struct xclient *xc, unsigned char *selection, unsigned int selectionlen) {
char selname[MAX_SELECTION_XCLIPBOARD+1];
unsigned int reslen,row,col,columns,stoprow,stopcol;
struct xclipboard *xclip;
uint32_t *backing;

if (selectionlen>MAX_SELECTION_XCLIPBOARD) GOTOERROR;

xclip=xc->baggage.xclipboard;
memcpy(selname,selection,selectionlen); selname[selectionlen]='\0';
row=xc->surface.selection.start.row;
col=xc->surface.selection.start.col;
stoprow=xc->surface.selection.stop.row;
stopcol=xc->surface.selection.stop.col;
columns=xc->config.columns;
reslen= columns * 4 * (stoprow - row + 1);
if (reserve_copy_xclipboard(xclip,selname,reslen)) GOTOERROR;

backing=xc->surface.lines[row].backing+col;
while (1) {
	unsigned char buff4[4];
	unsigned int bufflen;
	(void)uc4toutf8(&bufflen,buff4,(*backing)&UCS4_MASK_VALUE);
	if (add_copy_xclipboard(xclip,buff4,bufflen)) GOTOERROR;
	if ((row==stoprow)&&(col==stopcol)) break;
	col++;
	backing++;
	if (col==columns) {
		row++;
		backing=xc->surface.lines[row].backing;
		col=0;
	}
}
if (finish_copy_xclipboard(xclip)) GOTOERROR;
return 0;
error:
	return -1;
}

int setpointer_xclient(struct xclient *xc, int code) {
// XC_hand2: 60, XC_xterm: 152
struct x11info *x=xc->baggage.x;
Cursor c=None;
if (code>0) c=XCreateFontCursor(x->display,code);
(ignore)XDefineCursor(x->display,x->window,c);
XFlush(xc->baggage.x->display);
return 0;
}

int movewindow_xclient(struct xclient *xc, int x, int y) {
// call reconfig_ afterward
if (movewindow_x11info(xc->baggage.x,x,y)) GOTOERROR;
return 0;
error:
	return -1;
}

int drawoff_xclient(struct xclient *xc) {
xc->isnodraw=1;
// fprintf(stderr,"Setting nodraw\n");
if (unset_cursor(xc->baggage.cursor)) GOTOERROR;
return 0;
error:
	return -1;
}
int drawon_xclient(struct xclient *xc) {
xc->isnodraw=0;
if (redrawrect(xc,xc->config.xoff,xc->config.yoff,xc->config.rowwidth,xc->config.colheight)) GOTOERROR;
if (setcursor(xc, xc->baggage.cursor->row, xc->baggage.cursor->col)) GOTOERROR;
XFlush(xc->baggage.x->display);
// fprintf(stderr,"Reset nodraw\n");
return 0;
error:
	return -1;
}

int cursoronoff_xclient(struct xclient *xc, unsigned int height, unsigned int yoff) {
if (reset_cursor(xc->baggage.cursor)) GOTOERROR;
if (height) {
	(void)setheight_cursor(xc->baggage.cursor,height,yoff,xc->config.isblink);
} else {
	(void)setheight_cursor(xc->baggage.cursor,0,xc->config.cellh,0);
}
return 0;
error:
	return -1;
}

int grabpointer_xclient(struct xclient *xc, int toggle) {
struct halfclick_xclient *lhc;
struct click_xclient *lc;
lhc=&xc->surface.selection.lasthalfclick;
lc=&xc->surface.selection.lastclick;
lhc->ispress=0;
lhc->buttonnumber=0;
lc->buttonnumber=0;
if (toggle) {
	XSetWindowAttributes attr;
	xc->ismousegrabbed=1;
	attr.event_mask=xc->baggage.x->attr.event_mask;
	attr.event_mask|=PointerMotionMask;
	if (!XChangeWindowAttributes(xc->baggage.x->display,xc->baggage.x->window,CWEventMask,&attr)) GOTOERROR;
} else {
	xc->ismousegrabbed=0;
	if (!XChangeWindowAttributes(xc->baggage.x->display,xc->baggage.x->window,CWEventMask,&xc->baggage.x->attr)) GOTOERROR;
}
return 0;
error:
	return -1;
}
