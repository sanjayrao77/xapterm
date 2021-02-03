/*
 * vte.h
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
struct rgb_vte {
	unsigned int index;
	unsigned char r,g,b;
	uint32_t fgvaluemask,bgvaluemask;
};
#define FGCOLOR_VTE	15
#define BGCOLOR_VTE	0
#define CURSORCOLOR_VTE	9

struct vte {
	struct {
		int is8859:1;
		int isshowcontrol:1;
		int isinsertmode:1;
		int isautowrap:1;
		int isautorepeat:1;
		unsigned int rows,columns;
		unsigned int rowsm1,columnsm1;
	} config;
	struct rgb_vte colors[16];
	struct rgb_vte *curfgcolor,*curbgcolor;
	struct {
		unsigned int *tabline;
		unsigned int tablinemax;
	} tabs;
	struct {
		unsigned int istopleft:1;
		unsigned int top,bottom;
	} scrolling;
	struct {
		unsigned char *buffer;
		unsigned int max_buffer;
		unsigned char *q;
		unsigned int qlen; // !0 => vte is waiting on .waitline to be drawn
	} readqueue;
	struct {
// we want it to be large enough to take a dsr and anything a script will send
		unsigned int max_buffer,limit_buffer;
		int fd;
		unsigned char *buffer;
		unsigned int len;
	} writequeue;
	struct {
		unsigned int isovercol:1; // next char should LF
		unsigned int row,col;
	} cur;
	struct {
#define TEXT_MODE_INPUT_VTE	0
#define UTF8_MODE_INPUT_VTE	1
#define ESCAPE_MODE_INPUT_VTE	2
#define REPEAT_MODE_INPUT_VTE	3
#define MESSAGE_MODE_INPUT_VTE 4
#define PALETTE_MODE_INPUT_VTE 5
		unsigned int mode;
		struct {
			int isdone:1;
			unsigned int len:3,bytesleft:3;
			unsigned char four[4],*cur;
		} utf8;
		struct {
			int isoverrun:1;
			unsigned int len;
			unsigned int max_buffer;
			unsigned char *buffer,*cur;
			unsigned int lastmode; // where escape came from
		} escape;
		struct {
			unsigned int fuse;
			uint32_t value;
		} repeat;
		struct {
			int isoverrun:1;
			int isosc:1; // BEL terminates (as well as ST)
			unsigned int len;
			unsigned int max_buffer,max_bufferm1;
			unsigned char *buffer,*cur;
		} message;
		struct {
			int len;
			unsigned char buffer[10]; // 0-term by effect
		} palette;
	} input;
	struct {
		int isbright:1;
		unsigned int isreverse:1;
		unsigned int issuperreverse:1;
		unsigned int isinvisible:1;
		unsigned int underlinemask;
		unsigned int fgindex,bgindex;
	} sgr;
	struct {
		int isbright:1;
		unsigned int isreverse:1;
		unsigned int issuperreverse:1;
		unsigned int isovercol:1;
		unsigned int isinvisible:1;
		unsigned int underlinemask;
		unsigned int row,col;
		unsigned int fgindex,bgindex;
		unsigned int scrolltop,scrollbottom;
	} currentstate;
	struct {
		int iskeypadappmode:1;
		int iscursorappmode:1;
	} keyboardstates; // TODO move this this config
	struct {
		unsigned char *buffer;
		unsigned char *writeq;
	} tofree;
	struct {
		struct pty *pty;
		struct all_event *events;
		struct texttap *texttap;
	} baggage;
};

int init_vte(struct vte *vte, struct config *config, struct pty *pty, struct all_event *events, struct texttap *texttap,
		unsigned int inputbuffersize, unsigned int messagebuffersize, unsigned int writebuffermax);
void deinit_vte(struct vte *vte);
void setcolors_vte(struct vte *vte, struct colors_config *colors);
int processreadqueue_vte(struct vte *v);
int writeorqueue_vte(int *isdrop_out, struct vte *vte, unsigned char *letters, unsigned int len);
int fillreadqueue_vte(struct vte *vte);
int flush_vte(struct vte *vte);
void setcolor_vte(struct vte *vte, unsigned int index, unsigned char r, unsigned char g, unsigned char b);
int insert_readqueue_vte(struct vte *vte, char *str, unsigned int len);
int resize_vte(struct vte *vte, unsigned int rows, unsigned int cols);
