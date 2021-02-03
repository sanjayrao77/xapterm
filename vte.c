/*
 * vte.c - virtual terminal emulator, read escape codes and create events for xclient
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
#define DEBUG
#include "common/conventions.h"
#include "common/safemem.h"
#include "common/blockmem.h"
#include "common/texttap.h"
#include "config.h"
#include "pty.h"
#include "event.h"

#include "vte.h"

// #define DEBUG2

#define SPACEHASFG
// SPACEHASFG
// at first, spaces weren't given a foreground color
// this reduces cache slots as well as speeds up scrolling
// BUT, this makes inverted video wonky for text selection

static unsigned char SUB=26; // substitution char
static unsigned char fffd_utf8[3]={ 0xef, 0xbf, 0xbd }; // invalid char, U+FFFD: 1110 1111 1011 1111 1011 1101
static char vt102_decid[]={27,'[','?','6','c',0};

static inline void setcolor(struct vte *vte, unsigned int index, unsigned char r, unsigned char g, unsigned char b) {
vte->colors[index].index=index;
vte->colors[index].r=r;
vte->colors[index].g=g;
vte->colors[index].b=b;
#if 0
vte->colors[index].rgba[0]=r;
vte->colors[index].rgba[1]=g;
vte->colors[index].rgba[2]=b;
vte->colors[index].rgba[3]=255;
vte->colors[index].bgra[0]=b;
vte->colors[index].bgra[1]=g;
vte->colors[index].bgra[2]=r;
vte->colors[index].bgra[3]=255;
#endif
vte->colors[index].fgvaluemask=(uint32_t)index<<25; // next free bit is 30
vte->colors[index].bgvaluemask=(uint32_t)index<<21; // 21 bits for unicode
}

void setcolors_vte(struct vte *vte, struct colors_config *colors) {
unsigned int ui;
for (ui=0;ui<16;ui++) {
	struct color_config *c;
	c=&colors->colors[ui];
	(void)setcolor(vte,ui,c->r,c->g,c->b);
}
}

static inline void settabstops(struct vte *vte) {
unsigned int *t,*lastt;
t=vte->tabs.tabline+8;
lastt=vte->tabs.tabline+vte->config.columns;
while (t<lastt) {
	*t=1;
	t+=8;
}
}

static inline void growtabstops(unsigned int *tabline,unsigned int oldmax, unsigned int cols) {
memset(tabline+oldmax,0,(cols-oldmax)*sizeof(*tabline));
oldmax--;
oldmax|=7;
oldmax++;
tabline+=oldmax;
while (oldmax<cols) {
	*tabline=1;
	tabline+=8;
	oldmax+=8;
}
}

static inline void tabset_ansi(struct vte *v) {
v->tabs.tabline[v->cur.col]=1;
}

static inline void tabclear_ansi(struct vte *v, unsigned int p) {
switch (p) {
	case 0: v->tabs.tabline[v->cur.col]=0; break;
	case 3: memset(v->tabs.tabline,0,sizeof(unsigned int)*v->config.columns); break;
}
}

int init_vte(struct vte *vte, struct config *config, struct pty *pty, struct all_event *events, struct texttap *texttap,
		unsigned int inputbuffersize, unsigned int messagebuffersize, unsigned int writebuffermax) {
unsigned int ui;
unsigned int escapebuffersize=256; // (NPAR=16)*10=160

if (messagebuffersize<10) GOTOERROR; // only 10 really needed, for ]Pxxxxxxx\0; size>=1 is assumed when creating message

vte->config.rows=config->rows;
vte->config.columns=config->columns;
vte->config.rowsm1=config->rows-1;
vte->config.columnsm1=config->columns-1;
vte->config.isautowrap=1;
vte->config.isautorepeat=1;

vte->baggage.pty=pty;
vte->baggage.events=events;
vte->baggage.texttap=texttap;

vte->readqueue.max_buffer=inputbuffersize;
vte->input.escape.max_buffer=escapebuffersize;
vte->input.message.max_buffer=messagebuffersize;
vte->input.message.max_bufferm1=messagebuffersize-1;
vte->writequeue.limit_buffer=writebuffermax;
ui= inputbuffersize + escapebuffersize + messagebuffersize;
if (!(vte->readqueue.buffer = vte->tofree.buffer = MALLOC(ui))) GOTOERROR;
vte->readqueue.q=vte->readqueue.buffer;
vte->input.escape.buffer=vte->readqueue.buffer+inputbuffersize;
vte->input.message.buffer=vte->input.escape.buffer+escapebuffersize;

memcpy(vte->input.palette.buffer,"]P",2);

vte->tabs.tablinemax=vte->config.columns;
if (!(vte->tabs.tabline=ZTMALLOC(vte->tabs.tablinemax,unsigned int))) GOTOERROR;
(void)settabstops(vte);

vte->writequeue.fd=pty->master;

if (config->isdarkmode) (void)setcolors_vte(vte,&config->darkmode);
else (void)setcolors_vte(vte,&config->lightmode);

vte->sgr.fgindex=FGCOLOR_VTE;
vte->sgr.bgindex=BGCOLOR_VTE;
vte->curfgcolor=&vte->colors[vte->sgr.fgindex];
vte->curbgcolor=&vte->colors[vte->sgr.bgindex];

vte->scrolling.top=0;
vte->scrolling.bottom=vte->config.rowsm1;

return 0;
error:
	return -1;
}
void deinit_vte(struct vte *vte) {
IFFREE(vte->tofree.buffer);
IFFREE(vte->tofree.writeq);
}

static uint32_t utf8tovalue(struct vte *v, unsigned char *utf8, unsigned int utf8len) {
uint32_t value;

/* UTF: (1): 7bits (0..7f), (2): 5b,6b (0..7ff), (3): 4b,6b,6b (0..ffff), (4): 3b,6b,6b,6b (0..10ffff)
 * highest value is 0x10ffff, so we can pack this in 21bits no problem
 * Note that reverse mapping is not unique. We can map all values to a 4byte encoding.
 */
switch (utf8len) {
	case 1:
		value=*utf8;
		break;
	case 2:
		value=(((unsigned int)utf8[0]&(1|2|4|8|16))<<6) | (((unsigned int)utf8[1]&(1|2|4|8|16|32))<<0);
		break;
	case 3:
		value= (((unsigned int)utf8[0]&(1|2|4|8))<<12)
						| (((unsigned int)utf8[1]&(1|2|4|8|16|32))<<6)
						| (((unsigned int)utf8[2]&(1|2|4|8|16|32))<<0);
		break;
	case 4:
		value= (((unsigned int)utf8[0]&(1|2|4))<<18)
						| (((unsigned int)utf8[1]&(1|2|4|8|16|32))<<12)
						| (((unsigned int)utf8[2]&(1|2|4|8|16|32))<<6)
						| (((unsigned int)utf8[3]&(1|2|4|8|16|32))<<0);
		break;
	default: value=0; // can't happen
}
value|=v->sgr.underlinemask; // bit 30
#ifndef SPACEHASFG
if (value==32) { // whitespace gets foreground 0
	value|=v->curbgcolor->bgvaluemask; // bits 22..25
} else {
	value|=v->curbgcolor->bgvaluemask; // bits 22..25
	value|=v->curfgcolor->fgvaluemask; // bits 26..29
}
#else
value|=v->curbgcolor->bgvaluemask; // bits 22..25
value|=v->curfgcolor->fgvaluemask; // bits 26..29
#endif

