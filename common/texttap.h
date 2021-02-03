
/*
 * texttap.h
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

struct node_texttap {
	struct value_texttap {
		struct value_texttap *previous; // this can be cast to node_texttap
		unsigned int uint;
	} value; // keep this first so .previous cast works

	struct {
		struct node_texttap *next;
	} list; // for active and recycle
	struct {
			struct node_texttap *treetop;
	} nextvalue;

	struct {
		struct node_texttap *left,*right;
		signed char balance;
	} samevalue;

	struct {
		struct final_texttap *first;
	} finals;
};

struct final_texttap {
	void *cbparam;
	int (*cb)(void *, struct value_texttap *);

	struct final_texttap *next;
};

struct handle_texttap {
	struct value_texttap *value;
	struct final_texttap *final;
};


struct texttap {
	int isempty;
	struct {
		struct node_texttap *first;
	} active;
	struct {
		struct final_texttap *first;
	} fulltap;
	struct node_texttap *treetop;
	struct {
		struct node_texttap *firstnode;
		struct final_texttap *firstfinal;
	} recyclepool;
	struct {
		struct blockmem blockmem;
	} tofree;
};


int init_texttap(struct texttap *texttap);
void deinit_texttap(struct texttap *texttap);
int addchar_texttap(unsigned int *count_out, struct texttap *texttap, unsigned int value);
int addstring_texttap(unsigned int *count_out, struct texttap *texttap, char *str);
struct handle_texttap *str_look_texttap(struct texttap *tap, struct handle_texttap *handle, char *str,
		int (*cb)(void *,struct value_texttap *), void *param);
struct handle_texttap *look_texttap(struct texttap *tap, struct handle_texttap *handle, unsigned int *values,
		int (*cb)(void *,struct value_texttap *), void *param);
void str_remove_texttap(int *isfound_out, struct texttap *tap, char *str, int (*cb)(void *,struct value_texttap *), void *param);
void remove_texttap(int *isfound_out, struct texttap *tap, uint32_t *values, int (*cb)(void *,struct value_texttap *), void *param);
void print_texttap(struct texttap *tap);
void handle_remove_texttap(int *isfound_out, struct texttap *tap, struct handle_texttap *h);
int isnocbadd_texttap(struct texttap *texttap, unsigned int value);
