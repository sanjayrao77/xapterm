
/*
 * charcache.c - cache drawn characters so they don't have to be drawn every time
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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <X11/Xlib.h>
#define DEBUG
#include "common/conventions.h"
#include "common/safemem.h"
#include "x11info.h"

#include "charcache.h"

#define LEFT(a)	((a)->treevars.left)
#define RIGHT(a)	((a)->treevars.right)
#define BALANCE(a)	((a)->treevars.balance)

static inline int cmp(struct one_charcache *a, struct one_charcache *b) {
return _FASTCMP(a->value,b->value);
}

static void rebalanceleftleft(struct one_charcache **root_inout) {
struct one_charcache *a=*root_inout;
struct one_charcache *left=LEFT(a);
LEFT(a)=RIGHT(left);
RIGHT(left)=a;
*root_inout=left;
BALANCE(left)=0;
BALANCE(a)=0;
}

static void rebalancerightright(struct one_charcache **root_inout) {
struct one_charcache *a=*root_inout;
struct one_charcache *right=RIGHT(a);
RIGHT(a)=LEFT(right);
LEFT(right)=a;
*root_inout=right;
BALANCE(right)=0;
BALANCE(a)=0;
}

static void rebalanceleftright(struct one_charcache **root_inout) {
struct one_charcache *a=*root_inout;
struct one_charcache *left=LEFT(a);
struct one_charcache *gchild=RIGHT(left);
int b;
RIGHT(left)=LEFT(gchild);
LEFT(gchild)=left;
LEFT(a)=RIGHT(gchild);
RIGHT(gchild)=a;
*root_inout=gchild;
b=BALANCE(gchild);
if (b>0) {
		BALANCE(a)=-1;
		BALANCE(left)=0;
} else if (!b) {
		BALANCE(a)=BALANCE(left)=0;
} else {
		BALANCE(a)=0;
		BALANCE(left)=1;
}
BALANCE(gchild)=0;
}

static void rebalancerightleft(struct one_charcache **root_inout) {
struct one_charcache *a=*root_inout;
struct one_charcache *right=RIGHT(a);
struct one_charcache *gchild=LEFT(right);
int b;
LEFT(right)=RIGHT(gchild);
RIGHT(gchild)=right;
RIGHT(a)=LEFT(gchild);
LEFT(gchild)=a;
*root_inout=gchild;
b=BALANCE(gchild);
if (b<0) {
		BALANCE(a)=1;
		BALANCE(right)=0;
} else if (!b) {
		BALANCE(a)=BALANCE(right)=0;
} else {
		BALANCE(a)=0;
		BALANCE(right)=-1;
}
BALANCE(gchild)=0;
}

static void rebalanceleftbalance(struct one_charcache **root_inout) {
struct one_charcache *a=*root_inout;
struct one_charcache *left=LEFT(a);
LEFT(a)=RIGHT(left);
RIGHT(left)=a;
*root_inout=left;
BALANCE(left)=-1;
/* redundant */
// BALANCE(a)=1;
}

static void rebalancerightbalance(struct one_charcache **root_inout) {
struct one_charcache *a=*root_inout;
struct one_charcache *right=RIGHT(a);
RIGHT(a)=LEFT(right);
LEFT(right)=a;
*root_inout=right;
BALANCE(right)=1;
/* redundant */
// BALANCE(a)=-1;
}

static int extracthighest(struct one_charcache **root_inout, struct one_charcache **node_out) {
/* returns 1 if depth decreased, else 0 */
struct one_charcache *root=*root_inout;

if (RIGHT(root)) {
	int r=0;
	if (extracthighest(&RIGHT(root),node_out)) {
		int b;
		b=BALANCE(root);
		if (b<0) {
			BALANCE(root)=0; r=1;
		} else if (!b) {
			BALANCE(root)=1;
		} else {
				if (!BALANCE(LEFT(root))) {
					(void)rebalanceleftbalance(root_inout); 
				} else if (BALANCE(LEFT(root))==1) {
					(void)rebalanceleftleft(root_inout); 
					r=1;
				} else {
					(void)rebalanceleftright(root_inout);
					r=1;
				}
		}
	}
	return r;
} else {
	/* remove ourselves */
	*node_out=root;
	*root_inout=LEFT(root);		
	return 1;
}
}