return value;
}

#ifndef SPACEHASFG
#define BLANKVALUE(a) (32|(a)->curbgcolor->bgvaluemask)
#else
#define BLANKVALUE(a) (32|(a)->curbgcolor->bgvaluemask|(a)->curfgcolor->fgvaluemask)
#endif

static inline void eraseinline(struct vte *v, unsigned int start, unsigned int count) {
if (!count) return; // xclient assumes it
(void)eraseinline_event(v->baggage.events,BLANKVALUE(v),v->cur.row,1,start,count);
}

static void eraseline(struct vte *v, unsigned int rownum, unsigned int n) {
if (!n) return;
(void)eraseinline_event(v->baggage.events,BLANKVALUE(v),rownum,n,0,v->config.columns);
}

static void insertlines(struct vte *v, unsigned int n) {
if (!n) return;
n=_BADMIN(n,v->config.rows-v->cur.row);
(void)insertline_event(v->baggage.events,BLANKVALUE(v),v->cur.row,v->scrolling.bottom,n);
}

static void deleteline_ansi(struct vte *v, unsigned int n) {
if (!n) return;
n=_BADMIN(n,v->config.rows-v->cur.row);
(void)deleteline_event(v->baggage.events,BLANKVALUE(v),v->cur.row,v->scrolling.bottom,n);
}

#define lf(a) index_ansi(v)
static void index_ansi(struct vte *v) {
unsigned int row;
v->cur.isovercol=0;
row=v->cur.row;
#if 0
if (v->cur.row!=v->config.rowsm1) { v->cur.row+=1; return; }
(void)scroll1up_event(v->baggage.events,BLANKVALUE(v),0,v->config.rowsm1);
#endif
if (row==v->scrolling.bottom) {
	(void)scroll1up_event(v->baggage.events,BLANKVALUE(v),v->scrolling.top,v->scrolling.bottom);
	return;
}
if (row!=v->config.rowsm1) {
	v->cur.row=row+1;
}
}

static void reverseindex_ansi(struct vte *v) {
unsigned int row=v->cur.row;
#if 0
if (v->cur.row) { v->cur.row-=1; return; }
(void)scroll1down_event(v->baggage.events,BLANKVALUE(v),0,v->config.rowsm1);
#endif
v->cur.isovercol=0;
if (row==v->scrolling.top) {
	(void)scroll1down_event(v->baggage.events,BLANKVALUE(v),v->scrolling.top,v->scrolling.bottom);
	return;
}
if (row) {
	v->cur.row=row-1;
}
}

static inline void advanceovercol(struct vte *v) {
if (!v->cur.isovercol) return;
v->cur.isovercol=0;
v->cur.col=0;
if (v->cur.row!=v->config.rowsm1) v->cur.row+=1;
}

static inline void incrcursor(struct vte *v) {
if (v->cur.col!=v->config.columnsm1) { v->cur.col+=1; return; }
if (!v->config.isautowrap) { v->cur.col=0; return; } // TODO check if col=0 should be removed
v->cur.isovercol=1;
// fprintf(stderr,"%s:%d col:%u columnsm1:%u\n",__FILE__,__LINE__,v->cur.col,v->config.columnsm1);
#if 0
v->cur.col=0;
if (v->cur.row!=v->config.rowsm1) { v->cur.row+=1; return; }
(void)scroll1up_event(v->baggage.events,BLANKVALUE(v),0,v->config.rowsm1);
#endif
}

static uint32_t addchar(struct vte *v, unsigned char *utf8, unsigned int utf8len) {
uint32_t value;
value=utf8tovalue(v,utf8,utf8len);
v->input.repeat.value=value;
(void)advanceovercol(v);
if (v->config.isinsertmode) {
	if (v->cur.col!=v->config.columnsm1) (void)ich_event(v->baggage.events,BLANKVALUE(v),v->cur.row,v->cur.col,1);
}
(void)addchar_event(v->baggage.events,value,v->cur.row,v->cur.col);
(void)incrcursor(v);
return value;
}

static void repeatchar(struct vte *v) {
uint32_t value;
value=v->input.repeat.value;
(void)advanceovercol(v);
(void)addchar_event(v->baggage.events,value,v->cur.row,v->cur.col);
if (v->config.isinsertmode) {
	if (v->cur.col!=v->config.columnsm1) (void)ich_event(v->baggage.events,BLANKVALUE(v),v->cur.row,v->cur.col,1);
}
(void)incrcursor(v);
}

static inline int printhex3(char *lead, unsigned char *data, unsigned int len, unsigned int line) {
fprintf(stderr,"%s:%d %s (%d):",__FILE__,line,lead,len);
while (len) {
	if (isprint(*data)) {
		fputc(*data,stderr);
	} else switch (*data) {
		case 10: fputs("(LF)",stderr); break;
		case 13: fputs("(CR)",stderr); break;
		case 14: fputs("(SO)",stderr); break;
		case 15: fputs("(SI)",stderr); break;
		case 27: fputs("(ESC)",stderr); break;
		default:
			fprintf(stderr,"(0x%02x)",*data);
	}
	len--;
	data++;
}
fputc('\n',stderr);
return 0;
}
static int printhex2(char *lead, unsigned char *data, unsigned int len, unsigned int line) {
fprintf(stderr,"%s:%d %s (%d):",__FILE__,line,lead,len);
while (len) {
	fprintf(stderr," 0x%02x_'%c'",*data,isprint(*data)?*data:'?');
	len--;
	data++;
}
fputc('\n',stderr);
return 0;
}

static inline void cursorbackward_ansi(struct vte *v, unsigned int d);

static void buildutf8(struct vte *v, unsigned char c) {
switch (v->input.utf8.bytesleft) {
	case 3: *v->input.utf8.cur=c; v->input.utf8.cur+=1; v->input.utf8.bytesleft=2; break;
	case 2: *v->input.utf8.cur=c; v->input.utf8.cur+=1; v->input.utf8.bytesleft=1; break;
	default:
		*v->input.utf8.cur=c;
//	v->input.utf8.bytesleft=0;
		v->input.utf8.isdone=1;
		break;
}
}

static void processmessage(int *isbugout_inout, struct vte *v) {
unsigned char *data;
unsigned int len;
if (v->input.message.isoverrun) return;
data=v->input.message.buffer;
len=v->input.message.len;
// printhex2("Message",(unsigned char *)data,len,__LINE__);
// if (len==v->input.message.max_buffer) return; // room for a 0 has been reserved in buildmessage
data[len]='\0';
*isbugout_inout=1;
(void)message_event(v->baggage.events,(char *)data,len); // no strdup, bugout => we can use buffer
// ]... is OSC
// ^... is private message
// _... is api message
// X... is string
// P... is ansi DCS
}

