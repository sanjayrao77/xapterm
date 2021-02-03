/*
 * script.c - implement python to hold external scripts
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
#define PY_SSIZE_T_CLEAN
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/select.h>
#include <termios.h>
#include <pty.h>
#include <time.h>
#include <Python.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#define DEBUG
#include "common/conventions.h"
#include "common/blockmem.h"
#include "common/texttap.h"
#include "config.h"
#include "x11info.h"
#include "xftchar.h"
#include "vte.h"
#include "cursor.h"
#include "xclient.h"
#include "pty.h"

#include "script.h"

/* A note on modules:
 * config: this exists at startup, before anything is loaded
 * vte: this should house every core function even if it isn't in vte.c
 * tap: this is for getting events for incoming text, it's separate from vte so code can't access it without explicit granting
 */

/* return values
 * -1 is invalid call, generally calling a function that's not available yet
 * -2 is invalid parameter, e.g. a negative length
 * -3 is an unexpected outcome
 * NULL is system error
 */

struct onetap { // we receive this from texttap's callback
	struct _script *script;
	unsigned int code;
	PyObject *receiver_ro; // RO: only use this for matching a remove request, otherwise check
	PyObject *weakref; // use this to request receiver (INCREF before call!)
	struct handle_texttap handle;

	struct onetap *next;
};

#define MAXTAPLEN	1024
struct _script {
	struct {
		FILE *stdout,*stderr;
	} stdio;
	struct {
		struct onetap *first;
		struct onetap *firstfree;
	} taps;
	struct {
		int bufferlenm1;
		char *buffer;
		int mfd;
		FILE *fakefout;
	} iotrap;
	struct {
		unsigned int maxlen;
		unsigned char *buffer;
	} clipboard;
	struct x11info *x11info;
	struct config *config;
	struct xclient *xclient;
	struct texttap *texttap;
	PyObject *pModule;
	PyObject *config_module,*vte_module,*tap_module;
	PyObject *tapscheck; // weakref callback, so we can remove looks if caller is destroyed
	unsigned int row,col;
	uint32_t underlinemask;
	uint32_t fgvaluemask,bgvaluemask;
	struct blockmem blockmem;
	unsigned int temp_tap[MAXTAPLEN];
};

int texttapcb_script(void *v, struct value_texttap *value);

static int getuint(unsigned int *ui_out, PyObject *pyo) {
unsigned long ul;
if (!PyLong_Check(pyo)) return -1;
ul=PyLong_AsUnsignedLong(pyo);
if (ul==(unsigned long)-1) {
	if (PyErr_Occurred()) { PyErr_Clear(); return -1; }
}
*ui_out=(unsigned int)ul;
return 0;
}
static int getint(int *i_out, PyObject *pyo) {
if (!PyLong_Check(pyo)) return -1;
*i_out=(int)PyLong_AsLong(pyo);
return 0;
}

static int setstring(PyObject *dest, char *name, char *value) {
PyObject *v;
if (!(v=PyUnicode_DecodeFSDefault(value))) GOTOERROR;
if (PyObject_SetAttrString(dest,name,v)) GOTOERROR;
Py_DECREF(v);
return 0;
error:
	Py_XDECREF(v);
	return -1;
}

static int setint(PyObject *dest, char *name, int value) {
PyObject *val;
if (!(val=PyLong_FromLong((long)value))) GOTOERROR;
if (PyObject_SetAttrString(dest,name,val)) GOTOERROR;
Py_DECREF(val);
return 0;
error:
	Py_XDECREF(val);
	return -1;
}
static int setuint(PyObject *dest, char *name, unsigned int value) {
PyObject *val;
if (!(val=PyLong_FromUnsignedLong((unsigned long)value))) GOTOERROR;
if (PyObject_SetAttrString(dest,name,val)) GOTOERROR;
Py_DECREF(val);
return 0;
error:
	Py_XDECREF(val);
	return -1;
}

static int setuintdouble(PyObject *dest, char *name, unsigned int value1, unsigned int value2) {
PyObject *val,*v=NULL;
if (!(val=PyTuple_New(2))) GOTOERROR;
if (!(v=PyLong_FromUnsignedLong(value1))) GOTOERROR;
if (PyTuple_SetItem(val,0,v)) GOTOERROR; // steals ref
if (!(v=PyLong_FromUnsignedLong(value2))) GOTOERROR;
if (PyTuple_SetItem(val,1,v)) GOTOERROR;
v=NULL;

if (PyObject_SetAttrString(dest,name,val)) GOTOERROR;
Py_DECREF(val);
return 0;
error:
	Py_XDECREF(val);
	Py_XDECREF(v);
	return -1;
}
static int setuinttriple(PyObject *dest, char *name, unsigned int value1, unsigned int value2, unsigned int value3) {
PyObject *val,*v=NULL;
if (!(val=PyTuple_New(3))) GOTOERROR;
if (!(v=PyLong_FromUnsignedLong(value1))) GOTOERROR;
if (PyTuple_SetItem(val,0,v)) GOTOERROR; // steals ref
if (!(v=PyLong_FromUnsignedLong(value2))) GOTOERROR;
if (PyTuple_SetItem(val,1,v)) GOTOERROR;
if (!(v=PyLong_FromUnsignedLong(value3))) GOTOERROR;
if (PyTuple_SetItem(val,2,v)) GOTOERROR;
v=NULL;

if (PyObject_SetAttrString(dest,name,val)) GOTOERROR;
Py_DECREF(val);
return 0;
error:
	Py_XDECREF(val);
	Py_XDECREF(v);
	return -1;
}

static int int_noerr(PyObject *pyo) {
long l;
if (!PyLong_Check(pyo)) return 0;
l=PyLong_AsLong(pyo);
if (l==(long)-1) {
	if (PyErr_Occurred()) { PyErr_Clear(); return 0; }
}
return (int)l;
}
static unsigned int uint_noerr(PyObject *pyo) {
unsigned long ul;
if (!PyLong_Check(pyo)) return 0;
ul=PyLong_AsUnsignedLong(pyo);
if (ul==(unsigned long)-1) {
	if (PyErr_Occurred()) { PyErr_Clear(); return 0; }
}
return (unsigned int)ul;
}

static unsigned int intbyname_noerr(PyObject *src, char *name) {
PyObject *val;
int i;
if (!(val=PyObject_GetAttrString(src,name))) return 0;
i=int_noerr(val);
Py_DECREF(val);
return i;
}
static unsigned int uintbyname_noerr(PyObject *src, char *name) {
PyObject *val;
unsigned int ui;
if (!(val=PyObject_GetAttrString(src,name))) return 0;
ui=uint_noerr(val);
Py_DECREF(val);
return ui;
}

static void get2uintsbyname_noerr(int *isfound_out, unsigned int *dest2, PyObject *src, char *name) {
PyObject *val,*v;
int found=0;

if (!(val=PyObject_GetAttrString(src,name))) goto error;
if (!PyTuple_Check(val)) goto error;
if (2>PyTuple_GET_SIZE(val)) goto error;

v=PyTuple_GetItem(val,0); // borrowed ref
if (!getuint(&dest2[0],v)) found++;
v=PyTuple_GetItem(val,1);
if (!getuint(&dest2[1],v)) found++;

Py_DECREF(val);
*isfound_out=found;
return;
error:
	*isfound_out=0;
	Py_XDECREF(val);
}
static void get3uintsbyname_noerr(int *isfound_out, unsigned int *dest3, PyObject *src, char *name) {
PyObject *val,*v;
int found=0;

if (!(val=PyObject_GetAttrString(src,name))) goto error;
if (!PyTuple_Check(val)) goto error;
if (3>PyTuple_GET_SIZE(val)) goto error;

v=PyTuple_GetItem(val,0); // borrows ref
if (!getuint(&dest3[0],v)) found++;
v=PyTuple_GetItem(val,1);
if (!getuint(&dest3[1],v)) found++;
v=PyTuple_GetItem(val,2);
if (!getuint(&dest3[2],v)) found++;

Py_DECREF(val);
*isfound_out=found;
return;
error:
	*isfound_out=0;
	Py_XDECREF(val);
}

#if 0
static int getuint16byname(unsigned short *dest_out, PyObject *src, char *name) {
unsigned int ui;
if (getuint32byname(&ui,src,name)) return -1;
if (ui>65535) GOTOERROR;
*dest_out=ui;
return 0;
error:
	return -1;
}
#endif

static int getstringbyname(char *dest, unsigned int destlen, PyObject *src, char *name) {
PyObject *val;
const char *str;
Py_ssize_t len;
if (!(val=PyObject_GetAttrString(src,name))) {
	dest[0]='\0';
	return 0;
}
if (!PyUnicode_Check(val)) {
	dest[0]='\0';
	return 0;
}
if (!(str=PyUnicode_AsUTF8AndSize(val,&len))) GOTOERROR;
if (len>destlen) GOTOERROR;
memcpy(dest,str,len);
dest[len]='\0';
Py_DECREF(val);
return 0;
error:
	return -1;
}