static int extractlowest(struct one_charcache **root_inout, struct one_charcache **node_out) {
/* returns 1 if depth decreased, else 0 */
struct one_charcache *root=*root_inout;

if (LEFT(root)) {
	int r=0;
	if (extractlowest(&LEFT(root),node_out)) {
		int b;
		b=BALANCE(root);
		if (b>0) {
			BALANCE(root)=0; r=1;
		} else if (!b) {
			BALANCE(root)=-1;
		} else {
				if (!BALANCE(RIGHT(root))) {
					(void)rebalancerightbalance(root_inout);
				} else if (BALANCE(RIGHT((root)))==-1) {
					(void)rebalancerightright(root_inout);
					r=1;
				} else {
					(void)rebalancerightleft(root_inout);
					r=1;
				}
		}
	}
	return r;
} else {
	/* remove ourselves */
	*node_out=root;
	*root_inout=RIGHT(root);
	return 1;
}
}


static int rmnode(struct one_charcache **root_inout, struct one_charcache *node) {
/* returns 1 if depth decreased, else 0 */
struct one_charcache *root=*root_inout;
int r=0;
int c;

if (!root) { D2WHEREAMI; return 0; } /* if the node doesn't exist in the tree */

c=cmp(node,root);
if (c<0) {
	if (rmnode(&LEFT(root),node)) {
		int b;
		b=BALANCE(root);
		if (b>0) {
				BALANCE(root)=0;
				r=1;
		} else if (!b) {
				BALANCE(root)=-1;
		} else {
				if (!BALANCE(RIGHT(root))) {
					(void)rebalancerightbalance(root_inout);
				} else if (BALANCE(RIGHT(root))<0) {
					(void)rebalancerightright(root_inout);
					r=1;
				} else {
					(void)rebalancerightleft(root_inout);
					r=1;
				}
		}
	}
} else if (c>0) {
	if (rmnode(&RIGHT(root),node)) {
		int b;
		b=BALANCE(root);
		if (b<0) {
				BALANCE(root)=0;
				r=1;
		} else if (!b) {
				BALANCE(root)=1;
		} else {
				if (!BALANCE(LEFT(root))) {
					(void)rebalanceleftbalance(root_inout);
				} else if (BALANCE(LEFT(root))>0) {
					(void)rebalanceleftleft(root_inout);
					r=1;
				} else {
					(void)rebalanceleftright(root_inout);
					r=1;
				}
		}
	}
} else {
	/* found it */
	struct one_charcache *temp;
	if (BALANCE(root)==1) {
		if (extracthighest(&LEFT(root),&temp)) {
			BALANCE(temp)=0;
			r=1;
		} else {
			BALANCE(temp)=1;
		}
		LEFT(temp)=LEFT(root);
		RIGHT(temp)=RIGHT(root);
		*root_inout=temp;
	} else if (BALANCE(root)==-1) {
		if (extractlowest(&RIGHT(root),&temp)) {
			BALANCE(temp)=0;
			r=1;
		} else {
			BALANCE(temp)=-1;
		}
		LEFT(temp)=LEFT(root);
		RIGHT(temp)=RIGHT(root);
		*root_inout=temp;
	} else { /* balance 0 */
		if (LEFT(root)) {
			if (extracthighest(&LEFT(root),&temp)) {
				BALANCE(temp)=-1;
			} else {
				BALANCE(temp)=0;
			}
			LEFT(temp)=LEFT(root);
			RIGHT(temp)=RIGHT(root);
			*root_inout=temp;
		} else {
			*root_inout=NULL;
			r=1;
		}
	}
}

return r;
}