static inline void buildpalette(int *isbugout_inout, struct vte *v, unsigned char c) {
switch (c) {
	case 24: // CAN
	case 26: // SUB
		v->input.mode=0;
		break;
	case 27: // ESC
		v->input.mode=ESCAPE_MODE_INPUT_VTE;
		v->input.escape.len=0;
		v->input.escape.lastmode=PALETTE_MODE_INPUT_VTE;
		break;
	case 17: // XON
	case 19: // XOFF
		break;
	default:
		v->input.palette.buffer[v->input.palette.len]=c;
		v->input.palette.len+=1;
		if (v->input.palette.len==9) {
			v->input.mode=0;
			*isbugout_inout=1;
			(void)message_event(v->baggage.events,(char *)v->input.palette.buffer,9); // no strdup, bugout => we can use buffer
		}
}
}


static inline void buildmessage(int *isbugout_inout, struct vte *v, unsigned char c) {
static char oscR[]={']','R',0};
if (v->input.message.len==v->input.message.max_bufferm1) { // reserve 1 for 0
#if 1
				fprintf(stderr,"%s:%d Message sequence too long: %u",__FILE__,__LINE__,v->input.message.len);
#endif
		v->input.message.isoverrun=1;
		v->input.message.cur=v->input.message.buffer+1;
		v->input.message.len=1;
}
if (v->input.message.isosc) {
	if (c==7) { // BEL
		v->input.mode=0;
		(void)processmessage(isbugout_inout,v);
		return;
	}
	if (1==v->input.message.len) {
		switch (c) {
			case 'P':
				v->input.mode=PALETTE_MODE_INPUT_VTE;
				v->input.palette.len=2; // buffer already has ]P
				return;
			case 'R':
				v->input.mode=0;
				(void)message_event(v->baggage.events,oscR,2);
				*isbugout_inout=1;
				return;
		}
	}
}
switch (c) {
	case 24: // CAN
	case 26: // SUB
		v->input.mode=0;
		break;
	case 27: // ESC
		v->input.mode=ESCAPE_MODE_INPUT_VTE;
		v->input.escape.len=0;
		v->input.escape.lastmode=MESSAGE_MODE_INPUT_VTE;
		break;
	case 17: // XON
	case 19: // XOFF
		break;
	default:
		*(v->input.message.cur)=c;
		v->input.message.len+=1;
		v->input.message.cur+=1;
}
}

static inline void buildescape(int *isdone_inout, struct vte *v, unsigned char c) {
if (!v->input.escape.len) {
	v->input.escape.isoverrun=0;
	v->input.escape.cur=v->input.escape.buffer+1;
	v->input.escape.len=1;
#if 0
	fprintf(stderr,"%s:%d Buildescape character: 0x%02x\n",__FILE__,__LINE__,c);
#endif
	switch ( (v->input.escape.buffer[0]=c) ) {
		case ']': // OSC
		case '_': case 'P': case '^': case 'X': // wants ST terminator, no BEL
			v->input.mode=MESSAGE_MODE_INPUT_VTE;
			v->input.message.isosc=(c==']');
			v->input.message.len=1;
			v->input.message.buffer[0]=c;
			v->input.message.cur=v->input.message.buffer+1;
			break;
		case 24: case 26: // one letter escapes
		case '#':
		case '7': // DECSC
		case '8': // DECRC
		case '=': case '>': 
		case 'D': case 'E': case 'H': case 'M':
		case 'Q': // PU1 private use 1
		case 'R': // PU2 private use 2
		case 'Z':
		case '\\': // ST
		case 'b': // EMI enable manual input
		case 'c': 
			*isdone_inout=1;
			break;
		case '(': case ')': // linux
		case '%': break; // linux iso8859/utf8
		default:
			if ( (c < 0x40) || (c > 0x5f) ) { // wikipedia has 0x40..0x5f, expand this as we find more
#if 1
				fprintf(stderr,"%s:%d Unexpected character: 0x%02x\n",__FILE__,__LINE__,c);
#endif
// what should we do here? lots of terms do this (discard esc, discard c)
				v->input.mode=0;
				return;
			}
			break;
	}
} else {
	if (v->input.escape.len==v->input.escape.max_buffer) { // overrun
#if 1
				fprintf(stderr,"%s:%d Escape sequence too long: %u",__FILE__,__LINE__,v->input.escape.len);
#endif
		v->input.escape.isoverrun=1;
		v->input.escape.cur=v->input.escape.buffer+1;
		v->input.escape.len=1;
	} 
	switch (c) {
		case 24: // CAN
		case 26: // SUB
			v->input.mode=0;
			break;
		case 27: // ESC, ignore existing and start new escape 
			v->input.escape.len=0;
			break;
		case 17: // XON
		case 19: // XOFF
			break;
		default:
			*(v->input.escape.cur)=c;
			v->input.escape.len+=1;
			v->input.escape.cur+=1;
			if ((c>=0x40)&&(c<=0x7e)) *isdone_inout=1;
	}
}
}

#define tab(a) cursorhorizontaltab_ansi(a,1)
#if 0
static void tab(struct vte *v) {
unsigned int tcol;
tcol=(v->cur.col|7)+1;
if (tcol>v->config.columnsm1) tcol=v->config.columnsm1;
v->cur.col=tcol;
}
#endif


#if 0
static unsigned int strtou(char *str) {
unsigned int ret=0;
switch (*str) {
	case '1': ret=1; break;
	case '2': ret=2; break;
	case '3': ret=3; break;
	case '4': ret=4; break;
	case '5': ret=5; break;
	case '6': ret=6; break;
	case '7': ret=7; break;
	case '8': ret=8; break;
	case '9': ret=9; break;
	case '+':
	case '0': break;
	default: return 0; break;
}
while (1) {
	str++;
	switch (*str) {
		case '9': ret=ret*10+9; break;
		case '8': ret=ret*10+8; break;
		case '7': ret=ret*10+7; break;
		case '6': ret=ret*10+6; break;
		case '5': ret=ret*10+5; break;
		case '4': ret=ret*10+4; break;
		case '3': ret=ret*10+3; break;
		case '2': ret=ret*10+2; break;
		case '1': ret=ret*10+1; break;
		case '0': ret=ret*10; break;
		default: return ret; break;
	}
}
return ret;
}
#endif

static void getparam(unsigned int *p_out, unsigned char **data_inout, unsigned char term, unsigned int def) {
unsigned int p=0;
unsigned char *data=*data_inout;
switch (*data) {
	case '0': p=0; break; case '1': p=1; break; case '2': p=2; break; case '3': p=3; break; case '4': p=4; break;
	case '5': p=5; break; case '6': p=6; break; case '7': p=7; break; case '8': p=8; break; case '9': p=9; break;
	case ';': *p_out=def; *data_inout=data+1; return;
	default: if (*data==term) { *p_out=def; *data_inout=NULL; return; }
}
while (1) {
	data++;
	switch (*data) {
		case'0':p=p*10+0;break;case'1':p=p*10+1;break;case'2':p=p*10+2;break;case'3':p=p*10+3;break;case'4':p=p*10+4;break;
		case'5':p=p*10+5;break;case'6':p=p*10+6;break;case'7':p=p*10+7;break;case'8':p=p*10+8;break;case'9':p=p*10+9;break;
		case ';': *p_out=p; *data_inout=data+1; return;
		default: if (*data==term) { *p_out=p; *data_inout=NULL; return; }
	}
}
}