static int setconfigcolors(PyObject *dest, char *name, struct colors_config *colors) {
PyObject *pycolors;
PyObject *pyc=NULL,*pyuc=NULL;
int i;

if (!(pycolors=PyList_New(16))) GOTOERROR;
for (i=0;i<16;i++) {
	if (!(pyc=PyTuple_New(3))) GOTOERROR;
	if (!(pyuc=PyLong_FromLong(colors->colors[i].r))) GOTOERROR; // docs => pyuc is static; checking could be ignored
	if (PyTuple_SetItem(pyc,0,pyuc)) GOTOERROR; // steals reference
	if (!(pyuc=PyLong_FromLong(colors->colors[i].g))) GOTOERROR;
	if (PyTuple_SetItem(pyc,1,pyuc)) GOTOERROR;
	if (!(pyuc=PyLong_FromLong(colors->colors[i].b))) GOTOERROR;
	if (PyTuple_SetItem(pyc,2,pyuc)) GOTOERROR;
	pyuc=NULL;

	if (PyList_SetItem(pycolors,i,pyc)) GOTOERROR; // steals ref
	pyc=NULL;
}

if (PyObject_SetAttrString(dest,name,pycolors)) GOTOERROR;
Py_DECREF(pycolors);
return 0;
error:
	Py_XDECREF(pycolors);
	Py_XDECREF(pyc);
	Py_XDECREF(pyuc);
	return -1;
}

static int getconfigcolors(struct colors_config *dest, PyObject *src, char *name) {
PyObject *val;
Py_ssize_t count;
unsigned int ui;
struct color_config *color;

if (!(val=PyObject_GetAttrString(src,name))) {
	return 0;
}
if (!PyList_Check(val)) GOTOERROR;
count=PyList_GET_SIZE(val);
if (count>16) count=16;
color=dest->colors;
for (ui=0;ui<count;ui++,color++) {
	PyObject *rgb,*pc;
	if (!(rgb=PyList_GetItem(val,ui))) continue; // borrowed ref
	if (!PyTuple_Check(rgb)) continue;
	if (3!=PyTuple_GET_SIZE(rgb)) continue;
	pc=PyTuple_GetItem(rgb,0); color->r=(unsigned char)uint_noerr(pc); // borrowed ref
	pc=PyTuple_GetItem(rgb,1); color->g=(unsigned char)uint_noerr(pc);
	pc=PyTuple_GetItem(rgb,2); color->b=(unsigned char)uint_noerr(pc);
}
Py_DECREF(val);
return 0;
error:
	Py_XDECREF(val);
	return -1;
}

static int getexecargs(int *badarg_out, char ***args_out, struct _script *s, PyObject *pyo, char *name) {
PyObject *list;
char **args;
int i;
Py_ssize_t count;

if (!PyObject_HasAttrString(pyo,name)) return 0;
if (!(list=PyObject_GetAttrString(pyo,name))) GOTOERROR;
if (!PyList_Check(list)) goto badarg;
count=PyList_GET_SIZE(list);
if (!(args=ALLOC2_blockmem(&s->blockmem,char *,count+1))) GOTOERROR;
args[count]=NULL;
for (i=0;i<count;i++) {
	PyObject *a;
	Py_ssize_t len;
	const char *str;
	if (!(a=PyList_GetItem(list,i))) goto badarg; // borrowed ref
	if (!PyUnicode_Check(a)) goto badarg;
	if (!(str=PyUnicode_AsUTF8AndSize(a,&len))) GOTOERROR;
	if (!(args[i]=(char *)memdup_blockmem(&s->blockmem,(unsigned char *)str,len+1))) GOTOERROR;
}

*badarg_out=0;
*args_out=args;
return 0;
badarg:
	*badarg_out=1;
	return 0;
error:
	return -1;
}

static int restoreconfig(struct _script *script, struct config *config, int isinit) {
PyObject *src;
src=script->config_module;

if (isinit) {
	int badarg;
	if (getexecargs(&badarg,&config->cmdline,script,src,"cmdline")) GOTOERROR;
}

if (getconfigcolors(&config->darkmode,src,"darkmode")) GOTOERROR;
if (getconfigcolors(&config->lightmode,src,"lightmode")) GOTOERROR;
if (getstringbyname(config->exportterm,MAX_EXPORTTERM_CONFIG,src,"exportterm")) GOTOERROR;
if (getstringbyname(config->typeface,MAX_TYPEFACE_CONFIG,src,"typeface")) GOTOERROR;

{
	unsigned int two[2]={0,0};
	int isfound;
	(void)get2uintsbyname_noerr(&isfound,two,src,"windims");
	if (isfound==2) {
		config->xwidth=two[0];
		config->xheight=two[1];
	}
}
{
	unsigned int two[2]={0,0};
	int isfound;
	(void)get2uintsbyname_noerr(&isfound,two,src,"celldims");
	if (isfound==2) {
		config->cellw=two[0];
		config->cellh=two[1];
	}
}
config->columns=uintbyname_noerr(src,"columns");
config->rows=uintbyname_noerr(src,"rows");
config->cursorheight=uintbyname_noerr(src,"cursorheight");
config->cursoryoff=uintbyname_noerr(src,"cursoryoff");
config->font0shift=intbyname_noerr(src,"font0shift");
config->font0line=intbyname_noerr(src,"font0line");
config->fontulline=intbyname_noerr(src,"fontulline");
config->fontullines=intbyname_noerr(src,"fontullines");
config->charcache=uintbyname_noerr(src,"charcache");
config->depth=uintbyname_noerr(src,"depth");
{
	unsigned int triple[3]={0,0,0};
	int isfound;
	(void)get3uintsbyname_noerr(&isfound,triple,src,"rgb_cursor");
	if (isfound) { // default to black
		config->red_cursor=(triple[0]<<8)|triple[0];
		config->green_cursor=(triple[1]<<8)|triple[1];
		config->blue_cursor=(triple[2]<<8)|triple[2];
	}
}

{
	unsigned int ui;
	ui=uintbyname_noerr(src,"isfullscreen");
	config->isfullscreen=(ui)?1:0;
	ui=uintbyname_noerr(src,"isdarkmode");
	config->isdarkmode=(ui)?1:0;
	ui=uintbyname_noerr(src,"isblinkcursor");
	config->isblinkcursor=(ui)?1:0;
	ui=uintbyname_noerr(src,"isnostart");
	config->isnostart=(ui)?1:0;
}

#if 0
// this is auto-calc'd in config
{
	unsigned int two[2]={0,0};
	int isfound;
	if (getuint32doublebyname(&isfound,two,src,"offset")) GOTOERROR;
	if (isfound==2) {
		config->xoff=two[0];
		config->yoff=two[1];
	}
}
#endif

{
	unsigned int two[2]={0,0};
	int isfound;
	(void)get2uintsbyname_noerr(&isfound,two,src,"screendims");
	if (isfound==2) {
		config->screen.width=two[0];
		config->screen.height=two[1];
	}
}

{
	unsigned int two[2]={0,0};
	int isfound;
	(void)get2uintsbyname_noerr(&isfound,two,src,"mm_screendims");
	if (isfound==2) {
		config->screen.widthmm=two[0];
		config->screen.heightmm=two[1];
	}
}
return 0;
error:
	return -1;
}
static int storeconfig(struct _script *script, struct config *config) {
PyObject *dest;
dest=script->config_module;

if (setconfigcolors(dest,"darkmode",&config->darkmode)) GOTOERROR;
if (setconfigcolors(dest,"lightmode",&config->lightmode)) GOTOERROR;
if (setstring(dest,"exportterm",config->exportterm)) GOTOERROR;
if (setstring(dest,"typeface",config->typeface)) GOTOERROR;
if (setuintdouble(dest,"windims",config->xwidth,config->xheight)) GOTOERROR;
if (setuintdouble(dest,"celldims",config->cellw,config->cellh)) GOTOERROR;
if (setuint(dest,"columns",config->columns)) GOTOERROR;
if (setuint(dest,"rows",config->rows)) GOTOERROR;
if (setuint(dest,"cursorheight",config->cursorheight)) GOTOERROR;
if (setuint(dest,"cursoryoff",config->cursoryoff)) GOTOERROR;
if (setint(dest,"font0shift",config->font0shift)) GOTOERROR;
if (setint(dest,"font0line",config->font0line)) GOTOERROR;
if (setint(dest,"fontulline",config->fontulline)) GOTOERROR;
if (setint(dest,"fontullines",config->fontullines)) GOTOERROR;
if (setuint(dest,"charcache",config->charcache)) GOTOERROR;
if (setuinttriple(dest,"rgb_cursor",config->red_cursor>>8,config->green_cursor>>8,config->blue_cursor>>8)) GOTOERROR;
if (setuint(dest,"isfullscreen",config->isfullscreen)) GOTOERROR;
if (setuint(dest,"isdarkmode",config->isdarkmode)) GOTOERROR;
if (setuint(dest,"isblinkcursor",config->isblinkcursor)) GOTOERROR;
if (setuintdouble(dest,"offset",config->xoff,config->yoff)) GOTOERROR;
if (setuintdouble(dest,"screendims",config->screen.width,config->screen.height)) GOTOERROR;
if (setuintdouble(dest,"mm_screendims",config->screen.widthmm,config->screen.heightmm)) GOTOERROR;
if (setuint(dest,"isnostart",config->isnostart)) GOTOERROR;
if (setuint(dest,"depth",config->depth)) GOTOERROR;
return 0;
error:
	return -1;
}


