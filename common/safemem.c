/*
 * savemem.c - drop-in for malloc to do memory auditing
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
#define DEBUG
#define DEBUG2
#include "conventions.h"

#include "safemem.h"

struct safemem {
	void *start,*nextstart;
	char *file_caller;
	unsigned int line_caller;

	struct safemem *left,*right;
	signed char balance;
};

static struct safemem *topnode_global;

#define LEFT(a)	((a)->left)
#define RIGHT(a)	((a)->right)
#define BALANCE(a)	((a)->balance)

static inline int cmp(struct safemem *a, struct safemem *b) {
return _FASTCMP(a->start,b->start);
}

static void rebalanceleftleft(struct safemem **root_inout) {
struct safemem *a=*root_inout;
struct safemem *left=LEFT(a);
LEFT(a)=RIGHT(left);
RIGHT(left)=a;
*root_inout=left;
BALANCE(left)=0;
BALANCE(a)=0;
}

static void rebalancerightright(struct safemem **root_inout) {
struct safemem *a=*root_inout;
struct safemem *right=RIGHT(a);
RIGHT(a)=LEFT(right);
LEFT(right)=a;
*root_inout=right;
BALANCE(right)=0;
BALANCE(a)=0;
}

static void rebalanceleftright(struct safemem **root_inout) {
struct safemem *a=*root_inout;
struct safemem *left=LEFT(a);
struct safemem *gchild=RIGHT(left);
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

static void rebalancerightleft(struct safemem **root_inout) {
struct safemem *a=*root_inout;
struct safemem *right=RIGHT(a);
struct safemem *gchild=LEFT(right);
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

static void rebalanceleftbalance(struct safemem **root_inout) {
struct safemem *a=*root_inout;
struct safemem *left=LEFT(a);
LEFT(a)=RIGHT(left);
RIGHT(left)=a;
*root_inout=left;
BALANCE(left)=-1;
/* redundant */
// BALANCE(a)=1;
}

static void rebalancerightbalance(struct safemem **root_inout) {
struct safemem *a=*root_inout;
struct safemem *right=RIGHT(a);
RIGHT(a)=LEFT(right);
LEFT(right)=a;
*root_inout=right;
BALANCE(right)=1;
/* redundant */
// BALANCE(a)=-1;
}

static int extracthighest(struct safemem **root_inout, struct safemem **node_out) {
/* returns 1 if depth decreased, else 0 */
struct safemem *root=*root_inout;

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

static int extractlowest(struct safemem **root_inout, struct safemem **node_out) {
/* returns 1 if depth decreased, else 0 */
struct safemem *root=*root_inout;

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


static int rmnode(struct safemem **root_inout, struct safemem *node) {
/* returns 1 if depth decreased, else 0 */
struct safemem *root=*root_inout;
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
	struct safemem *temp;
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

static int addnode(struct safemem **root_inout, struct safemem *node) {
/* returns 1 if depth increased, else 0 */
struct safemem *root=*root_inout;
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

static struct safemem *findnode(struct safemem *root, void *ptr) {
while (root) {
	if (ptr<root->start) {
		root=LEFT(root);
	} else if (ptr>root->nextstart) {
		root=RIGHT(root);
	} else {
		return root;
	}
}
return NULL;
}

void *malloc_safemem(uint64_t size, char *filename, unsigned int line) {
struct safemem *ptr;
#ifdef DEBUG2
fprintf(stderr,"%s:%d allocating %"PRIu64"\n",filename,line,size);
#endif
ptr=malloc(size+sizeof(struct safemem));
if (!ptr) return NULL;
ptr->start=ptr+1;
ptr->nextstart=ptr->start+size;
ptr->file_caller=filename;
ptr->line_caller=line;
ptr->left=ptr->right=0;
ptr->balance=0;
(ignore)addnode(&topnode_global,ptr);
return (void *)(ptr+1);
}
void free_safemem(void *ptr_in, char *filename, unsigned int line) {
struct safemem *ptr,*f;
ptr=ptr_in;
ptr--;
f=findnode(topnode_global,ptr_in);
if (!f) {
	fprintf(stderr,"%s:%u Unknown free: %p\n",filename,line,ptr_in);
	return;
}
if (f->start!=ptr_in) {
	fprintf(stderr,"%s:%u Invalid free: %p\n",filename,line,ptr_in);
	return;
}
(void)rmnode(&topnode_global,f);
free(ptr);
}
void *realloc_safemem(void *ptr_in, uint64_t size, char *filename, unsigned int line) {
if (ptr_in) {
	free_safemem(ptr_in,filename,line);
}
return malloc_safemem(size,filename,line);
}
void test_safemem(void *ptr_in, uint64_t len, char *filename, unsigned int line) {
struct safemem *f;
f=findnode(topnode_global,ptr_in);
if (!f) {
	fprintf(stderr,"%s:%u Outside ptr: %p\n",filename,line,ptr_in);
	_exit(0);
}
if (f->nextstart<ptr_in+len) {
	fprintf(stderr,"%s:%u Overflow ptr: %p, %p is %"PRIu64"\n",filename,line,ptr_in,f,(uint64_t)(f->nextstart-f->start));
	_exit(0);
}
}

static void printout2(uint64_t *count_inout, struct safemem *p, FILE *fout) {
if (p->left) printout2(count_inout,p->left,fout);
*count_inout+=(uint64_t)(p->nextstart-p->start);
fprintf(fout,"%p: %"PRIu64"\n",p,(uint64_t)(p->nextstart-p->start));
if (p->right) printout2(count_inout,p->right,fout);
}

void printout_safemem(FILE *fout, char *filename, unsigned int line) {
uint64_t count=0;
fprintf(stderr,"%s:%u memory printout:\n",filename,line);
if (topnode_global) printout2(&count,topnode_global,fout);
fprintf(stderr,"%s:%u memory (%"PRIu64" bytes):\n",filename,line,count);
}