static void sgr_ansi(struct vte *v, unsigned char *data_in, unsigned int len) {
// ends in m
// m=>0m
// 0:reset,1:bright,4:underline,7:reverse,22:notbright,24:notunderline,27:notreverse,39:deffg,49:defbg
// 38,48:either 8bit or 24bit color, don't support these
// 30-37:fg=black,red,green,yellow,blue,magenta,cyan,white
// 90-97:fg+bright
// 40-47:bg=
// 100-107:bg+bright
int fgindex,bgindex;
unsigned char *data=data_in;
unsigned int p;

fgindex=v->sgr.fgindex;
bgindex=v->sgr.bgindex;

while (1) {
	(void)getparam(&p,&data,'m',0);
	switch (p) {
		case 0:
			fgindex=FGCOLOR_VTE;
			bgindex=BGCOLOR_VTE;
			v->sgr.isinvisible=
			v->sgr.isbright=
			v->sgr.isreverse=0;
			v->sgr.underlinemask=0;
			break;
		case 1: v->sgr.isbright=1; break;
		case 2: break; // TODO half-bright, what should this do?
		case 4: v->sgr.underlinemask=UNDERLINEBIT_VALUE; break;
		case 5: break; // blink, ignored, could do italics
		case 7: v->sgr.isreverse=1; break;
		case 8: v->sgr.isinvisible=1; break;
		case 10: break; // "reset mapping, display control flag and toggle meta flag"
		case 11: break; // "select null mapping, set display control flag, reset toggle meta flag"
		case 12: break; // "selectnull mapping, set display control flag, set toggle meta flag"
		case 21: break; // set normal intensity? doubly underlined?
		case 22: v->sgr.isbright=0; break;
		case 23: break; // not italic
		case 24: v->sgr.underlinemask=0; break;
		case 25: break; // blink off
		case 27: v->sgr.isreverse=0; break;
		case 28: v->sgr.isinvisible=0; break;
		case 29: break; // not overstrike
		case 30: fgindex=0; break; case 31: fgindex=1; break; case 32: fgindex=2; break; case 33: fgindex=3; break;
		case 34: fgindex=4; break; case 35: fgindex=5; break; case 36: fgindex=6; break; case 37: fgindex=7; break;
		case 38: v->sgr.underlinemask=UNDERLINEBIT_VALUE; fgindex=FGCOLOR_VTE; break;
		case 39: v->sgr.underlinemask=0; fgindex=FGCOLOR_VTE; break;
		case 40: bgindex=0; break; case 41: bgindex=1; break; case 42: bgindex=2; break; case 43: bgindex=3; break;
		case 44: bgindex=4; break; case 45: bgindex=5; break; case 46: bgindex=6; break; case 47: bgindex=7; break;
		case 49: bgindex=BGCOLOR_VTE; break;
		// 90-107 are from aixterm, according to wikipedia, not common but how else to do bright/bright ?
		case 90: fgindex=8; break; case 91: fgindex=9; break; case 92: fgindex=10; break; case 93: fgindex=11; break;
		case 94: fgindex=12; break; case 95: fgindex=13; break; case 96: fgindex=14; break; case 97: fgindex=15; break;
		case 100: bgindex=8; break; case 101: bgindex=9; break; case 102: bgindex=10; break; case 103: bgindex=11; break;
		case 104: bgindex=12; break; case 105: bgindex=13; break; case 106: bgindex=14; break; case 107: bgindex=15; break;
		default:
			printhex2("Unhandled SGR",data_in,len,__LINE__);
			fprintf(stderr,"%s:%d SGR ignoring unknown %u\n",__FILE__,__LINE__,p);
			break;
	}
	if (!data) {
		unsigned int isreverse;
		isreverse=v->sgr.isreverse ^ v->sgr.issuperreverse;
		if ((v->sgr.isbright)&&(fgindex<8)) fgindex|=8; // maybe bright comes after reverse?
		if (isreverse)  { v->curfgcolor=&v->colors[bgindex]; v->curbgcolor=&v->colors[fgindex]; }
		else { v->curfgcolor=&v->colors[fgindex]; v->curbgcolor=&v->colors[bgindex]; }
		v->sgr.fgindex=fgindex;
		v->sgr.bgindex=bgindex;
		if (v->sgr.isinvisible) v->curfgcolor=v->curbgcolor;
		break;
	}
}
}

static inline void cursorup_ansi(struct vte *v, unsigned int d) {
unsigned int r;
v->cur.isovercol=0;
r=v->cur.row;
if (d>=r) r=0; else r-=d;
v->cur.row=r;
}
static inline void cursordown_ansi(struct vte *v, unsigned int d) {
unsigned int r;
v->cur.isovercol=0;
r=v->cur.row;
r+=d;
r=_BADMIN(r,v->config.rowsm1);
v->cur.row=r;
}
static inline void cr(struct vte *v) {
v->cur.col=0;
}
static inline void cursorbackward_ansi(struct vte *v, unsigned int d) {
unsigned int c;
v->cur.isovercol=0;
c=v->cur.col;
if (d>=c) c=0; else c-=d;
v->cur.col=c;
}
static inline void cursorforward_ansi(struct vte *v, unsigned int d) {
unsigned int c;
v->cur.isovercol=0;
c=v->cur.col;
c+=d;
c=_BADMIN(c,v->config.columnsm1);
v->cur.col=c;
}

static inline void cursornextline_ansi(struct vte *v, unsigned int d) {
(void)cursordown_ansi(v,d);
v->cur.col=0;
}
static inline void cursorprecedingline_ansi(struct vte *v, unsigned int d) {
(void)cursorup_ansi(v,d);
v->cur.col=0;
}
static inline void cursorhorizontalabsolute_ansi(struct vte *v, unsigned int d) {
v->cur.isovercol=0;
if (!d) { v->cur.col=0; return; }
if (d>v->config.columns) { v->cur.col=v->config.columnsm1; return; }
v->cur.col=d-1;
}
static inline void cursorhorizontaltab_ansi(struct vte *v, unsigned int d) {
unsigned int *tabstops;
unsigned int col,maxcol;

v->cur.isovercol=0;
if (!d) d=1;
col=v->cur.col;
maxcol=v->config.columnsm1;
tabstops=v->tabs.tabline+col;
while (1) {
	if (col==maxcol) break;
	col++;
	tabstops++;
	if (*tabstops) {
		d--;
		if (!d) break;
	}
}
v->cur.col=col;
}

static inline void cursorbackwardtab_ansi(struct vte *v, unsigned int d) {
unsigned int *tabstops;
unsigned int col;

v->cur.isovercol=0;
if (!d) d=1;
col=v->cur.col;
tabstops=v->tabs.tabline+col;
while (1) {
	if (!col) break;
	col--;
	tabstops--;
	if (*tabstops) {
		d--;
		if (!d) break;
	}
}
v->cur.col=col;
}

static inline void repeatchar_ansi(struct vte *v, unsigned int d) {
if (!d) return;
v->input.repeat.fuse+=d;
v->input.mode=REPEAT_MODE_INPUT_VTE;
}


