/*
 * event.c - describe draw and vte events
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
#define DEBUG
#include "common/conventions.h"
#include "common/safemem.h"

#include "event.h"

static inline void setupevents(struct all_event *all, unsigned int count, struct one_event *events) {
all->unused.count=count;
while (count) {
	events->next=all->unused.first;
	all->unused.first=events;
	events+=1;
	count--;
}
}

int init_all_event(struct all_event *all, int num) {
if (!(all->events=MALLOC(num*sizeof(struct one_event)))) GOTOERROR;
(void)setupevents(all,num,all->events);
return 0;
error:
	return -1;
}

void deinit_all_event(struct all_event *all) {
IFFREE(all->events);
}

static inline struct one_event *getevent(struct all_event *all) {
struct one_event *e;
e=all->unused.first;
if (!e) return NULL;
all->unused.first=e->next;
all->unused.count-=1;
return e;
}
static inline void addevent(struct all_event *all, struct one_event *e) {
e->next=NULL;
if (!all->first) {
	all->first=all->last=e;
	return;
}
all->last->next=e;
all->last=e;
}

void recycle_event(struct all_event *all, struct one_event *e) {
all->unused.count+=1;
e->next=all->unused.first;
all->unused.first=e;
}

void addchar_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int col) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) {
//	fprintf(stderr,"%s:%d %s %x %u.%u\n",__FILE__,__LINE__,__FUNCTION__,value,row,col);
	WHEREAMI;
	return;
}
#endif
e->type=ADDCHAR_TYPE_EVENT;
e->addchar.value=value;
e->addchar.row=row;
e->addchar.col=col;
(void)addevent(all,e);
}

void eraseinline_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int rowcount,
		unsigned int col, unsigned int colcount) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=ERASEINLINE_TYPE_EVENT;
e->eraseinline.value=value;
e->eraseinline.row=row;
e->eraseinline.col=col;
e->eraseinline.colcount=colcount;
e->eraseinline.rowcount=rowcount;
(void)addevent(all,e);
}

static int isrewrite_insertline(struct one_event *e, unsigned int value, unsigned int row, unsigned int bottom) {
// there are some common sequences that can be merged to remove redundant X drawing
#if 0
#warning ansi speedups disabled
return 0;
#endif
if (!e) return 0;
if (e->type==SCROLL1UP_TYPE_EVENT) {
//	if (e->scroll1up.erasevalue!=value) return 0;
	if ( (e->scroll1up.toprow < row) && (row < e->scroll1up.bottomrow) ) {
// delete at 10: scroll1up,10,25 ; insert at 23 => scroll1up,10,23, can happen from vim's ^E
		e->scroll1up.bottomrow=row;
		e->scroll1up.erasevalue=value;
		return 1;
	}
// delete at 23: scroll1up,23,25 ; insert at 10 => scroll1up,10,23, can happen from vim's ^Y
	if ( (row < e->scroll1up.toprow) && (bottom == e->scroll1up.bottomrow)) {
		e->type=SCROLL1DOWN_TYPE_EVENT; // e.scroll1up and e.scroll1down are identical
		e->scroll1down.bottomrow=e->scroll1down.toprow;;
		e->scroll1down.toprow=row;
		e->scroll1down.erasevalue=value;
		return 1;
	}
	return 0;
}
return 0;
}

void insertline_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int bottom, unsigned int count) {
struct one_event *e,*l;

if (count==1) {
	l=all->last;
	if (isrewrite_insertline(l,value,row,bottom)) return;
	e=getevent(all);
#ifdef DEBUG
	if (!e) { WHEREAMI; return; }
#endif
	e->type=SCROLL1DOWN_TYPE_EVENT;
	e->scroll1down.toprow=row;
	e->scroll1down.bottomrow=bottom;
	e->scroll1down.erasevalue=value;
} else {
	e=getevent(all);
#ifdef DEBUG
	if (!e) { WHEREAMI; return; }
#endif
	e->type=SCROLLDOWN_TYPE_EVENT;
	e->scrolldown.toprow=row;
	e->scrolldown.bottomrow=bottom;
	e->scrolldown.erasevalue=value;
	e->scrolldown.count=count;
}
(void)addevent(all,e);
}

void deleteline_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int bottom, unsigned int count) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
if (count==1) {
	e->type=SCROLL1UP_TYPE_EVENT;
	e->scroll1up.toprow=row;
	e->scroll1up.bottomrow=bottom;
	e->scroll1up.erasevalue=value;
} else {
	e->type=SCROLLUP_TYPE_EVENT;
	e->scrollup.toprow=row;
	e->scrollup.bottomrow=bottom;
	e->scrollup.erasevalue=value;
	e->scrollup.count=count;
}
(void)addevent(all,e);
}

void scroll1up_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=SCROLL1UP_TYPE_EVENT;
e->scroll1up.toprow=toprow;
e->scroll1up.bottomrow=bottomrow;
e->scroll1up.erasevalue=value;
(void)addevent(all,e);
}
void scrollup_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow, unsigned int count) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=SCROLLUP_TYPE_EVENT;
e->scrollup.toprow=toprow;
e->scrollup.bottomrow=bottomrow;
e->scrollup.erasevalue=value;
e->scrollup.count=count;
(void)addevent(all,e);
}

void scroll1down_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=SCROLL1DOWN_TYPE_EVENT;
e->scroll1down.toprow=toprow;
e->scroll1down.bottomrow=bottomrow;
e->scroll1down.erasevalue=value;
(void)addevent(all,e);
}
void scrolldown_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow, unsigned int count) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=SCROLLDOWN_TYPE_EVENT;
e->scrolldown.toprow=toprow;
e->scrolldown.bottomrow=bottomrow;
e->scrolldown.erasevalue=value;
e->scrolldown.count=count;
(void)addevent(all,e);
}

void generic_event(struct all_event *all, char *str) {
// dest has 16 bytes
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=GENERIC_TYPE_EVENT;
// TODO do ESC[0n reply if !type
strcpy(e->generic.str,str);
(void)addevent(all,e);
}

void ich_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int col, unsigned int count) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=ICH_TYPE_EVENT;
e->ich.erasevalue=value;
e->ich.row=row;
e->ich.col=col;
e->ich.count=count;
(void)addevent(all,e);
}
void dch_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int col, unsigned int count) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=DCH_TYPE_EVENT;
e->dch.erasevalue=value;
e->dch.row=row;
e->dch.col=col;
e->dch.count=count;
(void)addevent(all,e);
}

void title_event(struct all_event *all, char *name) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=TITLE_TYPE_EVENT;
e->title.name=name;
(void)addevent(all,e);
}

void setcursor_event(struct all_event *all, unsigned int row, unsigned int col) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=SETCURSOR_TYPE_EVENT;
e->setcursor.row=row;
e->setcursor.col=col;
(void)addevent(all,e);
}

void bell_event(struct all_event *all) {
struct one_event *e;
e=all->last;
if (e && e->type==BELL_TYPE_EVENT) return;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=BELL_TYPE_EVENT;
(void)addevent(all,e);
}

void message_event(struct all_event *all, char *data, unsigned int len) {
// all messages should be 0-termed (data[len]=0)
// note that data will _not_ be freed
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=MESSAGE_TYPE_EVENT;
e->message.data=data;
e->message.len=len;
(void)addevent(all,e);
}

void smessage2_event(struct all_event *all, unsigned char *data, unsigned int len) {
// str is limited to 16 (with 0)
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=SMESSAGE_TYPE_EVENT;
memcpy(e->smessage.str,data,len);
e->smessage.str[len]='\0';
(void)addevent(all,e);
}
void smessage_event(struct all_event *all, char *str) {
// str is limited to 16 (with 0)
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=SMESSAGE_TYPE_EVENT;
strcpy(e->smessage.str,str);
(void)addevent(all,e);
}

void tap_event(struct all_event *all, unsigned int value) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=TAP_TYPE_EVENT;
e->tap.value=value;
(void)addevent(all,e);
}

void reverse_event(struct all_event *all) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=REVERSE_TYPE_EVENT;
(void)addevent(all,e);
}

void appcursor_event(struct all_event *all, unsigned int isset) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=APPCURSOR_TYPE_EVENT;
e->appcursor.isset=isset;
(void)addevent(all,e);
}

void autorepeat_event(struct all_event *all, unsigned int isset) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=AUTOREPEAT_TYPE_EVENT;
e->autorepeat.isset=isset;
(void)addevent(all,e);
}

void reset_event(struct all_event *all) {
struct one_event *e;
e=getevent(all);
#ifdef DEBUG
if (!e) { WHEREAMI; return; }
#endif
e->type=RESET_TYPE_EVENT;
(void)addevent(all,e);
}
