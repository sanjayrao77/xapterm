/*
 * charcache.h
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

struct one_charcache {
	uint32_t value;
	struct {
		signed char balance;
		struct one_charcache *left,*right;
	} treevars;
	struct one_charcache *next;

	Pixmap pixmap;
};

struct charcache {
	struct x11info *x;
	struct {
		unsigned int width,height;
	} config;
	struct {
		struct one_charcache *treetop;
		struct one_charcache *first,*last;
	} active;
	struct {
		struct one_charcache *first;
	} freepool;
	unsigned int count;
	struct one_charcache *list;
};

int init_charcache(struct charcache *cc, struct x11info *x, unsigned int count, unsigned int width, unsigned int height);
void deinit_charcache(struct charcache *cc);
Pixmap find_charcache(struct charcache *cc, uint32_t value);
struct one_charcache *add_charcache(struct charcache *cc, uint32_t value);
void reset_charcache(struct charcache *cc);
int resize_charcache(struct charcache *cc, unsigned int width, unsigned int height);