static inline void setcursorposition_ansi(struct vte *v, unsigned char *data, unsigned int len, unsigned char terminator) {
// n;mH: row n, col m, 1-based, n=''=>1, set to 1,1 until overwritten
unsigned int p;

v->cur.isovercol=0;

(void)getparam(&p,&data,terminator,1);
#if 0
	fprintf(stderr,"%s:%d CUP to %u, istopleft:%u scrolling.top:%u\n",__FILE__,__LINE__,p,v->scrolling.istopleft,v->scrolling.top); 
#endif
if (v->scrolling.istopleft) {
	unsigned int ui;
	ui=v->scrolling.bottom-v->scrolling.top;
	v->cur.row=(p>ui) ? v->scrolling.bottom : v->scrolling.top+p-1;
} else {
	v->cur.row=(p-1)%v->config.rows;
}
if (data) {
	(void)getparam(&p,&data,terminator,1);
	v->cur.col=(p-1)%v->config.columns;
} else v->cur.col=0;
}

static void dsr_ansi(struct vte *v, unsigned int p) {
char buffer[16];
// get [6n, return cursor position as ESC[n;mR, n=row,m=col, 1 based
switch (p) {
	case 5:
		strcpy(buffer,"\x1b[0n"); // no hardware faults
		(void)generic_event(v->baggage.events,buffer);
		break;
	case 6:
		snprintf(buffer,16,"\x1b[%u;%uR",v->cur.row+1,v->cur.col+1);
		(void)generic_event(v->baggage.events,buffer);
		break;
}
}

static inline void deviceattributes_ansi(struct vte *v, unsigned char *data, unsigned int len) {
(void)generic_event(v->baggage.events,vt102_decid);
}

static void eraseindisplay(struct vte *v, unsigned int type) {
// type: 0=>erase from cursor to end, 1=>from 0,0 to cursor, 2,3,else=>whole screen
unsigned int count;
v->cur.isovercol=0;
switch (type) {
	case 0:
		count=v->config.rowsm1-v->cur.row;
		(void)eraseinline(v,v->cur.col,v->config.columns-v->cur.col);
		(void)eraseline(v,v->cur.row+1,count);
		break;
	case 1:
		count=v->cur.row;
		(void)eraseinline(v,0,v->cur.col);
		(void)eraseline(v,0,count);
		break;
	case 3: // TODO 3: clear scrollback buffer as well
	case 2:
		(void)eraseline(v,0,v->config.rows);
		break;
	default: return;
}
}

static void reset_ansi(struct vte *v) {
(void)eraseline(v,0,v->config.rows);
v->cur.isovercol=0;
v->cur.row=v->cur.col=0;
(void)reset_event(v->baggage.events);
}

static void insertchar_ansi(struct vte *v, unsigned int num) {
unsigned int col;
// fprintf(stderr,"Insert chars: %u, %u chars\n",v->cur.col,num);
(void)advanceovercol(v);
col=v->cur.col;
if (col+num>=v->config.columns) num=v->config.columnsm1-col;
if (!num) return;
(void)ich_event(v->baggage.events,BLANKVALUE(v),v->cur.row,col,num);
}
static void dch_ansi(struct vte *v, unsigned int num) {
unsigned int col;
// fprintf(stderr,"Delete chars: %u, %u chars\n",v->cur.col,num);
v->cur.isovercol=0;
col=v->cur.col;
if (col+num>=v->config.columns) num=v->config.columnsm1-col;
if (!num) return; // assumed by xclient

(void)dch_event(v->baggage.events,BLANKVALUE(v),v->cur.row,col,num);
}

static void setreverse_sgr(struct vte *v, unsigned int set) {
struct rgb_vte *temp;
if (set==v->sgr.issuperreverse) return;
v->sgr.issuperreverse=set;
temp=v->curfgcolor;
v->curfgcolor=v->curbgcolor;
v->curbgcolor=temp;
(void)reverse_event(v->baggage.events);
}

static void scrollingregion(struct vte *v, unsigned char *data, unsigned int len) {
// N;Mr, top:N, bottom:M
unsigned int top,bottom=v->config.rows;
(void)getparam(&top,&data,'r',1);
if (top) top-=1;
if (data) {
	(void)getparam(&bottom,&data,'r',bottom);
}
v->scrolling.top=top;
v->scrolling.bottom=bottom-1;
#if 0
fprintf(stderr,"%s:%d %s top:%u bottom:%u\n",__FILE__,__LINE__,__FUNCTION__,top,bottom);
#endif
}

static inline void processg1charset(struct vte *v, unsigned char *data, unsigned int len) {
switch (data[len-1]) {
	case 'B': break; // default iso8859-1
	case '0': break; // vt100
	case 'U': break; // null mapping
	case 'K': break; // user mapping
}
}
static inline void processg0charset(struct vte *v, unsigned char *data, unsigned int len) {
switch (data[len-1]) {
	case 'B': break; // default iso8859-1
	case '0': break; // vt100
	case 'U': break; // null mapping
	case 'K': break; // user mapping
}
}
static inline void processhash(struct vte *v, unsigned char *data, unsigned int len) {
switch (data[len-1]) {
	case '8': break; // DECALN, TODO fill screen with E's
}
}

static inline void ech_ansi(struct vte *v, unsigned int count) {
(void)eraseinline(v,v->cur.col,_BADMIN(count,v->config.columns-v->cur.col));
}

static inline void vpa_ansi(struct vte *v, unsigned int row) {
row=_BADMIN(row,v->config.rowsm1);
v->cur.row=row;
}