static uint32_t ucs4tovalue(struct _script *s, uint32_t ucs4) {
uint32_t value;
value=ucs4;
value|=s->underlinemask; // bit 30
#if 0
if (value==32) { // whitespace gets foreground 0
	value|=s->bgvaluemask; // bits 22..25
} else {
	value|=s->bgvaluemask; // bits 22..25
	value|=s->fgvaluemask; // bits 26..29
}
#else
value|=s->bgvaluemask; // bits 22..25
value|=s->fgvaluemask; // bits 26..29
#endif
return value;
}

static inline int printargs(Py_ssize_t argc, PyObject *const *argv) {
PyObject *repr;
while (argc) {
	Py_UCS1 *u;
	PyTypeObject *ptype;

	ptype=(*argv)->ob_type;

	repr=PyObject_Repr(*argv);
	if (!repr) GOTOERROR;
	u=PyUnicode_1BYTE_DATA(repr);
	if (!u) GOTOERROR;

	fprintf(stderr,"Arg: %s (%s)\n",(char *)u,ptype->tp_name);
	Py_DECREF(repr);
	
	argc--;
	argv++;
}
return 0;
error:
	return -1;
}
static int clearline(struct _script *s, unsigned int r) {
unsigned int c;
uint32_t value;
struct xclient *xc=s->xclient;
value=ucs4tovalue(s,32);
c=s->config->columns;
while (c) {
	c--;
	if (addchar_xclient(xc,value,r,c)) GOTOERROR;
}
return 0;
error:
	return -1;
}
static PyObject *vte_setcursor(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_setcursor v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (setcursor_xclient(script->xclient,script->row,script->col)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_unsetcursor(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_unsetcursor v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (unset_cursor(script->xclient->baggage.cursor)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_clear(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_clear v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (clrscr_xclient(script->xclient, script->fgvaluemask,script->bgvaluemask)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_clearlines(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_clearlines v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (!argc) {
	if (clearline(script,script->row)) return NULL;
} else {
	unsigned int row,count=1;
	if (getuint(&row,argv[0])) return PyLong_FromLong(-2);
	if (argc>1) {
		if (getuint(&count,argv[1])) return PyLong_FromLong(-2);
		count%=script->config->rows;
	}
	while (count) {
		row%=script->config->rows;
		if (clearline(script,row)) return NULL;
		row++;
		count--;
	}
}
XFlush(script->xclient->baggage.x->display);
return PyLong_FromLong(0);
}
static PyObject *vte_movewindow(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
int x=0,y=0;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_movewindow v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc) {
	if (getint(&x,argv[0])) return PyLong_FromLong(-2);
	if (argc>1) {
		if (getint(&y,argv[1])) return PyLong_FromLong(-2);
	}
}
if (movewindow_xclient(script->xclient,x,y)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_moveto(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_moveto v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (!argc) {
	script->row=script->col=0;
} else {
	if (getuint(&script->row,argv[0])) return PyLong_FromLong(-2);
	if (argc>1) {
		if (getuint(&script->col,argv[1])) return PyLong_FromLong(-2);
	}
}
return PyLong_FromLong(0);
}
static PyObject *vte_setcolors(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int fore,back;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_setcolors v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (!argc) {
	script->fgvaluemask=(uint32_t)15<<25;
	script->bgvaluemask=0;
} else {
	if (getuint(&fore,argv[0])) return PyLong_FromLong(-2);
	fore=fore&15;
	script->fgvaluemask=fore<<25;
	if (argc>1) {
		if (getuint(&back,argv[1])) return PyLong_FromLong(-2);
		back=back&15;
		script->bgvaluemask=back<<21;
	}
}
return PyLong_FromLong(0);
}

static struct onetap *getonetap(struct _script *s) {
struct onetap *ot;
if (s->taps.firstfree) {
	ot=s->taps.firstfree;
	s->taps.firstfree=ot->next;
	return ot;
}
if (!(ot=ALLOC_blockmem(&s->blockmem,struct onetap))) return NULL;
return ot;
}

static int fill_temp_tap(struct _script *s, PyObject *pyo) {
// pyo should have already passed PyUnicode_Check
unsigned int ui,len;
unsigned int *dest;
void *data;
int kind;

len=PyUnicode_GET_LENGTH(pyo);
if (len>=MAXTAPLEN) return -1;
dest=s->temp_tap;
data=PyUnicode_DATA(pyo);
kind=PyUnicode_KIND(pyo);
for (ui=0;ui<len;ui++) {
	dest[ui]=PyUnicode_READ(kind,data,ui);
}
dest[ui]=0;
return 0;
}

static void rmonetap(int *isfound_out, struct _script *s, PyObject *dest, unsigned int code) {
struct onetap *ot,**ppn;
int isfound=0,ttisf;
ppn=&s->taps.first;
ot=s->taps.first;
while (ot) {
	if (ot->receiver_ro==dest) {
		if ( (!code) || (code==ot->code)) {
			struct onetap *next;
			isfound+=1;
			next=ot->next;
			*ppn=next;
			Py_DECREF(ot->weakref);

fprintf(stderr,"%s:%d removing tap (%p) %p:%u\n",__FILE__,__LINE__,ot,ot->receiver_ro,ot->code);
			(void)handle_remove_texttap(&ttisf,s->texttap,&ot->handle);
#ifdef DEBUG
			if (!ttisf) WHEREAMI;
#endif

			ot->next=s->taps.firstfree;
			s->taps.firstfree=ot;
			if (code) break;
			ot=next;
			continue;
		}
	}

	ppn=&ot->next;
	ot=ot->next;
}
*isfound_out=isfound;
}

static PyObject *tap_rmlook(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
PyObject *dest,*code;
struct _script **v,*script;
int isfound;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"tap_rmlook v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<2) return PyLong_FromLong(-2);
dest=argv[0];
code=argv[1];
if (!PyLong_Check(code)) return PyLong_FromLong(-2);

(void)rmonetap(&isfound,script,dest,PyLong_AsUnsignedLong(code));
if (!isfound) return PyLong_FromLong(-3);
return PyLong_FromLong(0);
}

static PyObject *tap_addlook(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
PyObject *dest,*match,*code,*wr=NULL;
struct _script **v,*script;
struct onetap *ot=NULL;
unsigned int uicode;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"tap_addlook v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<3) return PyLong_FromLong(-2);
dest=argv[0];
if (!PyObject_HasAttrString(dest,"OnLook")) return PyLong_FromLong(-3);
code=argv[1];
if (!PyLong_Check(code)) return PyLong_FromLong(-2);
if (!(uicode=PyLong_AsUnsignedLong(code))) return PyLong_FromLong(-2); // 0 is reserved for clear all
match=argv[2];
if (!PyUnicode_Check(match)) return PyLong_FromLong(-2);
if (fill_temp_tap(script,match)) GOTOERROR; // fills script->temp_tap for later

if (!(wr=PyWeakref_NewRef(dest,script->tapscheck))) GOTOERROR;

if (!(ot=getonetap(script))) GOTOERROR;
ot->script=script;
ot->code=uicode;
ot->receiver_ro=dest;
ot->weakref=wr;

// fprintf(stderr,"%s:%d adding tap (%p) %p:%u\n",__FILE__,__LINE__,ot,dest,uicode);
if (!look_texttap(script->texttap,&ot->handle,script->temp_tap,texttapcb_script,ot)) GOTOERROR;

ot->next=script->taps.first;
script->taps.first=ot;

return PyLong_FromLong(0);
error:
	Py_XDECREF(wr);
	if (ot) { ot->next=script->taps.firstfree; script->taps.firstfree=ot; }
	return NULL;
}
static PyObject *vte_cursorheight(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int height,yoff;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_cursorheight v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc) {
	if (getuint(&height,argv[0])) return PyLong_FromLong(-2);
	if (height>script->config->cellh) height=script->config->cellh;
	yoff=script->config->cellh-height;
} else {
	height=script->config->cursorheight;
	yoff=script->config->cursoryoff;
}
if (cursoronoff_xclient(script->xclient,height,yoff)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_setalarm(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int count;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_setalarm v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<1) return PyLong_FromLong(-2);
if (getuint(&count,argv[0])) return PyLong_FromLong(-2);
(void)setalarm_xclient(script->xclient,count);
return PyLong_FromLong(0);
}
static PyObject *vte_grabpointer(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int toggle=0;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_grabpointer v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc) {
	if (getuint(&toggle,argv[0])) return PyLong_FromLong(-2);
}
if (grabpointer_xclient(script->xclient,toggle)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_drawtoggle(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int toggle=0;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_drawtoggle v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc) {
	if (getuint(&toggle,argv[0])) return PyLong_FromLong(-2);
}

if (toggle) {
	if (drawon_xclient(script->xclient)) return NULL;
} else if (drawoff_xclient(script->xclient)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_time(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int t;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_time v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
t=(unsigned int)time(NULL);
return PyLong_FromUnsignedLong((unsigned long)t);
}
static PyObject *vte_milliseconds(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
struct timespec ts;
unsigned int t;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_milliseconds v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
t=0;
if (!clock_gettime(CLOCK_MONOTONIC,&ts)) {
	t=ts.tv_sec*1000;
	t+=ts.tv_nsec/(1000*1000);
}
return PyLong_FromUnsignedLong((unsigned long)t);
}
static PyObject *vte_setunderline(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int isset;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_setunderline v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<1) return PyLong_FromLong(-2);
if (getuint(&isset,argv[0])) return PyLong_FromLong(-2);
if (isset) script->underlinemask=UNDERLINEBIT_VALUE;
else script->underlinemask=0;
return PyLong_FromLong(0);
}

static PyObject *vte_drawstring(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
const PyObject *pyo;
Py_ssize_t len;
Py_UCS4 ucs;
int kind;
void *data;
int i;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_drawstring v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<1) return PyLong_FromLong(-1);
pyo=*argv;
if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
if (PyUnicode_READY(pyo)) return NULL;
data=PyUnicode_DATA(pyo);
len=PyUnicode_GET_LENGTH(pyo);
kind=PyUnicode_KIND(pyo);
for (i=0;i<len;i++) {
	uint32_t u32;
//	ucs=PyUnicode_READ_CHAR(pyo,i);
	ucs=PyUnicode_READ(kind,data,i);
	u32=ucs4tovalue(script,ucs);
	if (addchar_xclient(script->xclient,u32,script->row,script->col)) return NULL;
	script->col=(script->col+1)%script->config->columns;
}
XFlush(script->xclient->baggage.x->display);
return PyLong_FromLong(0);
}
static PyObject *vte_restorerect(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int x,y,width,height;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_restorerect v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<4) goto badarg;
if (getuint(&x,argv[0])) goto badarg;
if (getuint(&y,argv[1])) goto badarg;
if (getuint(&width,argv[2])) goto badarg;
if (getuint(&height,argv[3])) goto badarg;
if (restorerect_xclient(script->xclient,x,y,width,height)) return NULL;
return PyLong_FromLong(0);
badarg:
	return PyLong_FromLong(-2);
}
static PyObject *vte_select(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int a1,a2,a3,a4;
char *selection;
Py_ssize_t len;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_select v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<4) goto badarg;
if (getuint(&a1,argv[0])) goto badarg;
if (getuint(&a2,argv[1])) goto badarg;
if (getuint(&a3,argv[2])) goto badarg;
if (getuint(&a4,argv[3])) goto badarg;
if (!isvalid_setselection_xclient(script->xclient,a1,a2,a3,a4)) goto badarg;
if (argc>4) {
	PyObject *pyo;
	pyo=argv[4];
	if (!PyUnicode_Check(pyo)) goto badarg;
	if (!(selection=(char *)PyUnicode_AsUTF8AndSize(pyo,&len))) return NULL;
} else {
	selection="PRIMARY";
	len=7;
}

if (setselection_xclient(script->xclient,RAW_MODE_SELECTION_SURFACE_XCLIENT,a1,a2,a3,a4)) return NULL;
if (copyselection_xclient(script->xclient,(unsigned char *)selection,len)) return NULL;
return PyLong_FromLong(0);
badarg:
	return PyLong_FromLong(-2);
}
static PyObject *vte_fillrect(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int color=15,ms=150,x,y,width,height;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_fillrect v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<4) goto badarg;
if (getuint(&x,argv[0])) goto badarg;
if (getuint(&y,argv[1])) goto badarg;
if (getuint(&width,argv[2])) goto badarg;
if (getuint(&height,argv[3])) goto badarg;
if (argc>4) {
	if (getuint(&color,argv[4])) goto badarg;
	if (argc>5) {
		if (getuint(&ms,argv[5])) goto badarg;
	}
}
if (fillrect_xclient(script->xclient,x,y,width,height,color,ms)) return NULL;
return PyLong_FromLong(0);
badarg:
	return PyLong_FromLong(-2);
}
static PyObject *vte_fillpadding(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int color=0,ms=0;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_fillpadding v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc>0) {
	if (getuint(&color,argv[0])) return PyLong_FromLong(-2);
	if (argc>1) {
		if (getuint(&ms,argv[1])) return PyLong_FromLong(-2);
	}
}
if (fillpadding_xclient(script->xclient,color,ms)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_settitle(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
PyObject *pyo;
Py_ssize_t len;
const char *str;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_settitle v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (argc<1) return PyLong_FromLong(-2);
pyo=*argv;
if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
if (!(str=PyUnicode_AsUTF8AndSize(pyo,&len))) return NULL;

{
	struct x11info *x=script->xclient->baggage.x;
	XStoreName(x->display,x->window,str);
}
return PyLong_FromLong(0);
}
static PyObject *vte_paste(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
PyObject *pyo;
Py_ssize_t selectionlen;
char *selection;
unsigned int cliplen;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_paste v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (argc<1) {
	selection="PRIMARY";
	selectionlen=7;
} else {
	pyo=*argv;
	if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
	if (!(selection=(char *)PyUnicode_AsUTF8AndSize(pyo,&selectionlen))) return NULL;
}
if (paste_xclient(&cliplen,script->xclient,(unsigned char *)selection,selectionlen,5)) return NULL;
if (cliplen>script->clipboard.maxlen) {
	unsigned char *temp;
	unsigned int max;
	max=cliplen+1024;
	if (!(temp=realloc(script->clipboard.buffer,max))) return NULL;
	script->clipboard.buffer=temp;
	script->clipboard.maxlen=max;
}
if (getpaste_xclient(script->xclient,script->clipboard.buffer,cliplen)) return NULL;
pyo=PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND,script->clipboard.buffer,cliplen);
return pyo;
}

static PyObject *vte_copy(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
PyObject *pyo;
char *selection;
Py_ssize_t selectionlen;
const char *text;
Py_ssize_t textlen;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_copy v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (argc==1) {
	pyo=argv[0];
	selection="PRIMARY";
	selectionlen=7;
} else if (argc==2) {
	pyo=argv[0];
	if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
	if (!(selection=(char *)PyUnicode_AsUTF8AndSize(pyo,&selectionlen))) return NULL;
	pyo=argv[1];
} else return PyLong_FromLong(-2);
if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
if (!(text=PyUnicode_AsUTF8AndSize(pyo,&textlen))) return NULL;

if (copy_xclient(script->xclient,(unsigned char *)selection,selectionlen,(unsigned char *)text,textlen)) return NULL;
return PyLong_FromLong(0);
}

static PyObject *vte_stderr(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
// NOTE calls that don't require xclient may be called from OnInitBegin
// at that point, stdout and stderr are faked and limited in output length
// we can use script->stdio.stderr/out to get the original stderr/out 
struct _script **v,*script;
PyObject *pyo;
Py_ssize_t len;
const char *str;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_stderr v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (argc<1) return PyLong_FromLong(-2);
pyo=*argv;
if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
if (!(str=PyUnicode_AsUTF8AndSize(pyo,&len))) return NULL;

fputs("vte.stderr: ",script->stdio.stderr);
fwrite(str,len,1,script->stdio.stderr);
fputc('\n',script->stdio.stderr);
return PyLong_FromLong(0);
}
static PyObject *vte_stdout(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
PyObject *pyo;
Py_ssize_t len;
const char *str;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_stdout v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (argc<1) return PyLong_FromLong(-2);
pyo=*argv;
if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
if (!(str=PyUnicode_AsUTF8AndSize(pyo,&len))) return NULL;

fwrite(str,len,1,script->stdio.stdout);
fputc('\n',script->stdio.stdout);
fflush(script->stdio.stdout);
return PyLong_FromLong(0);
}
static PyObject *vte_ispaused(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_ispaused v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (script->xclient->ispaused) return PyLong_FromLong(1);
return PyLong_FromLong(0);
}
static PyObject *vte_pause(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_pause v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (pause_xclient(script->xclient)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_unpause(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_unpause v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (unpause_xclient(script->xclient)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_savetext(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_savetext v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
(void)savebacking_xclient(script->xclient);
return PyLong_FromLong(0);
}
static PyObject *vte_restoretext(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_restoretext v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (restorebacking_xclient(script->xclient)) return NULL;
return PyLong_FromLong(0);
}
static PyObject *vte_visualbell(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int color=0,ms=150;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_visualbell v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc>0) {
	if (getuint(&color,argv[0])) return PyLong_FromLong(-2);
	if (argc>1) {
		if (getuint(&ms,argv[1])) return PyLong_FromLong(-2);
	}
}
if (visualbell_xclient(script->xclient,color,ms)) return NULL;
return PyLong_FromLong(0);
}

static PyObject *vte_xbell(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
int percent=0;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_xbell v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc>0) {
	if (getint(&percent,argv[0])) return PyLong_FromLong(-2);
}
if (xbell_xclient(script->xclient,percent)) return NULL;
return PyLong_FromLong(0);
}

static PyObject *vte_setpointer(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
int code=0;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_setpointer v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc>0) {
	if (getint(&code,argv[0])) return PyLong_FromLong(-2);
}
if (setpointer_xclient(script->xclient,code)) return NULL;
return PyLong_FromLong(0);
}

static PyObject *vte_scrollback(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
int delta=0;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_scrollback v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if ((argc<1) || (getint(&delta,argv[0]))) return PyLong_FromLong(-2);
if (scrollback_xclient(script->xclient,delta)) return NULL;
return PyLong_FromLong(0);
}

static PyObject *vte_send(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
const char *letters;
Py_ssize_t len;
int isdrop;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_send v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<1) PyLong_FromLong(-2);
if (!PyUnicode_Check(argv[0])) return PyLong_FromLong(-2);
if (!(letters=PyUnicode_AsUTF8AndSize(argv[0],&len))) GOTOERROR;
if (send_xclient(&isdrop,script->xclient,(unsigned char *)letters,len)) return NULL;
if (isdrop) return PyLong_FromLong(-3);
return PyLong_FromLong(0);
error:
	return NULL;
}

static PyObject *vte_fetchline(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
unsigned int row,col,count;
uint32_t *backing;
PyObject *pyo;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_fetchline v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (argc<3) return PyLong_FromLong(-2);
if (getuint(&row,argv[0])) return PyLong_FromLong(-2);
if (getuint(&col,argv[1])) return PyLong_FromLong(-2);
if (getuint(&count,argv[2])) return PyLong_FromLong(-2);

backing=fetchline_xclient(script->xclient,row,col,count);
if (!backing) return PyLong_FromLong(-2);
for (col=0;col<count;col++) backing[col]&=(1<<21)-1;
pyo=PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND,backing,count);
return pyo;
}

static PyObject *vte_fetchcharpos(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
PyObject *pyo=NULL,*pyr=NULL,*pyc=NULL;;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"vte_fetchcharpos v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);
if (!(pyo=PyTuple_New(2))) GOTOERROR;
if (!(pyr=PyLong_FromUnsignedLong(script->xclient->status.lastaddchar.row))) GOTOERROR;
if (!(pyc=PyLong_FromUnsignedLong(script->xclient->status.lastaddchar.col))) GOTOERROR;
if (PyTuple_SetItem(pyo,0,pyr)) GOTOERROR; pyr=NULL; // steals ref
if (PyTuple_SetItem(pyo,1,pyc)) GOTOERROR; pyc=NULL; // steals ref
return pyo;
error:
	Py_XDECREF(pyo);
	Py_XDECREF(pyr);
	Py_XDECREF(pyc);
	return NULL;
}

static PyMethodDef VteMethods[]={
	{"clear",(PyCFunction)vte_clear,METH_FASTCALL,"Clear screen."},
	{"clearlines",(PyCFunction)vte_clearlines,METH_FASTCALL,"Clear a row on the screen."},
	{"copy",(PyCFunction)vte_copy,METH_FASTCALL,"Copy text to a clipboard."},
	{"cursorheight",(PyCFunction)vte_cursorheight,METH_FASTCALL,"Changes height of the cursor."},
	{"drawtoggle",(PyCFunction)vte_drawtoggle,METH_FASTCALL,"Process input without drawing to the screen."},
	{"drawstring",(PyCFunction)vte_drawstring,METH_FASTCALL,"Draw a unicode string to the screen."},
	{"fillpadding",(PyCFunction)vte_fillpadding,METH_FASTCALL,"Draw color in the window padding."},
	{"fillrect",(PyCFunction)vte_fillrect,METH_FASTCALL,"Draw rectangle on the window."},
	{"fetchline",(PyCFunction)vte_fetchline,METH_FASTCALL,"Fetch a line from the screen."},
	{"fetchcharpos",(PyCFunction)vte_fetchcharpos,METH_FASTCALL,"Fetch the position of the last character."},
	{"grabpointer",(PyCFunction)vte_grabpointer,METH_FASTCALL,"Capture all events from the mouse."},
	{"ispaused",(PyCFunction)vte_ispaused,METH_FASTCALL,"Check if the terminal is paused."},
	{"milliseconds",(PyCFunction)vte_milliseconds,METH_FASTCALL,"Milliseconds since some unspecified moment."},
	{"moveto",(PyCFunction)vte_moveto,METH_FASTCALL,"Position to draw on the screen."},
	{"movewindow",(PyCFunction)vte_movewindow,METH_FASTCALL,"Move window on the screen."},
	{"paste",(PyCFunction)vte_paste,METH_FASTCALL,"Fetch text from a clipboard."},
	{"pause",(PyCFunction)vte_pause,METH_FASTCALL,"Pause the terminal."},
	{"restoretext",(PyCFunction)vte_restoretext,METH_FASTCALL,"Restore the drawn text from the buffer."},
	{"restorerect",(PyCFunction)vte_restorerect,METH_FASTCALL,"Redraw screen from last draw."},
	{"savetext",(PyCFunction)vte_savetext,METH_FASTCALL,"Save the drawn text to a buffer."},
	{"scrollback",(PyCFunction)vte_scrollback,METH_FASTCALL,"Scroll the history."},
	{"select",(PyCFunction)vte_select,METH_FASTCALL,"Select text for clipboard."},
	{"send",(PyCFunction)vte_send,METH_FASTCALL,"Send characters as if they were typed."},
	{"setalarm",(PyCFunction)vte_setalarm,METH_FASTCALL,"Set a timer for a callback."},
	{"setcolors",(PyCFunction)vte_setcolors,METH_FASTCALL,"Set foreground,background color pair for text."},
	{"setcursor",(PyCFunction)vte_setcursor,METH_FASTCALL,"Place a blinking cursor."},
	{"setpointer",(PyCFunction)vte_setpointer,METH_FASTCALL,"Choose a mouse cursor."},
	{"settitle",(PyCFunction)vte_settitle,METH_FASTCALL,"Set the window's title."},
	{"setunderline",(PyCFunction)vte_setunderline,METH_FASTCALL,"Enable/disable underline text mode."},
	{"stderr",(PyCFunction)vte_stderr,METH_FASTCALL,"Print to the terminal's stderr."},
	{"stdout",(PyCFunction)vte_stdout,METH_FASTCALL,"Print to the terminal's stdout."},
	{"time",(PyCFunction)vte_time,METH_FASTCALL,"Seconds since the epoch."},
	{"unpause",(PyCFunction)vte_unpause,METH_FASTCALL,"Unpause the terminal."},
	{"unsetcursor",(PyCFunction)vte_unsetcursor,METH_FASTCALL,"Hide blinking text cursor."},
	{"visualbell",(PyCFunction)vte_visualbell,METH_FASTCALL,"Flash the screen."},
	{"xbell",(PyCFunction)vte_xbell,METH_FASTCALL,"Beep the horn."},
	{NULL,NULL,0,NULL}
};

static PyModuleDef VteModule={
	PyModuleDef_HEAD_INIT,"vte",NULL,sizeof(void*),VteMethods,
	NULL, NULL, NULL, NULL
};

static inline int uneq_colors(struct colors_config *a, struct colors_config *b) {
return memcmp(a,b,sizeof(struct colors_config));
}

static PyObject *config_apply(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
struct config config;
v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"config_apply v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->xclient) return PyLong_FromLong(-1);

if (restoreconfig(script,&config,0)) return NULL;
(void)recalc_config(&config);

if ( (config.font0shift!=script->config->font0shift) || (config.font0line!=script->config->font0line) ||
		(config.fontulline!=script->config->fontulline) || (config.fontullines!=script->config->fontullines) ) {
	script->config->font0shift=config.font0shift;
	script->config->font0line=config.font0line;
	script->config->fontulline=config.fontulline;
	script->config->fontullines=config.fontullines;
	(void)setparams_xclient(script->xclient, config.font0shift, config.font0line, config.fontulline, config.fontullines);
}
if ((config.cursorheight!=script->config->cursorheight)||(config.isblinkcursor!=script->config->isblinkcursor)) {
	script->config->cursorheight=config.cursorheight;
	if (resizecursor_xclient(script->xclient,config.cellw,config.cellh,config.cursorheight,config.cursoryoff,
			config.isblinkcursor)) return NULL;
}
if (config.cursoryoff!=script->config->cursoryoff) {
	script->config->cursoryoff=config.cursoryoff;
	if (resizecursor_xclient(script->xclient,config.cellw,config.cellh,config.cursorheight,config.cursoryoff,
			config.isblinkcursor)) return NULL;
}
if ( (config.cellw!=script->config->cellw) || (config.cellh!=script->config->cellh) ) {
	script->config->cellw=config.cellw;
	script->config->cellh=config.cellh;
	if (resizecell_xclient(script->xclient,config.cellw,config.cellh)) return NULL;
	if (resizecursor_xclient(script->xclient,config.cellw,config.cellh,config.cursorheight,config.cursoryoff,
			config.isblinkcursor)) return NULL;
}
if ( (config.xwidth!=script->config->xwidth)|| (config.xheight!=script->config->xheight) ) {
	script->config->xwidth=config.xwidth;
	script->config->xheight=config.xheight;
	if (resizewindow_xclient(script->xclient,config.xwidth,config.xheight)) return NULL;
	if (resizecursor_xclient(script->xclient,config.cellw,config.cellh,config.cursorheight,config.cursoryoff,
			config.isblinkcursor)) return NULL;
}
if ( (config.columns!=script->config->columns) || (config.rows!=script->config->rows) ) {
	script->config->columns=config.columns;
	script->config->rows=config.rows;
	if (resizesurface_xclient(script->xclient,config.rows,config.columns)) return NULL;
	if (resizecursor_xclient(script->xclient,config.cellw,config.cellh,config.cursorheight,config.cursoryoff,
			config.isblinkcursor)) return NULL;
}
if (strcmp(config.typeface,script->config->typeface)) {
	strcpy(script->config->typeface,config.typeface);
	if (changefont_xclient(script->xclient,config.typeface)) return NULL;
}
if (config.isdarkmode!=script->config->isdarkmode) {
	script->config->isdarkmode^=1;
	if (setuint(script->config_module,"isdarkmode",script->config->isdarkmode)) return NULL;
	if (fixcolors_xclient(script->xclient)) return NULL;
}
if ( (config.red_cursor!=script->config->red_cursor)
		|| (config.green_cursor!=script->config->green_cursor)
		|| (config.blue_cursor!=script->config->blue_cursor) ) {
	script->config->red_cursor=config.red_cursor;
	script->config->green_cursor=config.green_cursor;
	script->config->blue_cursor=config.blue_cursor;
	if (setcursorcolors_xclient(script->xclient,config.red_cursor,config.green_cursor,config.blue_cursor)) return NULL;
}
if (uneq_colors(&config.darkmode,&script->config->darkmode) || uneq_colors(&config.lightmode,&script->config->lightmode)) {
	script->config->darkmode=config.darkmode;
	script->config->lightmode=config.lightmode;
	if (fixcolors_xclient(script->xclient)) return NULL;
}
if (reconfig_xclient(script->xclient)) return NULL;
return PyLong_FromLong(0);
}

static PyObject *config_queryfont(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
PyObject *pyo,*ret;
const char *text;
struct queryfont_xftchar qf;

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"config_queryfont v=%p argc=%d\n",v,argc);
if (!v) return NULL;
if (!(script=*v)) return NULL;
if (!script->x11info) return PyLong_FromLong(-1);
if (argc!=1) return PyLong_FromLong(-2);
pyo=argv[0];
if (!PyUnicode_Check(pyo)) return PyLong_FromLong(-2);
if (!(text=PyUnicode_AsUTF8(pyo))) return NULL;
if (queryfont_xftchar(&qf,script->x11info,(char *)text)) return NULL;
if (!(ret=PyTuple_New(4))) GOTOERROR;
if (!(pyo=PyLong_FromLong(qf.ismatch))) GOTOERROR;
if (PyTuple_SetItem(ret,0,pyo)) GOTOERROR; // steals ref
if (!(pyo=PyLong_FromLong(qf.ascent))) GOTOERROR;
if (PyTuple_SetItem(ret,1,pyo)) GOTOERROR;
if (!(pyo=PyLong_FromLong(qf.descent))) GOTOERROR;
if (PyTuple_SetItem(ret,2,pyo)) GOTOERROR;
if (!(pyo=PyLong_FromLong(qf.width))) GOTOERROR;
if (PyTuple_SetItem(ret,3,pyo)) GOTOERROR;
return ret;
error:
	Py_XDECREF(ret);
	return NULL;
}

static PyObject *config_issynched(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script **v,*script;
PyObject *yes,*no;

yes=PyLong_FromLong(1); // static
no=PyLong_FromLong(0); // static

v=(struct _script **)PyModule_GetState(self);
//	fprintf(stderr,"config_issynched v=%p argc=%d\n",v,argc);
if (!v) return no;
if (!(script=*v)) return no;
if (!script->xclient) return no;

// if there are outstanding draw requests, they may point to locations that won't exist after a config.apply
// so we clear them out before allowing an apply
// the script can do a callback with vte.setalarm(0) to ensure that issynched returns yes
// update: I think this is obsolete now since the script is always called from a state where this is true
if (iseventsempty_xclient(script->xclient)) return yes;
return no;
}

static PyMethodDef ConfigMethods[]={
	{"apply",(PyCFunction)config_apply,METH_FASTCALL,"Set the current configuration."},
	{"queryfont",(PyCFunction)config_queryfont,METH_FASTCALL,"Read font dimensions."},
	{"issynched",(PyCFunction)config_issynched,METH_FASTCALL,"Check if pending commands would block config changes."},
	{NULL,NULL,0,NULL}
};

static PyModuleDef ConfigModule={
	PyModuleDef_HEAD_INIT,"config",NULL,sizeof(void*),ConfigMethods, NULL, NULL, NULL, NULL
};

// by not registering tap_module with ModuleDict, hopefully code will need to be passed the pointer for it to
// snoop on incoming text. The pointer will be passed to user.OnInitEnd and user can pass it along as approval.
static PyMethodDef TapMethods[]={
	{"addlook",(PyCFunction)tap_addlook,METH_FASTCALL,"Set a callback for specified input."},
	{"rmlook",(PyCFunction)tap_rmlook,METH_FASTCALL,"Remove callback(s) from addlook."},
	{NULL,NULL,0,NULL}
};

static PyModuleDef TapModule={
	PyModuleDef_HEAD_INIT,"tap",NULL,sizeof(void*),TapMethods, NULL, NULL, NULL, NULL
};

static int anon_addmodule(PyObject **mod_out, PyModuleDef *moduledef, void *baggage) {
PyObject *mod=NULL;
if (!(mod=PyModule_Create(moduledef))) GOTOERROR;
if (baggage) {
	void **v;
	if (!(v=PyModule_GetState(mod))) GOTOERROR;
	*v=baggage;
}
*mod_out=mod;
return 0;
error:
	Py_XDECREF(mod);
	return -1;
}
static int addmodule(PyObject **mod_out, char *name, PyModuleDef *moduledef, void *baggage) {
PyObject *mod=NULL,*sysmods;
if ((!PyImport_AddModule(name)) && PyErr_Occurred() ) GOTOERROR;
if (!(mod=PyModule_Create(moduledef))) GOTOERROR;
if (baggage) {
	void **v;
	if (!(v=PyModule_GetState(mod))) GOTOERROR;
	*v=baggage;
}
if (!(sysmods=PyImport_GetModuleDict())) GOTOERROR;
if (PyDict_SetItemString(sysmods,name,mod)) GOTOERROR;
*mod_out=mod;
return 0;
error:
	Py_XDECREF(mod);
	return -1;
}

static int callfunctionwithargs(struct _script *script, char *name, int argc, char **argv) {
PyObject *pFunc, *args=NULL;
int i;

if (!PyObject_HasAttrString(script->pModule,name)) return 0;
pFunc=PyObject_GetAttrString(script->pModule,name);
if (!pFunc) GOTOERROR;
if (!PyCallable_Check(pFunc)) GOTOERROR;
if (!(args=PyTuple_New(argc))) GOTOERROR;
for (i=0;i<argc;i++) {
	PyObject *v;
	if (!(v=PyUnicode_DecodeFSDefault(argv[i]))) GOTOERROR;
	if (PyTuple_SetItem(args,i,v)) GOTOERROR; // steals ref
}
{
	PyObject *pValue;
	pValue=PyObject_CallObject(pFunc,args);
	if ((!pValue) && PyErr_Occurred()) {
		iffputs("c",script->iotrap.fakefout);
		PyErr_Print();
	}
	Py_XDECREF(pValue);
}
Py_DECREF(args);
Py_DECREF(pFunc);
return 0;
error:
	Py_XDECREF(args);
	Py_XDECREF(pFunc);
	return -1;
}

static int callfunction(struct _script *script, PyObject *o, char *name, PyObject *args) {
PyObject *pFunc;
PyObject *pValue;

if (!PyObject_HasAttrString(o,name)) return 0;
pFunc=PyObject_GetAttrString(o,name);
if (!pFunc) GOTOERROR;
if (!PyCallable_Check(pFunc)) GOTOERROR;
pValue=PyObject_CallObject(pFunc,args);
if ((!pValue) && PyErr_Occurred()) {
	iffputs("c",script->iotrap.fakefout);
	PyErr_Print();
}
Py_XDECREF(pValue);
if (mark_xclient(script->xclient)) GOTOERROR;
Py_DECREF(pFunc);
return 0;
error:
	Py_XDECREF(pFunc);
	return -1;
}

#define callfunction1arg(a,b,c,d) callfunction2arg(a,b,c,d,NULL)
static int callfunction2arg(struct _script *script, PyObject *o, char *name, PyObject *v1, PyObject *v2) {
// steals v refs
PyObject *args=NULL;
int numargs=0;

#if 0
fprintf(stderr,"%s:%d calling %s\n",__FILE__,__LINE__,name);
#endif

if (v1) {
	numargs=(v2)?2:1;
	if (!(args=PyTuple_New(numargs))) GOTOERROR;
	switch (numargs) {
		case 2: if (PyTuple_SetItem(args,1,v2)) GOTOERROR; v2=NULL; // steals ref
		case 1: if (PyTuple_SetItem(args,0,v1)) GOTOERROR; v1=NULL;
	}
}

if (callfunction(script,o,name,args)) GOTOERROR;
Py_XDECREF(args);
return 0;
error:
	Py_XDECREF(args);
	Py_XDECREF(v1);
	Py_XDECREF(v2);
	return -1;
}

static int callfunction5arg(struct _script *script, PyObject *o, char *name, unsigned int numargs,
		PyObject *v1, PyObject *v2, PyObject *v3, PyObject *v4, PyObject *v5) {
// steals v refs
PyObject *args=NULL;

#if 0
fprintf(stderr,"%s:%d calling %s\n",__FILE__,__LINE__,name);
#endif

if (!(args=PyTuple_New(numargs))) GOTOERROR;
switch (numargs) {
	case 5: if (PyTuple_SetItem(args,4,v5)) GOTOERROR; v5=NULL; // steals ref
	case 4: if (PyTuple_SetItem(args,3,v4)) GOTOERROR; v4=NULL;
	case 3: if (PyTuple_SetItem(args,2,v3)) GOTOERROR; v3=NULL;
	case 2: if (PyTuple_SetItem(args,1,v2)) GOTOERROR; v2=NULL;
	case 1: if (PyTuple_SetItem(args,0,v1)) GOTOERROR; v1=NULL;
}

if (callfunction(script,o,name,args)) GOTOERROR;
Py_XDECREF(args);
return 0;
error:
	Py_XDECREF(args);
	Py_XDECREF(v1);
	Py_XDECREF(v2);
	Py_XDECREF(v3);
	Py_XDECREF(v4);
	Py_XDECREF(v5);
	return -1;
}
static int str_callfunction(struct _script *script, PyObject *o, char *name, char *arg, unsigned int arglen) {
PyObject *v;
if (!(v=PyUnicode_DecodeFSDefaultAndSize(arg,arglen))) GOTOERROR;
return callfunction1arg(script,o,name,v); // steals ref
error:
	return -1;
}
static int int_callfunction(struct _script *script, PyObject *o, char *name, int arg) {
PyObject *v;
if (!(v=PyLong_FromLong(arg))) GOTOERROR;
return callfunction1arg(script,o,name,v); // steals ref
error:
	return -1;
}
static int uint_callfunction(struct _script *script, PyObject *o, char *name, unsigned int arg) {
PyObject *v;
if (!(v=PyLong_FromUnsignedLong(arg))) GOTOERROR;
return callfunction1arg(script,o,name,v); // steals ref
error:
	return -1;
}
static int uint2_callfunction(struct _script *script, PyObject *o, char *name, unsigned int ui1, unsigned int ui2) {
PyObject *v1,*v2;
if (!(v1=PyLong_FromUnsignedLong(ui1))) GOTOERROR;
if (!(v2=PyLong_FromUnsignedLong(ui2))) GOTOERROR;
return callfunction2arg(script,o,name,v1,v2); // steals ref
error:
	Py_XDECREF(v1);
	return -1;
}
static int uint5_callfunction(struct _script *script, PyObject *o, char *name, unsigned int numargs,
		unsigned int ui1, unsigned int ui2, unsigned int ui3, unsigned int ui4, unsigned int ui5) {
PyObject *v1=NULL,*v2=NULL,*v3=NULL,*v4=NULL,*v5=NULL;
switch (numargs) {
case 5: if (!(v5=PyLong_FromUnsignedLong(ui5))) GOTOERROR;
case 4: if (!(v4=PyLong_FromUnsignedLong(ui4))) GOTOERROR;
case 3: if (!(v3=PyLong_FromUnsignedLong(ui3))) GOTOERROR;
case 2: if (!(v2=PyLong_FromUnsignedLong(ui2))) GOTOERROR;
case 1: if (!(v1=PyLong_FromUnsignedLong(ui1))) GOTOERROR;
}
return callfunction5arg(script,o,name,numargs,v1,v2,v3,v4,v5); // steals ref
error:
	Py_XDECREF(v1);
	Py_XDECREF(v2);
	Py_XDECREF(v3);
	Py_XDECREF(v4);
	Py_XDECREF(v5);
	return -1;
}

static void removeonetap(struct _script *s, PyObject *wr) {
// this is called from weakrefcallback
struct onetap *ot,**ppn;
ppn=&s->taps.first;
ot=s->taps.first;
while (ot) {
	if (ot->weakref==wr) {
		int isfound;

fprintf(stderr,"%s:%d removing tap (%p) %p:%u\n",__FILE__,__LINE__,ot,ot->receiver_ro,ot->code);
		(void)handle_remove_texttap(&isfound,s->texttap,&ot->handle);
#ifdef DEBUG
		if (!isfound) WHEREAMI;
#endif

		*ppn=ot->next;
		ot->next=s->taps.firstfree;
		s->taps.firstfree=ot;
		break;
	}
	ppn=&ot->next;
	ot=ot->next;
}
}

static PyObject *taps_weakrefcallback(PyObject *self, PyObject *const *argv, Py_ssize_t argc) {
struct _script *s;
if (!PyCapsule_CheckExact(self)) GOTOERROR;
s=(struct _script*)PyCapsule_GetPointer(self,NULL);
// fprintf(stderr,"taps_weakrefcallback script=%p argc=%d\n",s,argc);
// printargs(argc,argv);
if (argc>0) {
	PyObject *wr;
	wr=argv[0];
	PyObject *pyo;
	pyo=PyWeakref_GetObject(wr); // borrowed ref
	if (pyo!=Py_None) {
		fprintf(stderr,"%s:%d ref not dead\n",__FILE__,__LINE__);
	} else {
		(void)removeonetap(s,wr);
		Py_DECREF(wr);
	}
}
return PyLong_FromLong(0);
error:
	return NULL;
}

static PyMethodDef tapscheckdef={"tapswrcb",(PyCFunction)taps_weakrefcallback,METH_FASTCALL,"Callback for taps weakrefs."};

static int init_script(struct script *script_in, struct x11info *xi, struct config *config, struct texttap *texttap, char *pyname,
		unsigned int isnobytecode, int argc, char **argv, FILE *realstderr) {
// pyname="" => use the default script
// !pyname => disable
// otherwise, assume it's a custom script
struct _script *script;

if (!pyname) return 0;

script=(struct _script*)script_in;

script->x11info=xi;
script->config=config;
script->texttap=texttap;
script->fgvaluemask=(uint32_t)15<<25;

if (pyname[0]) { 
	char *pwd;
	pwd=getenv("PWD");
	if (!pwd) GOTOERROR;
	if (setenv("PYTHONPATH",pwd,0)) GOTOERROR;
} else {
	char *home;
	char configdir[PATH_MAX];
	int r;
	home=getenv("HOME");
	if (home) {
		r=snprintf(configdir,PATH_MAX,"%s/.config/xapterm",home);
		if (access(configdir,F_OK)) home=NULL;
	} 
	if (!home) {
		r=snprintf(configdir,PATH_MAX,"/usr/share/xapterm");
	}
	if ((r<0)||(r>=PATH_MAX)) GOTOERROR;
	pyname="user";
	if (setenv("PYTHONPATH",configdir,0)) GOTOERROR;
}

if (isnobytecode) {
//	if (setenv("PYTHONDONTWRITEBYTECODE","",1)) GOTOERROR;
	Py_DontWriteBytecodeFlag=1;
}

(void)Py_Initialize();
stderr=realstderr;

if (addmodule(&script->config_module,"config",&ConfigModule,script)) GOTOERROR;
if (addmodule(&script->vte_module,"vte",&VteModule,script)) GOTOERROR;
if (anon_addmodule(&script->tap_module,&TapModule,script)) GOTOERROR;
{
	PyObject *cap;
	if (!(cap=PyCapsule_New((void *)script,NULL,NULL))) GOTOERROR;
	if (!(script->tapscheck=PyCFunction_New(&tapscheckdef,cap))) { Py_DECREF(cap); GOTOERROR; }
	Py_DECREF(cap);
}
{
	PyObject *pName;
// fprintf(stderr,"%s:%d pyname:%s\n",__FILE__,__LINE__,pyname);
	pName=PyUnicode_DecodeFSDefault(pyname);
	if (!pName) GOTOERROR;

	script->pModule=PyImport_Import(pName);
	Py_DECREF(pName);
}
if (!script->pModule) GOTOERROR;
if (storeconfig(script,config)) GOTOERROR;
if (callfunctionwithargs(script,"OnInitBegin",argc,argv)) GOTOERROR;
if (restoreconfig(script,config,1)) GOTOERROR;
(void)recalc_config(config);
return 0;
error:
	return -1;
}

void addxclient_script(struct script *script_in, struct xclient *xclient) {
struct _script *script=(struct _script*)script_in;
script->xclient=xclient;
}

int shutdown_script(struct script *script) {
if (0>Py_FinalizeEx()) GOTOERROR;
return 0;
error:
	return -1;
}
static void deinit_taps(struct _script *s) {
struct onetap *t;
t=s->taps.first;
while (t) {
	int isfound;
	(void)handle_remove_texttap(&isfound,s->texttap,&t->handle);
#ifdef DEBUG
	if (!isfound) WHEREAMI;
#endif
	t=t->next;
}
}

static void deinit_script(struct _script *script) {
if (PyErr_Occurred()) PyErr_Print();
if (script->iotrap.fakefout) {
	unsigned int ui;
	char *str;
	str=getinsertion_script(&ui,script);
	if (str) {
		fwrite(str,ui,1,stderr);
	}
	fclose(script->iotrap.fakefout);
	close(script->iotrap.mfd);
}
if (script->tapscheck) {
	(void)PyObject_Del(script->tapscheck);
}
iffree(script->clipboard.buffer);
deinit_taps(script);
deinit_blockmem(&script->blockmem);
}

static int str_callfunction2(struct _script *script, char *fn, char *arg, unsigned int arglen) {
if (!script->pModule) return 0;
return str_callfunction(script,script->pModule,fn,arg,arglen);
}
static int uint5_callfunction2(struct _script *script, unsigned int numargs, char *fn,
		unsigned int v1, unsigned int v2, unsigned int v3, unsigned int v4, unsigned int v5) {
if (!script->pModule) return 0;
return uint5_callfunction(script,script->pModule,fn,numargs,v1,v2,v3,v4,v5);
}
static int uint2_callfunction2(struct _script *script, char *fn, unsigned int v1, unsigned int v2) {
if (!script->pModule) return 0;
return uint2_callfunction(script,script->pModule,fn,v1,v2);
}
static int int_callfunction2(struct _script *script, char *fn, int arg) {
if (!script->pModule) return 0;
return int_callfunction(script,script->pModule,fn,arg);
}
static int callfunction2(struct _script *script, char *fn) {
if (!script->pModule) return 0;
return callfunction1arg(script,script->pModule,fn,NULL);
}

int oninitend_script(struct script *script_in) {
struct _script *script=(struct _script*)script_in;
return callfunction1arg(script,script->pModule,"OnInitEnd",script->tap_module); // steals ref
}

int onsuspend_script(void *script_in, int ign) {
return callfunction2((struct _script*)script_in,"OnSuspend");
}

int onresume_script(void *script_in, int ign) {
return callfunction2((struct _script*)script_in,"OnResume");
}

int onbell_script(void *script_in) {
return callfunction2((struct _script*)script_in,"OnBell");
}
int onalarm_script(void *script_in, int t32) {
return int_callfunction2((struct _script*)script_in,"OnAlarm",(unsigned int)t32);
}

SICLEARFUNC(_script);
struct script *new_script(struct x11info *xi, struct config *config, struct texttap *texttap, char *pyname, unsigned int isnobytecode,
		int isstderr, int argc, char **argv) {
struct _script *s;
FILE *stdout_backup,*stderr_backup,*fakefout;
const unsigned int bufferlen=BUFFSIZE_INSERTION_XCLIENT; // 512

if (!(s=malloc(sizeof(struct _script)))) GOTOERROR;
clear__script(s);
if (init_blockmem(&s->blockmem,4096)) GOTOERROR;

s->stdio.stdout=stdout;
s->stdio.stderr=stderr;
stdout_backup=stdout;
stderr_backup=stderr;
if (isstderr) { // don't trap stderr from python, output will go to terminal's stderr which may be nowhere
	s->iotrap.mfd=-1;
} else {
	int ptym,ptys;
	struct termios termios;
	static struct winsize winsize={.ws_row=25,.ws_col=80};

	if (tcgetattr(STDOUT_FILENO,&termios)) GOTOERROR;
	if (openpty(&ptym,&ptys,NULL,&termios,&winsize)) GOTOERROR;
	if (!(s->iotrap.fakefout=fdopen(ptys,"a"))) {
		close(ptys);
		close(ptym);
		GOTOERROR;
	}
	s->iotrap.bufferlenm1=bufferlen-1;
	if (!(s->iotrap.buffer=alloc_blockmem(&s->blockmem,bufferlen))) GOTOERROR;
	s->iotrap.mfd=ptym;
	fakefout=s->iotrap.fakefout;
	setbuf(fakefout,NULL);
	stdout=stderr=fakefout;
}

if (init_script((struct script*)s,xi,config,texttap,pyname,isnobytecode,argc,argv,stderr_backup)) {
	stdout=stdout_backup;
	stderr=stderr_backup;
	GOTOERROR;
}
stdout=stdout_backup;
stderr=stderr_backup;
return (struct script*)s;
error:
	if (s) { deinit_script(s); free(s); }
	return NULL;
}

void free_script(struct script *script_in) {
struct _script *s=(struct _script*)script_in;
deinit_script(s);
free(s);
}

int oncontrolkey_script(void *script_in, int key) {
return int_callfunction2((struct _script*)script_in,"OnControlKey",key);
}

int onkey_script(void *script_in, int key) {
return int_callfunction2((struct _script*)script_in,"OnKey",key);
}

int onpointer_script(void *script_in, unsigned int type, unsigned int mods, unsigned int button, unsigned int row, unsigned int col) {
return uint5_callfunction2((struct _script*)script_in,5,"OnPointer",type,mods,button,row,col);
}

int checkinsertion_script(void *script_in) {
struct _script *s=(struct _script*)script_in;
fd_set rset;
struct timeval tv;
int fd;
fd=s->iotrap.mfd;
if (fd<0) return 0;
tv.tv_sec=0;
tv.tv_usec=0;
FD_ZERO(&rset);
FD_SET(fd,&rset);
if (1!=select(fd+1,&rset,NULL,NULL,&tv)) return 0;
return 1;
}

char *getinsertion_script(unsigned int *len_out, void *script_in) {
struct _script *s=(struct _script*)script_in;
int fd,k;
fd=s->iotrap.mfd;
if (!checkinsertion_script(script_in)) return NULL;
k=read(fd,s->iotrap.buffer,s->iotrap.bufferlenm1);
if (k<=0) return NULL;
// fprintf(stderr,"%s:%d Fetched %d bytes from script\n",__FILE__,__LINE__,k);
*len_out=(unsigned int)k;
s->iotrap.buffer[k]='\0';
return s->iotrap.buffer;
}

int onmessage_script(void *script_in, char *str, unsigned int len) {
return str_callfunction2((struct _script*)script_in,"OnMessage",str,len);
}

int onkeysym_script(void *script_in, unsigned int keysym, unsigned int modifiers) {
return uint2_callfunction2((struct _script*)script_in,"OnKeySym",keysym,modifiers);
}
int onkeysymrelease_script(void *script_in, unsigned int keysym, unsigned int modifiers) {
return uint2_callfunction2((struct _script*)script_in,"OnKeySymRelease",keysym,modifiers);
}

int onresize_script(void *script_in, unsigned int width, unsigned int height) {
struct _script *s=(struct _script*)script_in;
PyObject *dest;

s->config->xwidth=width;
s->config->xheight=height;

dest=s->config_module;
if (setuintdouble(dest,"windims",width,height)) GOTOERROR;
return callfunction2(s,"OnResize");
error:
	return -1;
}

int texttapcb_script(void *v, struct value_texttap *value) {
struct onetap *ot=(struct onetap *)v;
int r;
PyObject *receiver;
receiver=PyWeakref_GetObject(ot->weakref);
if (receiver==Py_None) return 0; // this is fine, the object was deleted but we haven't gotten the memo yet
Py_INCREF(receiver); // may not be necessary
fprintf(stderr,"%s:%d got cb\n",__FILE__,__LINE__);
r= uint_callfunction(ot->script,receiver,"OnLook",ot->code);
Py_DECREF(receiver);
return r;
}
