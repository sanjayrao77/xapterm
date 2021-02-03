/*
 * texttap.c - efficient way to detect multiple strings in an input stream
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define DEBUG
#include "conventions.h"
#include "blockmem.h"

#include "texttap.h"

// this is "(struct node_texttap*)a->value.previous-0"
#define previous_node_texttap(a) ((struct node_texttap*)((void *)(a)->value.previous-offsetof(struct node_texttap,value)))
#define node_handle_texttap(a) ((struct node_texttap*)((void *)(a)->value-offsetof(struct node_texttap,value)))


#define LEFT(a)	((a)->samevalue.left)
#define RIGHT(a)	((a)->samevalue.right)
#define BALANCE(a)	((a)->samevalue.balance)

#define DEBUG2

static inline int cmp(struct node_texttap *a, struct node_texttap *b) {
return _FASTCMP(a->value.uint,b->value.uint);
}

static void rebalanceleftleft(struct node_texttap **root_inout) {
struct node_texttap *a=*root_inout;
struct node_texttap *left=LEFT(a);
LEFT(a)=RIGHT(left);
RIGHT(left)=a;
*root_inout=left;
BALANCE(left)=0;
BALANCE(a)=0;
}

static void rebalancerightright(struct node_texttap **root_inout) {
struct node_texttap *a=*root_inout;
struct node_texttap *right=RIGHT(a);
RIGHT(a)=LEFT(right);
LEFT(right)=a;
*root_inout=right;
BALANCE(right)=0;
BALANCE(a)=0;
}

static void rebalanceleftright(struct node_texttap **root_inout) {
struct node_texttap *a=*root_inout;
struct node_texttap *left=LEFT(a);
struct node_texttap *gchild=RIGHT(left);
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

static void rebalancerightleft(struct node_texttap **root_inout) {
struct node_texttap *a=*root_inout;
struct node_texttap *right=RIGHT(a);
struct node_texttap *gchild=LEFT(right);
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

static void rebalanceleftbalance(struct node_texttap **root_inout) {
struct node_texttap *a=*root_inout;
struct node_texttap *left=LEFT(a);
LEFT(a)=RIGHT(left);
RIGHT(left)=a;
*root_inout=left;
BALANCE(left)=-1;
/* redundant */
// BALANCE(a)=1;
}

static void rebalancerightbalance(struct node_texttap **root_inout) {
struct node_texttap *a=*root_inout;
struct node_texttap *right=RIGHT(a);
RIGHT(a)=LEFT(right);
LEFT(right)=a;
*root_inout=right;
BALANCE(right)=1;
/* redundant */
// BALANCE(a)=-1;
}