static void processcsi(struct vte *v, unsigned char *data_in, unsigned int len) {
unsigned char *data=data_in;
unsigned int p;
int unk=1;

#if 0
printhex2("Received csi escape",data,len,__LINE__);
#endif

switch (data[len-1]) {
	case '@': // ansi ICH
		(void)getparam(&p,&data,'@',1);
		(void)insertchar_ansi(v,p);
		break;
	case 'A': // ansi CUU
		(void)getparam(&p,&data,'A',1);
		(void)cursorup_ansi(v,p);
		break;
	case 'B': // ansi CUD
		(void)getparam(&p,&data,'B',1);
		(void)cursordown_ansi(v,p);
		break;
	case 'C': // ansi CUF
		(void)getparam(&p,&data,'C',1);
		(void)cursorforward_ansi(v,p);
		break;
	case 'D': // ansi CUB
		(void)getparam(&p,&data,'D',1);
		(void)cursorbackward_ansi(v,p);
		break;
	case 'E': // ansi CNL
		(void)getparam(&p,&data,'E',1);
		(void)cursornextline_ansi(v,p);
		break;
	case 'F': // ansi CPL
		(void)getparam(&p,&data,'F',1);
		(void)cursorprecedingline_ansi(v,p);
		break;
	case 'G': // ansi CHA
		(void)getparam(&p,&data,'G',1);
		(void)cursorhorizontalabsolute_ansi(v,p);
		break;
	case 'H': // ansi CUP
		(void)setcursorposition_ansi(v,data,len,'H');
//		printhex2("Set cursor position",data_in,len,__LINE__);
		break;
	case 'I': // ansi CHT
		(void)getparam(&p,&data,'I',1);
		(void)cursorhorizontaltab_ansi(v,p);
		break;
	case 'J': // ansi ED
		(void)getparam(&p,&data,'J',0);
		(void)eraseindisplay(v,p);
//		printhex2("Erase in display",data_in,len,__LINE__);
		break;
	case 'K': // ansi EL
		(void)getparam(&p,&data,'K',0);
		switch (p) {
			case 0:
				(void)eraseinline(v,v->cur.col,v->config.columns-v->cur.col);
				break;
			case 1:
				(void)eraseinline(v,0,v->cur.col);
				break;
			case 2:
				(void)eraseline(v,v->cur.row,1);
			default: printhex2("Erase in line",data_in,len,__LINE__); break;
		}
		break;
	case 'L': // ansi IL
		(void)getparam(&p,&data,'L',1);
		p=_BADMIN(p,v->config.rows);
		(void)insertlines(v,p);
//		printhex2("Insert lines",data_in,len,__LINE__);
		break;
	case 'M': // ansi DL
		(void)getparam(&p,&data,'M',1);
		p=_BADMIN(p,v->config.rows);
		(void)deleteline_ansi(v,p);
		break;
	case 'P': // ansi DCH
		(void)getparam(&p,&data,'P',1);
		(void)dch_ansi(v,p);
		break;
	case 'R': // ansi CPR
//		(void)cursorpositionreport_ansi(v,data,len);
		break;
	case 'W': // ansi CTC
//		(void)cursortabcontrol_ansi(v,data,len);
		break;
	case 'X': // ansi ECH
		(void)getparam(&p,&data,'X',1);
		(void)ech_ansi(v,p);
		break;
	case 'Y': // ansi CVT
//		(void)cursorverticaltab_ansi(v,data,len);
		break;
	case 'Z': // ansi CBT
		(void)getparam(&p,&data,'Z',1);
		(void)cursorbackwardtab_ansi(v,p);
		break;
	case ']': // TODO support linux private csi
		break;
	case 'a': // ansi HPR, format effector of CUF
		(void)getparam(&p,&data,'a',1);
		(void)cursorforward_ansi(v,p);
		break;
	case 'b': // ansi REP
		(void)getparam(&p,&data,'b',1);
		(void)repeatchar_ansi(v,p);
		break;
	case 'c': // ansi DA
		if (len==1) (void)deviceattributes_ansi(v,data,len);
		else if ( (len==3) && (data[0]=='?') ) { // data[1]: 0=>reset,1=>hide,2..9=>height
			unsigned char temp[4]={'[','?','0','c'};
			temp[2]=data[1];
			(void)smessage2_event(v->baggage.events,temp,4);
		} else {
			printhex2("Unknown csi escape",data,len,__LINE__);
		}
		break;
	case 'd': // ansi VPA
		(void)getparam(&p,&data,'d',1);
		(void)vpa_ansi(v,p);
		break;
	case 'e': // ansi VPR, format effector version of CUD
		(void)getparam(&p,&data,'d',1);
		(void)cursordown_ansi(v,p);
		break;
	case 'f': // ansi HVP, format effector version of CUP
		(void)setcursorposition_ansi(v,data,len,'f');
		break;
	case 'g': // ansi TBC
		(void)getparam(&p,&data,'g',0);
		(void)tabclear_ansi(v,p);
		break;
	case 'h':
		switch (len) {
			case 2:
				switch (data[0]) {
					case '3': v->config.isshowcontrol=1; unk=0; break; // DECCRM on
					case '4': v->config.isinsertmode=1; unk=0; break; // DECIM
				}
				break;
			case 3:
				if (data[0]=='?') {
					switch (data[1]) {
						case '1': v->keyboardstates.iscursorappmode=1; (void)appcursor_event(v->baggage.events,1); unk=0; break;
						case '3': unk=0; break; // DECCOLM set 132 columns
						case '5': (void)setreverse_sgr(v,1); unk=0; break; // DECSCNM reverse video
						case '6': v->scrolling.istopleft=1; unk=0; break; // DECOM
						case '7': v->config.isautowrap=1; unk=0; break; // DECAWM
						case '8': v->config.isautorepeat=1; (void)autorepeat_event(v->baggage.events,1); unk=0; break; // DECARM
						case '9': unk=0; break; // X10 mouse reporting
					}
				}
				break;
			case 4:
				if (!memcmp(data,"?25h",4)) { // cursor on
					(void)smessage2_event(v->baggage.events,(unsigned char *)"[?25h",5);
					unk=0;
				} 
				break;
			case 6:
				if (!memcmp(data,"?1000h",6)) { // TODO X11 mouse reporting
				} else if (!memcmp(data,"?1049h",6)) { // disable history // TODO
				}
				break;
			}
			if (unk) printhex2("Unknown csi escape",data,len,__LINE__);
		break;
	case 'l':
		switch (len) {
			case 2:
				switch (data[0]) {
					case '3': v->config.isshowcontrol=0; unk=0; break; // DECCRM off
					case '4': v->config.isinsertmode=0; unk=0; break; // DECIM
				}
				break;
			case 3:
				if (data[0]=='?') {
					switch (data[1]) {
						case '1': v->keyboardstates.iscursorappmode=0; (void)appcursor_event(v->baggage.events,0); unk=0; break;
						case '3': unk=0; break; // DECCOLM set 80 columns
						case '5': (void)setreverse_sgr(v,0); unk=0; break; // DECSCNM restore reverse video
						case '6': v->scrolling.istopleft=0; unk=0; break; // DECOM
						case '7': v->config.isautowrap=0; unk=0; break; // DECAWM
						case '8': v->config.isautorepeat=0; (void)autorepeat_event(v->baggage.events,0); unk=0; break; // DECARM
						case '9': unk=0; break; // X10 mouse reporting
					}
				}
				break;
			case 4:
				if (!memcmp(data,"?25l",4)) { // cursor off
					(void)smessage2_event(v->baggage.events,(unsigned char *)"[?25l",5);
					unk=0;
				} 
				break;
			case 6:
				if (!memcmp(data,"?1000l",6)) { // TODO X11 mouse reporting
				} else if (!memcmp(data,"?1049l",6)) { // re-enable history // TODO
				}
				break;
			} 
#ifdef DEBUG2
// there's no real need to print recognized disabling
			if (unk) printhex2("Unknown csi escape",data,len,__LINE__);
#endif
		break;
	case 'm': //		printhex2("Color change",data,len,__LINE__);
		(void)sgr_ansi(v,data,len);
		break;
	case 'n': // ansi DSR
		(void)getparam(&p,&data,'n',0);
		(void)dsr_ansi(v,p);
		break;
	case 'o': // ansi DAQ
//		(void)defineareaqualification(v,data,len);
		break;
	case 'q': break; // linux DECLL keyboard LEDs, not much we can do, we could tell python though
	case 'r': // linux DECSTBM
		(void)scrollingregion(v,data,len);
		break;
	case 't':
		if ((len==7)&&(!memcmp(data,"22;0;0t",7))) {
			// store window and title
		} else if ((len==7)&&(!memcmp(data,"23;0;0t",7))) {
			// restore window and title
		} else printhex2("Unknown csi escape",data,len,__LINE__);
		break;
	default:
		printhex2("Unknown csi escape",data,len,__LINE__);
}
}