static int addnode(struct one_charcache **root_inout, struct one_charcache *node) {
/* returns 1 if depth increased, else 0 */
struct one_charcache *root=*root_inout;
int r=0;

if (!root) {
	*root_inout=node;
	return 1;
}

if (cmp((node),(root))<0) {
	if (addnode(&LEFT(root),node)) {
		int b;
		b=BALANCE(root);
		if (!b) {
			BALANCE(root)=1; r=1;
		} else if (b>0) {
				if (BALANCE(LEFT(root))>0) (void)rebalanceleftleft(root_inout); else (void)rebalanceleftright(root_inout);
		} else {
			BALANCE(root)=0;
		}
	}
} else {
	if (addnode(&RIGHT(root),node)) {
		int b;
		b=BALANCE(root);
		if (!b) {
			BALANCE(root)=-1; r=1;
		} else if (b>0) {
			BALANCE(root)=0;
		} else {
				if (BALANCE(RIGHT(root))<0) (void)rebalancerightright(root_inout); else (void)rebalancerightleft(root_inout);
		}
	}
}

return r;
}

static inline struct one_charcache *findnode(struct one_charcache *root, uint32_t value) {
while (root) {
	if (value<root->value) {
		root=LEFT(root);
	} else if (value>root->value) {
		root=RIGHT(root);
	} else {
		return root;
	}
}
return NULL;
}

SICLEARFUNC(one_charcache);
int init_charcache(struct charcache *cc, struct x11info *x, unsigned int count, unsigned int width, unsigned int height) {
struct one_charcache *occ,*list=NULL;
cc->x=x;
if (!(list=MALLOC(count*sizeof(struct one_charcache)))) GOTOERROR;
cc->config.width=width;
cc->config.height=height;
cc->list=list;
occ=list;
while (count) {
	clear_one_charcache(occ);
	if (!(occ->pixmap=XCreatePixmap(x->display,x->window,width,height,x->depth))) GOTOERROR;
	occ->next=cc->freepool.first;
	cc->freepool.first=occ;
	cc->count+=1;
	occ+=1;
	count--;
}
return 0;
error:
	return -1;
}

void deinit_charcache(struct charcache *cc) {
struct x11info *x=cc->x;
struct one_charcache *list,*occ;
unsigned int count;
list=cc->list;
if (!list) return;
count=cc->count;
occ=list;
while (count) {
	(ignore)XFreePixmap(x->display,occ->pixmap);
	count--;
	occ+=1;
}

FREE(list);
}

Pixmap find_charcache(struct charcache *cc, uint32_t value) {
struct one_charcache *occ;
occ=findnode(cc->active.treetop,value);
if (!occ) return 0;
return occ->pixmap;
}

struct one_charcache *add_charcache(struct charcache *cc, uint32_t value) {
struct one_charcache *occ;

occ=cc->freepool.first;
if (occ) {
	cc->freepool.first=occ->next;
} else {
	occ=cc->active.first;
	cc->active.first=occ->next;
	(ignore)rmnode(&cc->active.treetop,occ);
}

occ->value=value;
occ->treevars.balance=0;
occ->treevars.left=occ->treevars.right=NULL;
occ->next=NULL;

(void)addnode(&cc->active.treetop,occ);

if (!cc->active.first) {
	cc->active.first=cc->active.last=occ;
} else {
	cc->active.last->next=occ;
	cc->active.last=occ;
}

return occ;
}

void reset_charcache(struct charcache *cc) {
struct one_charcache *one;

one=cc->active.first;
while (one) {
	struct one_charcache *next;
	next=one->next;
	one->next=cc->freepool.first;
	cc->freepool.first=one;
	one=next;
}

cc->active.last=cc->active.first=cc->active.treetop=NULL;
}

int resize_charcache(struct charcache *cc, unsigned int width, unsigned int height) {
struct x11info *x=cc->x;
struct one_charcache *occ;
unsigned int count;

if ((width<=cc->config.width)&&(height<=cc->config.height)) return 0;
(void)reset_charcache(cc);

count=cc->count;
occ=cc->list;
while (1) {
	(ignore)XFreePixmap(x->display,occ->pixmap);
	if (!(occ->pixmap=XCreatePixmap(x->display,x->window,width,height,x->depth))) GOTOERROR;
	count--;
	if (!count) break;
	occ+=1;
}
cc->config.width=width;
cc->config.height=height;
return 0;
error:
	return -1;
}
