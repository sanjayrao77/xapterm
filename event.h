/*
 * event.h
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
#define ADDCHAR_TYPE_EVENT		1
#define ERASEINLINE_TYPE_EVENT	2
#define SETCURSOR_TYPE_EVENT	3
#define SCROLL1UP_TYPE_EVENT		4
#define SCROLLUP_TYPE_EVENT		5
#define SCROLL1DOWN_TYPE_EVENT	6
#define SCROLLDOWN_TYPE_EVENT	7
#define DCH_TYPE_EVENT				8
#define TITLE_TYPE_EVENT			9
#define GENERIC_TYPE_EVENT		10
#define SMESSAGE_TYPE_EVENT		11
#define MESSAGE_TYPE_EVENT		12
#define BELL_TYPE_EVENT				13
#define ICH_TYPE_EVENT				14
#define REVERSE_TYPE_EVENT		15
#define APPCURSOR_TYPE_EVENT	16
#define AUTOREPEAT_TYPE_EVENT	17
#define TAP_TYPE_EVENT				18
#define RESET_TYPE_EVENT			19
#if 0
#define INSERTLINE_TYPE_EVENT	11
#define DELETELINE_TYPE_EVENT	12
#endif

struct one_event {
	int type;
	union {
		struct {
			uint32_t value;
			unsigned int row,col;
		} addchar;
		struct {
			uint32_t value;
			unsigned int row,col;
			unsigned short colcount;
			unsigned short rowcount;
		} eraseinline;
		struct {
			unsigned int row,col;
		} setcursor;
		struct {
			unsigned int toprow,bottomrow;
			uint32_t erasevalue;
		} scroll1up,scroll1down;
		struct {
			unsigned int toprow,bottomrow;
			uint32_t erasevalue;
			unsigned int count;
		} scrollup,scrolldown;
#if 0
		struct {
			unsigned int row;
			uint32_t erasevalue;
			unsigned int count;
		} insertline,deleteline;
#endif
		struct {
			uint32_t erasevalue;
			unsigned int row,col;
			unsigned int count;
		} dch;
		struct {
			uint32_t erasevalue;
			unsigned int row,col;
			unsigned int count;
		} ich;
		struct {
			char *data;
			unsigned int len;
		} message;
		struct {
			char str[16];
		} smessage;
		struct {
			char str[16]; // used for DSR, etc.
		} generic;
		struct {
			char *name;
		} title;
		struct {
		} bell;
		struct {
			unsigned int value;
		} tap;
		struct {
		} reverse;
		struct {
			unsigned int isset;
		} appcursor,autorepeat;
	};
	struct one_event *next;
};


struct all_event {
	struct one_event *first,*last;
	struct {
		unsigned int count;
		struct one_event *first;
	} unused;
	struct one_event *events;
};

int init_all_event(struct all_event *all, int num);
void deinit_all_event(struct all_event *all);
void recycle_event(struct all_event *all, struct one_event *e);
void addchar_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int col);
void eraseinline_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int rowcount,
		unsigned int col, unsigned int colcount);
void insertline_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int bottom, unsigned int count);
void deleteline_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int bottom, unsigned int count);
void scrollup_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow, unsigned int count);
void scrolldown_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow, unsigned int count);
void generic_event(struct all_event *all, char *str);
void dch_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int col, unsigned int count);
void title_event(struct all_event *all, char *name);
void setcursor_event(struct all_event *all, unsigned int row, unsigned int col);
void bell_event(struct all_event *all);
void ich_event(struct all_event *all, uint32_t value, unsigned int row, unsigned int col, unsigned int count);
void message_event(struct all_event *all, char *data, unsigned int len);
void tap_event(struct all_event *all, unsigned int value);
void reverse_event(struct all_event *all);
void scroll1up_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow);
void scroll1down_event(struct all_event *all, uint32_t value, unsigned int toprow, unsigned int bottomrow);
void smessage_event(struct all_event *all, char *str);
void smessage2_event(struct all_event *all, unsigned char *data, unsigned int len);
void appcursor_event(struct all_event *all, unsigned int isset);
void autorepeat_event(struct all_event *all, unsigned int isset);
void reset_event(struct all_event *all);