static inline void nextline_ansi(struct vte *v) {
(void)cr(v);
(void)lf(v);
}

static void save_currentstate(struct vte *v) {
// TODO test to see if linux saves v.keyboardstates
v->currentstate.isbright=v->sgr.isbright;
v->currentstate.isreverse=v->sgr.isreverse;
v->currentstate.issuperreverse=v->sgr.issuperreverse;
v->currentstate.isinvisible=v->sgr.isinvisible;
v->currentstate.isovercol=v->cur.isovercol;
v->currentstate.underlinemask=v->sgr.underlinemask;
v->currentstate.row=v->cur.row;
v->currentstate.col=v->cur.col;
v->currentstate.fgindex=v->curfgcolor->index;
v->currentstate.bgindex=v->curbgcolor->index;
v->currentstate.scrolltop=v->scrolling.top;
v->currentstate.scrollbottom=v->scrolling.bottom;
}
static void restore_currentstate(struct vte *v) {
v->sgr.isbright=v->currentstate.isbright;
v->sgr.isreverse=v->currentstate.isreverse;
v->sgr.issuperreverse=v->currentstate.issuperreverse;
v->sgr.isinvisible=v->currentstate.isinvisible;
v->cur.isovercol=v->currentstate.isovercol;
v->sgr.underlinemask=v->currentstate.underlinemask;
v->cur.row=v->currentstate.row;
v->cur.col=v->currentstate.col;
v->curfgcolor=&v->colors[v->currentstate.fgindex];
v->curbgcolor=&v->colors[v->currentstate.bgindex];
v->scrolling.top=v->currentstate.scrolltop;
v->scrolling.bottom=v->currentstate.scrollbottom;
}

static void processescape(int *isbugout_inout, struct vte *v) {
unsigned char *data;
unsigned int len;

if (v->input.escape.isoverrun) return; // data is overwritten

data=v->input.escape.buffer; // data is 0-term
len=v->input.escape.len; // len > 1, needed ESC+START+END to get here, ESC is clipped off, TERM is clipped off
#if 0
printhex3("escape",data,len,__LINE__);
#endif
switch (*data) {
	case 24: case 26: (ignore)addchar(v,&SUB,1); break;
	case '#': (void)processhash(v,data+1,len-1); break;
	case '%': 
		if ((len==2)&&(data[1]=='@')) v->config.is8859=1;
		else if ((len==2)&&(data[1]=='G')) v->config.is8859=0; // ignoring ESC%8
		else printhex2("escape",data,len,__LINE__);
		break;
	case '(': (void)processg0charset(v,data+1,len-1); break;
	case ')': (void)processg1charset(v,data+1,len-1); break;
	case '7': (void)save_currentstate(v); break; // DECSC
	case '8': (void)restore_currentstate(v); break; // DECRC
	case '=': v->keyboardstates.iskeypadappmode=1; break; // DECPNM
	case '>': v->keyboardstates.iskeypadappmode=0; break; // DECPAM
	case 'D': (void)index_ansi(v); break; // ansi IND
	case 'E': (void)nextline_ansi(v); break; // ansi NEL
	case 'H': (void)tabset_ansi(v); break; // ansi HTS
	case 'M': (void)reverseindex_ansi(v); break; // ansi RI
	case 'Q': break; // PU1
	case 'R': break; // PU2
#if 0
	case 'T': // ansi CCH -- what is this?
			(void)cancelpreviouscharacter_ansi(v,data,len);
		break;
#endif
	case 'Z': (void)deviceattributes_ansi(v,NULL,0); break;
	case '[': (void)processcsi(v,data+1,len-1); break;
	case '\\': 
		if (v->input.escape.lastmode==MESSAGE_MODE_INPUT_VTE) { // ansi ST, message is finished
			v->input.mode=0;
			(void)processmessage(isbugout_inout,v);
		} else { } // ansi DMI, disable manual input
		break; 
	case 'b': break; // ansi EMI, enable manual input
	case 'c': (void)reset_ansi(v); break; // ansi RSI
	default:
		printhex2("Unknown escape",data,len,__LINE__);
		break;
}
}

static int isaddtapevent(struct vte *v, unsigned int value) {
if (isnocbadd_texttap(v->baggage.texttap,value)) return 0;
(void)tap_event(v->baggage.events,value);
return 1;
}

