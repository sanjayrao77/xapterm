/*
 * surface.c - a text drawing surface
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
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>
#define DEBUG
#include "config.h" 
#include "common/conventions.h"
#include "common/safemem.h"
#include "x11info.h"
#include "xftchar.h"
#include "charcache.h"
#include "event.h"
#include "vte.h"
#include "cursor.h"
#include "keysym.h"
#include "xclient.h"

#include "surface.h"

static inline void memset4(unsigned int *dest, unsigned int v, unsigned int count) {
unsigned int *lastdest;
lastdest=dest+count;
while (1) {
	*dest=v;
	dest++;
	if (dest==lastdest) break;
}
}

static inline int init_scrollback(struct surface_xclient *s, unsigned int sbcount, unsigned int columns, uint32_t *backing) {
struct sbline_xclient *sbline;

if (!sbcount) return 0;
if (!(s->tofree.sblines=MALLOC(sbcount*sizeof(struct sbline_xclient)))) GOTOERROR;

sbline=s->tofree.sblines;
while (1) {
	sbline->backing=backing;
	sbline->next=s->scrollback.firstfree;
	s->scrollback.firstfree=sbline;
	backing+=columns;
	sbline++;
	sbcount--;
	if (!sbcount) break;
}

return 0;
error:
	return -1;
}

int init_surface_xclient(struct surface_xclient *s, unsigned int rows, unsigned int columns, uint32_t bvalue, unsigned int sbcount) {
unsigned int ui,backcount;
uint32_t *backing;

backcount=columns*(1+2*rows+sbcount);
if (!(backing=s->tofree.backing=MALLOC(backcount*sizeof(uint32_t)))) GOTOERROR;
s->tofree.backcount=backcount;
s->numinline=columns;

s->maxlines=rows;
if (!(s->tofree.lines=MALLOC(3*rows*sizeof(struct line_xclient)))) GOTOERROR;
s->lines=s->tofree.lines;
s->sparelines=s->tofree.lines+rows;
s->savedlines=s->sparelines+rows;
for (ui=0;ui<rows;ui++) {
	memset4(backing,bvalue,columns);
	s->lines[ui].backing=backing; backing+=columns;
	s->savedlines[ui].backing=backing; backing+=columns;
}
s->spareline=backing; backing+=columns;

if (init_scrollback(s,sbcount,columns,backing)) GOTOERROR;

return 0;
error:
	return -1;
}
void deinit_surface_xclient(struct surface_xclient *surface) {
IFFREE(surface->tofree.backing);
IFFREE(surface->tofree.lines);
IFFREE(surface->tofree.sblines);
}

/*
	k if columns or rows decreased, need to blackout window on shrunk areas
	k alloc lines and blanking at same time
		k clear new bytes with blankvalue (for new rows, not the same as memory size difference)
		k set backing pointers for new rows
	k memmove all backing lines, if needed
		. not needed if numinline didn't increase
		k set blankvalue on growth of cols
	k reset existing pointers, if needed
		k not needed if realloc didn't change and columns didn't increase
	X if rows decreased, need to set blankvalue on former lines
		. i don't think this is needed
	k add backing pointers to new lines, if any
*/

static inline int blackoutshrinkage(struct x11info *x, unsigned int oldrows, unsigned int oldcolumns, unsigned int newrows,
		unsigned int newcolumns, unsigned int cellw, unsigned int cellh, long fillcolor, int isremap) {
if (isremap) return 0;
if (!XSetForeground(x->display,x->context,fillcolor)) GOTOERROR;
if (newcolumns<oldcolumns) {
//	fprintf(stderr,"%s:%d blacking out %ux%u..%ux%u\n",__FILE__,__LINE__,0,newcolumns,(oldcolumns-newcolumns),_BADMAX(oldrows,newrows));
	if (!XFillRectangle(x->display,x->window,x->context,newcolumns*cellw,0,(oldcolumns-newcolumns)*cellw,
			_BADMAX(oldrows,newrows)*cellh)) GOTOERROR;
}
if (newrows<oldrows) {
//	fprintf(stderr,"%s:%d blacking out %ux%u..%ux%u\n",__FILE__,__LINE__,0,newrows,_BADMAX(oldcolumns,newcolumns),oldrows-newrows);
	if (!XFillRectangle(x->display,x->window,x->context,0,newrows*cellh,_BADMAX(oldcolumns,newcolumns)*cellw,
			(oldrows-newrows)*cellh)) GOTOERROR;
}
return 0;
error:
	return -1;
}

static inline int reallocmemory(struct surface_xclient *s, unsigned int newrows, unsigned int newcolumns, unsigned int sbcount) {
unsigned int oldrows=s->maxlines;
if (newrows>s->maxlines) {
	struct line_xclient *temp;
//	fprintf(stderr,"%s:%d resizing lines from %u to %u\n",__FILE__,__LINE__,s->maxlines,newrows);
	temp=realloc(s->tofree.lines,3*newrows*sizeof(struct line_xclient));
	if (!temp) GOTOERROR;
	s->tofree.lines=temp;
	if (oldrows) {
		memmove(temp+2*newrows,temp+2*oldrows,oldrows*sizeof(struct line_xclient));
		memmove(temp+newrows,temp+oldrows,oldrows*sizeof(struct line_xclient));
	}
	s->lines=temp;
	s->sparelines=temp+newrows;
	s->savedlines=s->sparelines+newrows;
	s->maxlines=newrows;
}
if ((newrows>oldrows)||(newcolumns>s->numinline)) {
	uint32_t *temp;
	unsigned int maxnil,oldnil=s->numinline;
	unsigned int backcount;
	maxnil=_BADMAX(oldnil,newcolumns);
//	fprintf(stderr,"%s:%d resizing backing from %u.%u->%u.%u\n",__FILE__,__LINE__,oldrows,s->numinline,newrows,newcolumns);
	backcount=(1+2*s->maxlines+sbcount)*maxnil;
	temp=realloc(s->tofree.backing,backcount*sizeof(uint32_t));
	if (!temp) GOTOERROR;
	s->tofree.backing=temp;
	s->tofree.backcount=backcount;
	s->numinline=maxnil;
}
return 0;
error:
	return -1;
}