static int extracthighest(struct node_texttap **root_inout, struct node_texttap **node_out) {
/* returns 1 if depth decreased, else 0 */
struct node_texttap *root=*root_inout;

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

static int extractlowest(struct node_texttap **root_inout, struct node_texttap **node_out) {
/* returns 1 if depth decreased, else 0 */
struct node_texttap *root=*root_inout;

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


static inline int rmnode(struct node_texttap **root_inout, struct node_texttap *node) {
/* returns 1 if depth decreased, else 0 */
struct node_texttap *root=*root_inout;
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
	struct node_texttap *temp;
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

static inline int addnode(struct node_texttap **root_inout, struct node_texttap *node) {
/* returns 1 if depth increased, else 0 */
struct node_texttap *root=*root_inout;
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

static inline struct node_texttap *find_node_texttap(struct node_texttap *root, unsigned int value) {
while (root) {
	if (value<root->value.uint) {
		root=LEFT(root);
	} else if (value>root->value.uint) {
		root=RIGHT(root);
	} else {
		return root;
	}
}
return NULL;
}


static struct final_texttap *alloc_final_texttap(struct texttap *tap) {
struct final_texttap *f;
f=tap->recyclepool.firstfinal;
if (f) {
	tap->recyclepool.firstfinal=f->next;
	return f;
}
if (!(f=ALLOC_blockmem(&tap->tofree.blockmem,struct final_texttap))) GOTOERROR;
return f;
error:
	return NULL;
}

static struct node_texttap *alloc_node_texttap(struct texttap *tap) {
struct node_texttap *n;
n=tap->recyclepool.firstnode;
if (n) {
	tap->recyclepool.firstnode=n->list.next;
	return n;
}
if (!(n=ALLOC_blockmem(&tap->tofree.blockmem,struct node_texttap))) GOTOERROR;
return n;
error:
	return NULL;
}

int init_texttap(struct texttap *texttap) {
texttap->isempty=1;
if (init_blockmem(&texttap->tofree.blockmem,8192)) GOTOERROR;
return 0;
error:
	return -1;
}

void deinit_texttap(struct texttap *texttap) {
deinit_blockmem(&texttap->tofree.blockmem);
}

static int fulltap_runfinals(unsigned int *count_inout, struct final_texttap *first, unsigned int uint) {
struct value_texttap value;
value.previous=NULL;
value.uint=uint;
while (1) {
	int r;
	*count_inout+=1;
	r=first->cb(first->cbparam,&value);
	if (r<0) GOTOERROR; // TODO, remove it
	first=first->next;
	if (!first) break;
}
return 0;
error:
	return -1;
}
static int runfinals(unsigned int *count_inout, struct final_texttap *first, struct value_texttap *value) {
while (1) {
	int r;
	*count_inout+=1;
	r=first->cb(first->cbparam,value);
	if (r<0) GOTOERROR; // TODO, remove it
	first=first->next;
	if (!first) break;
}
return 0;
error:
	return -1;
}

int addchar_texttap(unsigned int *count_out, struct texttap *texttap, unsigned int value) {
struct node_texttap *node,**ppnext;
unsigned int count=0;

if (texttap->fulltap.first) {
	if (fulltap_runfinals(&count,texttap->fulltap.first,value)) GOTOERROR;
}

node=texttap->active.first;
ppnext=&texttap->active.first;
while (node) {
	struct node_texttap *next,*fn;

	next=node->list.next;
	fn=find_node_texttap(node->nextvalue.treetop,value);
	if (!fn) *ppnext=next;
	else {
		if (fn->finals.first) {
			if (runfinals(&count,fn->finals.first,&fn->value)) GOTOERROR;
			if (!fn->nextvalue.treetop) *ppnext=next;
			else {
				*ppnext=fn;
				fn->list.next=next;
				ppnext=&fn->list.next;
			}
		} else {
			*ppnext=fn;
			fn->list.next=next;
			ppnext=&fn->list.next;
		}
	}
	node=next;
}
{
	struct node_texttap *fn;
	fn=find_node_texttap(texttap->treetop,value);
	if (fn) {
		if (fn->finals.first) {
			if (runfinals(&count,fn->finals.first,&fn->value)) GOTOERROR;
			if (fn->nextvalue.treetop) { fn->list.next=texttap->active.first; texttap->active.first=fn; }
		} else {
			fn->list.next=texttap->active.first; texttap->active.first=fn;
		}
	}
}
*count_out=count;
return 0;
error:
	return -1;
}

int isnocbadd_texttap(struct texttap *texttap, unsigned int value) {
// returns 1 if there would be a callback
struct node_texttap *node,*fn,**ppnext;

// fprintf(stderr,"Adding %u\n",value);

if (texttap->fulltap.first) return 0;

node=texttap->active.first;
while (node) {
	fn=find_node_texttap(node->nextvalue.treetop,value);
	if ((fn) && (fn->finals.first)) return 0;
	node=node->list.next;
}
fn=find_node_texttap(texttap->treetop,value);
if ((fn) && (fn->finals.first)) return 0;

// fprintf(stderr,"No cb on %u\n",value);

// this is addchar_, stripped of cases we just checked
node=texttap->active.first;
ppnext=&texttap->active.first;
while (node) {
	struct node_texttap *next;
	next=node->list.next;
	fn=find_node_texttap(node->nextvalue.treetop,value);
	if (!fn) *ppnext=next;
	else {
		*ppnext=fn;
		fn->list.next=next;
		ppnext=&fn->list.next;
	}
	node=next;
}
{
	fn=find_node_texttap(texttap->treetop,value);
	if (fn) {
		fn->list.next=texttap->active.first; texttap->active.first=fn;
	}
}

return 1;
}

int addstring_texttap(unsigned int *count_out, struct texttap *texttap, char *str) {
unsigned int count=0;
while (*str) {
	unsigned int c;
	if (addchar_texttap(&c,texttap,(unsigned int)*str)) GOTOERROR;
	count+=c;
	str++;
}
*count_out=count;
return 0;
error:
	return -1;
}

SICLEARFUNC(node_texttap);
static struct node_texttap *findoradd_node_texttap(struct node_texttap **treetop_inout, struct texttap *tap, unsigned int value) {
struct node_texttap *n;

n=find_node_texttap(*treetop_inout,value);
if (n) return n;
n=alloc_node_texttap(tap);
if (!n) GOTOERROR;
clear_node_texttap(n);
n->value.uint=value;
(ignore)addnode(treetop_inout,n);
return n;
error:
	return NULL;
}

static void recycle_final_texttap(struct texttap *tap, struct final_texttap *final) {
final->next=tap->recyclepool.firstfinal;
tap->recyclepool.firstfinal=final;
}
static void recycle_node_texttap(struct texttap *tap, struct node_texttap *node) {
node->list.next=tap->recyclepool.firstnode;
tap->recyclepool.firstnode=node;
}

struct handle_texttap *str_look_texttap(struct texttap *tap, struct handle_texttap *handle, char *str,
		int (*cb)(void *,struct value_texttap *), void *param) {
// assumes str has length >0
struct final_texttap *final;
struct node_texttap **treetop;
struct value_texttap *previous;

fprintf(stderr,"%s: %s\n",__FUNCTION__,str);

if (!(final=alloc_final_texttap(tap))) GOTOERROR;
final->cbparam=param;
final->cb=cb;

handle->final=final;

treetop=&tap->treetop;
previous=NULL;
while (1) {
	struct node_texttap *fn;
	fn=findoradd_node_texttap(treetop,tap,(unsigned int)*str);
	if (!fn) {
		(void)recycle_final_texttap(tap,final);
		GOTOERROR;
	}
	fn->value.previous=previous;
	str++;
	if (!*str) {
		final->next=fn->finals.first;
		fn->finals.first=final;
		handle->value=&fn->value;
		break;
	}
	treetop=&fn->nextvalue.treetop;
	previous=&fn->value;
}
return handle;
error:
	return NULL;
}

struct handle_texttap *look_texttap(struct texttap *tap, struct handle_texttap *handle, unsigned int *values,
		int (*cb)(void *,struct value_texttap *), void *param) {
// !length => full tap
struct final_texttap *final;

if (!(final=alloc_final_texttap(tap))) GOTOERROR;
final->cbparam=param;
final->cb=cb;

handle->final=final;
if ((!values)||(!*values)) {
	final->next=tap->fulltap.first;
	tap->fulltap.first=final;
	handle->value=NULL;
} else {
	struct node_texttap **treetop;
	struct value_texttap *previous;
	treetop=&tap->treetop;
	previous=NULL;
	while (1) {
		struct node_texttap *fn;
		fn=findoradd_node_texttap(treetop,tap,*values);
		if (!fn) {
			(void)recycle_final_texttap(tap,final);
			GOTOERROR;
		}
		fn->value.previous=previous;
		values++;
		if (!*values) {
			final->next=fn->finals.first;
			fn->finals.first=final;
			handle->value=&fn->value;
			break;
		}
		treetop=&fn->nextvalue.treetop;
		previous=&fn->value;
	}
}
tap->isempty=0;
return handle;
error:
	return NULL;
}

static void removefromactive(struct texttap *tap, struct node_texttap *node) {
struct node_texttap **ppnext,*cur;
ppnext=&tap->active.first;
cur=*ppnext;
while (cur) {
	if (cur==node) {
		*ppnext=node->list.next;
		break;
	}
	ppnext=&cur->list.next;
	cur=*ppnext;
}
}

#if 0
// this was replaced by .previous
static struct node_texttap *str_checkforempty(struct texttap *tap, char *str, unsigned int len_in) {
struct node_texttap **treetop;
struct node_texttap *fn;
unsigned int len=len_in;
treetop=&tap->treetop;
while (1) {
	fn=find_node_texttap(*treetop,(unsigned int)*str);
#ifdef DEBUG2
	if (!fn) { WHEREAMI; _exit(0); } // shouldn't happen
#endif
	len--;
	if (!len) break;
	treetop=&fn->nextvalue.treetop;
	str++;
}
if ((!fn->finals.first) && (!fn->nextvalue.treetop)) {
	(void)removefromactive(tap,fn);
	(ignore)rmnode(treetop,fn);
	(void)recycle_node_texttap(tap,fn);
	return fn;
}
return NULL;
}
static struct node_texttap *checkforempty(struct texttap *tap, unsigned int *values, unsigned int len_in) {
struct node_texttap **treetop;
struct node_texttap *fn;
unsigned int len=len_in;
treetop=&tap->treetop;
while (1) {
	fn=find_node_texttap(*treetop,*values);
#ifdef DEBUG2
	if (!fn) { WHEREAMI; _exit(0); } // shouldn't happen
#endif
	len--;
	if (!len) break;
	treetop=&fn->nextvalue.treetop;
	values++;
}
if ((!fn->finals.first) && (!fn->nextvalue.treetop)) {
	(void)removefromactive(tap,fn);
	(ignore)rmnode(treetop,fn);
	(void)recycle_node_texttap(tap,fn);
	return fn;
}
return NULL;
}
#endif

static struct final_texttap *removefinal(struct node_texttap *node, struct final_texttap *final) {
struct final_texttap **ppnext,*cur;
ppnext=&node->finals.first;
cur=*ppnext;
while (cur) {
	if (cur==final) {
		*ppnext=cur->next;
		return cur;
	}

	ppnext=&cur->next;
	cur=*ppnext;
}
return NULL;
}
static struct final_texttap *removefinal2(struct node_texttap *node, int (*cb)(void *,struct value_texttap *), void *param) {
struct final_texttap **ppnext,*cur;
ppnext=&node->finals.first;
cur=*ppnext;
while (cur) {
	if ((cur->cb==cb)&&(cur->cbparam==param)) {
		*ppnext=cur->next;
		return cur;
	}

	ppnext=&cur->next;
	cur=*ppnext;
}
return NULL;
}

static void print_node_texttap(struct node_texttap *node, int depth) {
depth++;
if (depth>10) return;
if (node->samevalue.left) print_node_texttap(node->samevalue.left,depth);
fprintf(stderr,"struct node_texttap {\n"\
" struct value_texttap {\n"\
"		unsigned int uint:%u\n"\
"		struct value_texttap *previous:%p\n"\
" } value;\n"\
"	struct {\n"\
"		struct node_texttap *next:%p\n"\
"	} list;\n"\
"	struct {\n"\
"		struct node_texttap *treetop:%p\n"\
"	} nextvalue;\n"\
"	struct {\n"\
"		struct node_texttap *left:%p,*right:%p;\n"\
"		signed char balance: %d\n"\
"	} samevalue;\n"\
"	struct {\n"\
"		struct final_texttap *first:%p\n"\
"	} finals;\n"\
"} %p;\n",
node->value.uint,node->value.previous,node->list.next,node->nextvalue.treetop,
node->samevalue.left, node->samevalue.right, node->samevalue.balance,
node->finals.first,node);
if (node->nextvalue.treetop) {
	print_node_texttap(node->nextvalue.treetop,depth);
}
if (node->samevalue.right) print_node_texttap(node->samevalue.right,depth);
}

void print_texttap(struct texttap *tap) {
fprintf(stderr,"struct texttap {\n"\
"	struct {\n"\
"		struct node_texttap *first:%p\n"\
"	} active;\n"\
"	struct node_texttap *treetop:%p\n"\
"	struct {\n"\
"		struct node_texttap *firstnode:%p\n"\
"		struct final_texttap *firstfinal:%p\n"\
"	} recyclepool;\n"\
"} %p;\n",
	tap->active.first,
	tap->treetop,
	tap->recyclepool.firstnode,
	tap->recyclepool.firstfinal,
	tap);
if (tap->treetop) print_node_texttap(tap->treetop,0);
}

static inline void checkforemptynodes(struct texttap *tap, struct node_texttap *node) {
while (1) {
	struct node_texttap *previous;
	if (node->finals.first) break;
	if (node->nextvalue.treetop) break;
	previous=previous_node_texttap(node); // just node->value.previous, recast
	
	(void)removefromactive(tap,node); // previous nodes can also be active if the search repeats, e.g. "aaaa"
	if (previous) (ignore)rmnode(&previous->nextvalue.treetop,node);
	else (ignore)rmnode(&tap->treetop,node);
	(void)recycle_node_texttap(tap,node);
	if (!previous) break;
	node=previous;
}
}

static inline void checkforempty(struct texttap *tap) {
if (tap->treetop) return;
if (tap->fulltap.first) return;
tap->isempty=1;
}

void str_remove_texttap(int *isfound_out, struct texttap *tap, char *str, int (*cb)(void *,struct value_texttap *), void *param) {
struct node_texttap **treetop;
unsigned char *cur=(unsigned char *)str;
int isfound=0;
unsigned int len=0;

treetop=&tap->treetop;
while (1) {
	struct node_texttap *fn;

	fn=find_node_texttap(*treetop,(unsigned int)*cur);
	if (!fn) goto done;
	cur++;
	if (!*cur) {
		struct final_texttap *final;
		final=removefinal2(fn,cb,param);
		if (!final) goto done;
		(void)recycle_final_texttap(tap,final);
		(void)checkforemptynodes(tap,fn);
		isfound=1;
		break;
	}	
	len++;
	treetop=&fn->nextvalue.treetop;
}
// while (len) { if (!str_checkforempty(tap,str,len)) break; len--; } // replaced by .previous
(void)checkforempty(tap);
done:
if (isfound_out) *isfound_out=isfound;
}

void remove_texttap(int *isfound_out, struct texttap *tap, uint32_t *values, int (*cb)(void *,struct value_texttap *), void *param) {
struct node_texttap **treetop;
uint32_t *cur=values;
int isfound=0;
unsigned int len=0;

treetop=&tap->treetop;
while (1) {
	struct node_texttap *fn;

	fn=find_node_texttap(*treetop,(unsigned int)*cur);
	if (!fn) goto done;
	cur++;
	if (!*cur) {
		struct final_texttap *final;
		final=removefinal2(fn,cb,param);
		if (!final) goto done;
		(void)recycle_final_texttap(tap,final);
		(void)checkforemptynodes(tap,fn);
		isfound=1;
		break;
	}	
	len++;
	treetop=&fn->nextvalue.treetop;
}
// while (len) { if (!checkforempty(tap,values,len)) break; len--; } // replaced by .previous
(void)checkforempty(tap);
done:
if (isfound_out) *isfound_out=isfound;
}

void handle_remove_texttap(int *isfound_out, struct texttap *tap, struct handle_texttap *h) {
struct node_texttap *fn;
struct final_texttap *final;
fn=node_handle_texttap(h);
final=removefinal(fn,h->final);
if (!final) {
	*isfound_out=0;
	return;
}
(void)recycle_final_texttap(tap,final);
(void)checkforemptynodes(tap,fn);
(void)checkforempty(tap);
*isfound_out=1;
}