// TODO change 5 to actual, perhaps 4: 1: cursor, 1: eraselines, 1: eraseinline, 1: tapevent
#define MINUNUSEDEVENTS	5
int processreadqueue_vte(struct vte *v) {
unsigned char *data;
unsigned int len;

data=v->readqueue.q;
len=v->readqueue.qlen;

#if 0
printhex3("vte read",data,len,__LINE__);
#endif
// if (v->cursor.unset(v->cursor.cursor)) GOTOERROR;
while (1) {
// fprintf(stderr,"%s:%d Processing character 0x%02x(%c) mode is %u\n",__FILE__,__LINE__,*data, isprint(*data)?*data:'?', v->input.mode);
//	fprintf(stderr,"%s:%d cursor is currently row:%u col:%u\n",__FILE__,__LINE__,v->cur.row,v->cur.col);
	if ( (v->baggage.events->unused.count < MINUNUSEDEVENTS)) goto endearly;
	switch (v->input.mode) {
		case UTF8_MODE_INPUT_VTE:
			if (!v->input.utf8.isdone) {
				if ((*data&(128|64))!=128) {
					*data=SUB;
					v->input.mode=0;
					continue;
				}
				(void)buildutf8(v,*data);
			}
			if (v->input.utf8.isdone) {
				uint32_t value;
				value=addchar(v,v->input.utf8.four,v->input.utf8.len);
				v->input.mode=0;
				if (isaddtapevent(v,value)) { data++; len--; goto endearly; }
			}
			break;
		case ESCAPE_MODE_INPUT_VTE:
			{
				int isdone=0;
				(void)buildescape(&isdone,v,*data);
				if (isdone) {
					int isbugout=0;
					v->input.mode=v->input.escape.lastmode;
					(void)processescape(&isbugout,v);
					if (isbugout) { data++; len--; goto endearly; }
				}
			}
			break;
		case REPEAT_MODE_INPUT_VTE:
			(void)repeatchar(v);
			v->input.repeat.fuse-=1;
			if (!v->input.repeat.fuse) v->input.mode=0;
			continue;
		case MESSAGE_MODE_INPUT_VTE:
			{
				int isbugout=0;
				(void)buildmessage(&isbugout,v,*data);
				if (isbugout) { data++; len--; goto endearly; }
			}
			break;
		case PALETTE_MODE_INPUT_VTE:
			{
				int isbugout=0;
				(void)buildpalette(&isbugout,v,*data);
				if (isbugout) { data++; len--; goto endearly; }
			}
			break;
		default: // if we wanted to, we could unwind this for all 8bit values
			if (*data&128) {
				if (v->config.is8859) { // TODO do other 8bit escapes
					if (*data==0x9b) { // CSI
						v->input.mode=ESCAPE_MODE_INPUT_VTE;
						v->input.escape.len=0;
						v->input.escape.lastmode=0;
						(void)buildescape(NULL,v,'[');
					} else {
						unsigned char utf82[2];
						unsigned int d=*data;
						utf82[0]=128|64|(d>>6);
						utf82[1]=128|(d&63);
						(void)addchar(v,utf82,2);
						if (isaddtapevent(v,d)) { data++; len--; goto endearly; }
					}
				} else {
					v->input.mode=UTF8_MODE_INPUT_VTE;
					if ((*data&(64+32))==64) { // two
						v->input.utf8.len=2; v->input.utf8.bytesleft=1;
					} else if ((*data&(64+32+16))==(64+32)) { // three
						v->input.utf8.len=3; v->input.utf8.bytesleft=2;
					} else if ((*data&(64+32+16+8))==(64+32+16)) { // four
						v->input.utf8.len=4; v->input.utf8.bytesleft=3;
					} else {
						memcpy(v->input.utf8.four,fffd_utf8,3);
						v->input.utf8.len=3;
						v->input.utf8.isdone=1;
						continue; // read char again
					}
					v->input.utf8.four[0]=*data;
					v->input.utf8.cur=v->input.utf8.four+1;
					v->input.utf8.isdone=0;
				}
			} else switch (*data) { // DECCRM is ignored here -- does linux ever do it?
				case 0: break; // ignored
//				case 5: break; // TODO ENQ
				case 7: (void)bell_event(v->baggage.events); data++; len--; goto endearly;
				case 8: (void)cursorbackward_ansi(v,1); break;
				case 9: (void)tab(v); break;
				case 10: // LF
				case 11: // VT
				case 12: // FF
					(void)lf(v);	
					if (isaddtapevent(v,*data)) { data++; len--; goto endearly; }
					break;
				case 13:
					(void)cr(v);
					if (isaddtapevent(v,*data)) { data++; len--; goto endearly; }
					break;
				case 14: break; // SO (Shift-Out)
				case 15: break; // SI (Shift-In)
				case 24: break; // CAN, ignored here
				case 26: break; // SUB, ignored here
				case 27: v->input.mode=ESCAPE_MODE_INPUT_VTE; v->input.escape.len=0; v->input.escape.lastmode=0; break;
				case 127: break;
				case 1: case 2: case 3: case 4:
				case 6:
				case 16: case 17: case 18: case 19: case 20: case 21: case 22:
				case 23:
				case 25:
					{ unsigned char ch='A'-1+*data; (void)addchar(v,(unsigned char *)"^",1); (void)addchar(v,&ch,1); }
					break;
				default:
					if (isprint(*data)) {
						(void)addchar(v,data,1);
					} else {
						fprintf(stderr,"%s:%d Unhandled 7bit character 0x%02x\n",__FILE__,__LINE__,*data);
					}
					if (isaddtapevent(v,*data)) { data++; len--; goto endearly; }
					break;
			}
	}
	len--;
	if (!len) break;
	data++;
}
// fprintf(stderr,"%s:%d setting cursor to row:%u col:%u\n",__FILE__,__LINE__,v->cur.row,v->cur.col);
v->readqueue.qlen=0;
(void)setcursor_event(v->baggage.events,v->cur.row,v->cur.col);
return 0;
endearly: // A note on bugout: it lets events/xclient use vte buffers w/o worrying about overwriting buffer
// BUT, the main reasons for bugout is: 1> so script can pause immediately, 2> script can resize surface immediately
	v->readqueue.q=data;
	v->readqueue.qlen=len;
	(void)setcursor_event(v->baggage.events,v->cur.row,v->cur.col);
	return 0;
}

int fillreadqueue_vte(struct vte *vte) {
struct pty *pty=vte->baggage.pty;
int k;
k=read(pty->master,vte->readqueue.buffer,vte->readqueue.max_buffer);
if (k<1) return -1;
vte->readqueue.q=vte->readqueue.buffer;
vte->readqueue.qlen=k;
// fprintf(stderr,"%s:%d:%s %d bytes read\n",__FILE__,__LINE__,__FUNCTION__,k);
return 0;
}

int insert_readqueue_vte(struct vte *vte, char *str, unsigned int len) {
unsigned char *dest;
unsigned int destlen,ui;
if (vte->input.mode) return -1;
ui=vte->readqueue.qlen;
dest=vte->readqueue.q+ui;
destlen=vte->readqueue.max_buffer-ui;
if (len>destlen) len=destlen;
memcpy(dest,str,len);
vte->readqueue.qlen+=len;
return 0;
}

int flush_vte(struct vte *vte) {
int k=0;
if (!vte->writequeue.len) return 0;
k=write(vte->writequeue.fd,vte->writequeue.buffer,vte->writequeue.len);
if (k<=0) GOTOERROR;
vte->writequeue.len-=k;
if (k==vte->writequeue.len) return 0;
memmove(vte->writequeue.buffer,vte->writequeue.buffer+k,vte->writequeue.len);
return 0;
error:
	return -1;
}
static int flush2_vte(struct vte *vte) {
static struct timeval tv0_global;
fd_set wset;
int fd=vte->writequeue.fd;
FD_ZERO(&wset);
FD_SET(fd,&wset);
if (1==select(fd+1,NULL,&wset,NULL,&tv0_global)) {
	if (flush_vte(vte)) GOTOERROR;
}
return 0;
error:
	return -1;
}
int writeorqueue_vte(int *isdrop_out, struct vte *vte, unsigned char *data, unsigned int len) {
#if 0
printhex3("vte write",data,len,__LINE__);
#endif
if (vte->writequeue.len+len>vte->writequeue.max_buffer) {
	unsigned char *temp;
	unsigned int max;
	max=_BADMIN(vte->writequeue.max_buffer+((len|16383)+1),vte->writequeue.limit_buffer);
	temp=realloc(vte->writequeue.buffer,max); // this may be a no-op if we were already at the limit
	if (!temp) {
		*isdrop_out=1;
		return 0;
	}
	vte->writequeue.buffer = vte->tofree.writeq = temp;
	vte->writequeue.max_buffer=max;
	if (vte->writequeue.len+len > max) {
		*isdrop_out=1;
		return 0;
	}
}
memcpy(vte->writequeue.buffer+vte->writequeue.len,data,len);
vte->writequeue.len+=len;
if (flush2_vte(vte)) return -1;
*isdrop_out=0;
return 0;
}

int resize_vte(struct vte *vte, unsigned int rows, unsigned int cols) {
vte->config.rows=rows;
if (vte->scrolling.bottom==vte->config.rowsm1) {
	vte->scrolling.bottom=rows-1;
} else {
	if (vte->scrolling.bottom>=rows) vte->scrolling.bottom=rows-1;
}
vte->config.rowsm1=rows-1;
vte->config.columns=cols;
vte->config.columnsm1=cols-1;
if (cols>vte->tabs.tablinemax) {
	unsigned int *temp;
	if (!(temp=realloc(vte->tabs.tabline,cols*sizeof(*temp)))) GOTOERROR;
	(void)growtabstops(temp,vte->tabs.tablinemax,cols);
	vte->tabs.tabline=temp;
	vte->tabs.tablinemax=cols;
}
vte->cur.row=_BADMIN(vte->cur.row,rows-1);
vte->cur.col=_BADMIN(vte->cur.col,cols-1);
return 0;
error:
	return -1;
}
