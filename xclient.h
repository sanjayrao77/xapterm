
/*
 * xclient.h
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

#define _XCLIENT_H

// when inserting script output into vte's input, we wait until this much is
// free in vte's input before we attempt it (to avoid clipping)
// as such, vte needs at least this in its read buffer
#define BUFFSIZE_INSERTION_XCLIENT	512

struct line_xclient {
	uint32_t *backing; // [COLUMNS], the value at last draw
};

struct sbline_xclient {
	uint32_t *backing;
	struct sbline_xclient *next,*previous;
};

struct xclient {
	struct {
		unsigned int xwidth,xheight,xoff,yoff;
		unsigned int cellh,cellw;
		unsigned int cursorheight,cursoryoff,isblink;
		unsigned int columns,rows;
		unsigned int columnsm1,rowsm1;
		unsigned int rowwidth,colheight;
		unsigned int scrollbackcount; // 0 is ok, 1 is not, 2+ is ok
		unsigned int movepixels; // square of number of pixels to be considered mouse motion, hopefully 1mm^2
		unsigned int isappcursor:1;
		unsigned int isautorepeat:1;
		struct {
			int isredraw:1;
			int isremap:1;
			int iscurset:1;
		} changes;
	} config;
	struct {
		struct {
			unsigned int row,col;
		} lastaddchar;
		int ismarked:1;
	} status;
	struct {
		int iscurset:1;
		int ispaused:1;
		unsigned int linesback; // 0=>no lines back
	} scrollback;
#if 0
	struct { // it's necessary to send paste requests to script so script can intercept them for dialogs
		unsigned int max_buffer;
		unsigned char *buffer; // = .tofree.pastebuffer
	} paste;
#endif
	struct surface_xclient {
		struct {
			struct sbline_xclient *first,*last;
			struct sbline_xclient *firstfree;
			struct {
				struct sbline_xclient *first;
			} reverse;
		} scrollback;
		unsigned int numinline; // never decreases, number of uint32s in each backing line (resizing => != config.columns)
		unsigned int maxlines;
		struct line_xclient *lines; // [ROWS]
		struct line_xclient *sparelines; // [ROWS], there is _no_ backing reserved, this is for scrolling .lines
		struct line_xclient *savedlines; // for script to save/restore a screenshot
		uint32_t *spareline; // useful for pyunicode_fromkindanddata
		struct {
#define NONE_MODE_SELECTION_SURFACE_XCLIENT 0
#define RAW_MODE_SELECTION_SURFACE_XCLIENT 1
#define WORD_MODE_SELECTION_SURFACE_XCLIENT 2
#define LINE_MODE_SELECTION_SURFACE_XCLIENT 3
#define ALL_MODE_SELECTION_SURFACE_XCLIENT	4
			int mode; // also number of clicks to start it
			struct click_xclient {
				unsigned int buttonnumber:2;
				unsigned int x,y; // of Press
				Time pressstamp; // of Press
			} lastclick;
			struct halfclick_xclient {
				int ispress:1;
				unsigned int buttonnumber:2;
				unsigned int x,y;
				Time stamp;
			} lasthalfclick;
			struct {
				unsigned int row,col;
			} start;
			struct {
				unsigned int row,col;
			} stop;
			struct {
				unsigned int len,max;
				uint32_t **list;
			} backings;
		} selection;
		struct {
			struct line_xclient *lines;
			struct sbline_xclient *sblines;
			uint32_t *backing;
			unsigned int backcount;
		} tofree;
	} surface;
	XColor xcolors[16];
	uint64_t nextalarm;
	int isnodraw:1;
	int ispaused:1;
	int isquit:1;
	int ismousegrabbed:1;
	struct {
		struct x11info *x;
		struct xftchar *xftchar;
		struct charcache *charcache;
		struct texttap *texttap;
		struct pty *pty;
		struct all_event *events;
		struct vte *vte;
		struct cursor *cursor;
		void *script;
		struct config *config;
		struct xclipboard *xclipboard;
	} baggage;
	struct {
		int (*control_s)(void *,int);
		int (*control_q)(void *,int);
		int (*control)(void *,int);
		int (*key)(void *,int);
		int (*bell)(void *);
		int (*alarmcall)(void *,int);
		char *(*getinsertion)(unsigned int *,void *);
		int (*checkinsertion)(void *);
		int (*message)(void *,char *,unsigned int);
		int (*keysym)(void *,unsigned int, unsigned int);
		int (*unkeysym)(void *,unsigned int, unsigned int);
		int (*onresize)(void *,unsigned int, unsigned int);
		int (*pointer)(void *,unsigned int, unsigned int,unsigned int,unsigned int, unsigned int);
	} hooks;
	struct {
//		unsigned char *pastebuffer;
	} tofree;
};

int init_xclient(struct xclient *xc, struct config *config, struct x11info *x, struct xftchar *xftchar,
		struct charcache *charcache, struct texttap *texttap, struct pty *pty, struct all_event *events, struct vte *vte,
		struct cursor *cursor, struct xclipboard *xclipboard, void *script);
void deinit_xclient(struct xclient *xc);
// int verify_xclient(void);
int mainloop_xclient(struct xclient *xc);
int fixcolors_xclient(struct xclient *xc);
int addchar_xclient(struct xclient *xc, uint32_t value, unsigned int row, unsigned int col);
int visualbell_xclient(struct xclient *xc, unsigned int color, unsigned int ms);
int xbell_xclient(struct xclient *xc, int percent);
int fillpadding_xclient(struct xclient *xc, unsigned int color, unsigned int ms);
int clrscr_xclient(struct xclient *xc, uint32_t fgvaluemask, uint32_t bgvaluemask);
void savebacking_xclient(struct xclient *xc);
int restorebacking_xclient(struct xclient *xc);
int pause_xclient(struct xclient *xc);
int unpause_xclient(struct xclient *xc);
void setalarm_xclient(struct xclient *xc, unsigned int seconds);
int setcursorcolors_xclient(struct xclient *xc, unsigned short r, unsigned short g, unsigned short b);
unsigned int *fetchline_xclient(struct xclient *xc, unsigned int row, unsigned int col, unsigned int count);
int send_xclient(int *isdrop_out, struct xclient *xc, unsigned char *letters, unsigned int len);
int setcursor_xclient(struct xclient *xc, unsigned int row, unsigned int col);
int fillrect_xclient(struct xclient *xc, unsigned int x, unsigned int y,
		unsigned int width, unsigned int height, unsigned int color, unsigned int ms);
int restorerect_xclient(struct xclient *xc, unsigned int x, unsigned int y, unsigned int width, unsigned int height);
int changefont_xclient(struct xclient *xc, char *fontname);
int reconfig_xclient(struct xclient *xc);
int iseventsempty_xclient(struct xclient *xc);
int resizewindow_xclient(struct xclient *xc, unsigned int width, unsigned int height);
int resizecell_xclient(struct xclient *xc, unsigned int cellw, unsigned int cellh);
int resizecursor_xclient(struct xclient *xc, unsigned int cellw, unsigned int cellh, unsigned int cursorheight,
		unsigned int cursoryoff, unsigned int isblink);
int resizesurface_xclient(struct xclient *xc, unsigned int rows, unsigned int cols);
void setparams_xclient(struct xclient *xc, int font0shift, int font0line, int fontulline, int fontullines);
int mark_xclient(struct xclient *xc);
int unmark_xclient(struct xclient *xc);
int scrollback_xclient(struct xclient *xc, int delta);
int copy_xclient(struct xclient *xc, unsigned char *selection, unsigned int selectionlen, unsigned char *text, unsigned int textlen);
int getpaste_xclient(struct xclient *xc, unsigned char *dest, unsigned int destlen);
int paste_xclient(unsigned int *newlen_out, struct xclient *xc, unsigned char *selection, unsigned int selectionlen, int timeout);
void clearselection_xclient(struct xclient *xc);
int isvalid_setselection_xclient(struct xclient *xc, unsigned int row_start, unsigned int col_start,
	unsigned int row_stop, unsigned int col_stop);
int setselection_xclient(struct xclient *xc, int mode, unsigned int row_start, unsigned int col_start,
		unsigned int row_stop, unsigned int col_stop);
int copyselection_xclient(struct xclient *xc, unsigned char *selection, unsigned int selectionlen);
int setpointer_xclient(struct xclient *xc, int code);
int movewindow_xclient(struct xclient *xc, int x, int y);
int drawon_xclient(struct xclient *xc);
int drawoff_xclient(struct xclient *xc);
int cursoronoff_xclient(struct xclient *xc, unsigned int height, unsigned int yoff);
int grabpointer_xclient(struct xclient *xc, int toggle);