static inline void blanknewlines(struct surface_xclient *s, unsigned int oldrows, unsigned int oldmaxlines, unsigned int newcolumns,
		uint32_t blankvalue) {
unsigned int maxlines,ui,numinline;
uint32_t *newstart;
struct line_xclient *newline,*newsaved;

maxlines=s->maxlines;
if (maxlines<=oldmaxlines) return;

// fprintf(stderr,"%s:%d blanking new maxlines, %u->%u\n",__FILE__,__LINE__,oldmaxlines,maxlines);

ui=maxlines-oldmaxlines;
numinline=s->numinline;
newstart=s->tofree.backing+(1+2*oldrows)*numinline;
newline=s->lines+oldmaxlines;
newsaved=s->savedlines+oldrows;
while (1) {
	memset4(newstart,blankvalue,newcolumns);
	newline->backing=newstart;
	newstart+=numinline;
	memset4(newstart,blankvalue,newcolumns);
	newsaved->backing=newstart;
	ui--;
	if (!ui) break;
	newstart+=numinline;
	newline++;
	newsaved++;
}
}

static inline void shiftoldbacking(struct surface_xclient *s, unsigned int oldmaxlines, unsigned int oldnuminline,
		unsigned int oldcols, unsigned int newcols, unsigned int sbcount, uint32_t blankvalue) {
unsigned int newnuminline;
uint32_t *oldptr,*newptr,*backing;
unsigned int count,tail;

newnuminline=s->numinline;
if (oldnuminline==newnuminline) return;
if (!oldmaxlines) return;

// fprintf(stderr,"%s:%d shifting backing %u->%u\n",__FILE__,__LINE__,oldnuminline,newnuminline);

tail=0;
if (newcols>oldcols) tail=newcols-oldcols;
backing=s->tofree.backing;
count=1+2*oldmaxlines+sbcount;
oldptr=backing+count*oldnuminline;
newptr=backing+count*newnuminline;
while (1) {
	oldptr-=oldnuminline;
	newptr-=newnuminline;
	memmove(newptr,oldptr,oldnuminline*sizeof(uint32_t));
	if (tail) memset4(newptr+oldnuminline,blankvalue,tail);
	count--;
	if (!count) break;
}
}

static inline uint32_t *updatepointersB(uint32_t *b, uint32_t *oldb, unsigned int oldnum, uint32_t *newb, unsigned int newnum) {
return newb+newnum*((b-oldb)/oldnum);
}
#define CHANGE(a) a=updatepointersB(a,oldbacking,oldnuminline,newbacking,newnuminline)
static void updatepointers(struct surface_xclient *s, unsigned int oldnuminline, unsigned int oldmaxlines, uint32_t *oldbacking) {
struct sbline_xclient *sb;
unsigned int ui;
unsigned int newnuminline;
uint32_t *newbacking;
uint32_t **blist;

if (!oldnuminline) return;
newbacking=s->tofree.backing;
newnuminline=s->numinline;
if ((newnuminline==oldnuminline) && (oldbacking==newbacking)) return;

// fprintf(stderr,"%s:%d updating pointers %u -> %u\n",__FILE__,__LINE__,oldnuminline,newnuminline);

CHANGE(s->spareline);
for (ui=0;ui<oldmaxlines;ui++) {
	CHANGE(s->lines[ui].backing);
	CHANGE(s->savedlines[ui].backing);
}
sb=s->scrollback.first;
while (sb) {
	CHANGE(sb->backing);
	sb=sb->next;
}
sb=s->scrollback.firstfree;
while (sb) {
	CHANGE(sb->backing);
	sb=sb->next;
}
blist=s->selection.backings.list;
ui=s->selection.backings.len;
while (ui) {
	CHANGE(*blist);
	blist++;
	ui--;
}
}
#undef CHANGE
int resize_surface_xclient(struct surface_xclient *s, struct x11info *x, unsigned int oldrows, unsigned int oldcolumns,
		unsigned int newrows, unsigned int newcolumns, uint32_t blankvalue, unsigned int sbcount, unsigned int cellw, unsigned int cellh,
		long fillcolor, int isremap) {
// caller should set xc.config values afterward
uint32_t *oldbacking;
unsigned int oldnuminline,oldmaxlines;
oldbacking=s->tofree.backing;
oldnuminline=s->numinline;
oldmaxlines=s->maxlines;
if (blackoutshrinkage(x,oldrows,oldcolumns,newrows,newcolumns,cellw,cellh,fillcolor,isremap)) GOTOERROR;
if (reallocmemory(s,newrows,newcolumns,sbcount)) GOTOERROR;
(void)blanknewlines(s,oldrows,oldmaxlines,newcolumns,blankvalue);
(void)shiftoldbacking(s,oldmaxlines,oldnuminline,oldcolumns,newcolumns,sbcount,blankvalue);
(void)updatepointers(s,oldnuminline,oldmaxlines,oldbacking);
// (void)blankoldlines(s,oldrows,newrows,oldcolumns,blankvalue); // is there any need for this?
return 0;
error:
	return -1;
}
