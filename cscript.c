/*
 * cscript.c - alternative to user.py, written in C
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
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#define DEBUG
#include "common/conventions.h"
#include "config.h"
#include "x11info.h"
#include "vte.h"
#include "cursor.h"
#include "xclient.h"

#include "cscript.h"

// TODO make an opaque struct to typecheck these void* calls

struct cscript {
	struct config *config;
	struct xclient *xclient;
};

static int init_cscript(struct cscript *cs, struct config *config) {
cs->config=config;
return 0;
}

static void deinit_cscript(struct cscript *cs) {
}

void free_cscript(void *v) {
free(v);
}

void *new_cscript(struct config *config) {
struct cscript *cs;
if (!(cs=malloc(sizeof(struct cscript)))) GOTOERROR;
if (init_cscript(cs,config)) GOTOERROR;
return cs;
error:
	if (cs) {
		deinit_cscript(cs);
		free_cscript(cs);
	}
	return NULL;
}

int onsuspend_cscript(void *v, int ign) {
struct cscript *cs=(struct cscript*)v;

if (cs->xclient->ispaused) {
	if (restorebacking_xclient(cs->xclient)) GOTOERROR;
	if (unpause_xclient(cs->xclient)) GOTOERROR;
} else {
	char *text;
	uint32_t fgvaluemask,bgvaluemask,underlinemask,barevalue;
	unsigned int col,textlen;
	int i;
	if (pause_xclient(cs->xclient)) GOTOERROR;
	(void)savebacking_xclient(cs->xclient);
	text="VTE Suspended, Press ^q or ^s to Resume";
	textlen=strlen(text);
	fgvaluemask=(0)<<25;
	bgvaluemask=(15)<<21;
	underlinemask=UNDERLINEBIT_VALUE;
	if (cs->config->columns<textlen) col=0;
	else col=cs->config->columns-textlen;
	barevalue=fgvaluemask|bgvaluemask|underlinemask;
	for (i=0;i<textlen;i++) {
		uint32_t value;
		value=(unsigned int)text[i]|barevalue;
		if (addchar_xclient(cs->xclient,value,0,col+i)) GOTOERROR;
	}
	XFlush(cs->xclient->baggage.x->display);
}

return 0;
error:
	return -1;
}

int onresume_cscript(void *v, int ign) {
struct cscript *cs=(struct cscript*)v;
if (cs->xclient->ispaused) {
	if (restorebacking_xclient(cs->xclient)) GOTOERROR;
	if (unpause_xclient(cs->xclient)) GOTOERROR;
}
return 0;
error:
	return -1;
}

static int sendletters(struct cscript *cs, unsigned char *letters, unsigned int len) {
int isdrop;
return send_xclient(&isdrop,cs->xclient,letters,len);
}

int onkeysym_cscript(void *v, unsigned int keysym, unsigned int modifiers) {
struct cscript *cs=(struct cscript*)v;
if (!modifiers) {
	switch (keysym) {
		case 0xff51: if (sendletters(cs,(unsigned char *)"[D",3)) GOTOERROR; break; // Left
		case 0xff52: if (sendletters(cs,(unsigned char *)"[A",3)) GOTOERROR; break; // Up
		case 0xff53: if (sendletters(cs,(unsigned char *)"[C",3)) GOTOERROR; break; // Right
		case 0xff54: if (sendletters(cs,(unsigned char *)"[B",3)) GOTOERROR; break; // Down
		case 0xff50: if (sendletters(cs,(unsigned char *)"[H",3)) GOTOERROR; break; // Home
		case 0xff57: if (sendletters(cs,(unsigned char *)"[F",3)) GOTOERROR; break; // End
	}
}
return 0;
error:
	return -1;
}
void addxclient_cscript(void *v, struct xclient *xclient) {
struct cscript *cs=(struct cscript*)v;
cs->xclient=xclient;
}
int oninitend_cscript(void *v) {
// we have xclient and everything is loaded
return 0;
}

int onmessage_cscript(void *v, char *str, unsigned int len) {
struct cscript *cs=(struct cscript*)v;
struct x11info *x=cs->xclient->baggage.x;
switch (str[0]) {
	case '_': XStoreName(x->display,x->window,str+1); break;
	case ']':
		if (!strncmp(str,"]0;",3)) XStoreName(x->display,x->window,str+3);
#if 0
// TODO add palette changes, we can mimic script.config.apply
		if ((str[1]=='P')&&(len==9)) { } // set palette
		else if (str[1]=='R') { } // reset palette
#endif
		break;
}
return 0;
}
