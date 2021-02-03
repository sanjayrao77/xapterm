/*
 * keysym.c - helper functions for X11 keysyms
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
#define DEBUG
#include "common/conventions.h"

#include "keysym.h"

struct uc4_keysym {
	uint32_t keysym,unicode;
};

#include "keysyms0.c"

#if NUM_UCS4_KEYSYM == 0
#error
#endif

uint32_t getuc4_keysym(unsigned int keysym) {
struct uc4_keysym *list;
unsigned int num;
// 0x20 - 0x7f and 0xa0 - 0xff map directly, no keysyms below 0x20
if (!(keysym&~0xff)) {
	if ((keysym > 127) && (keysym < 160)) return 0;
	return keysym;
}
if (keysym&16777216) return keysym&0x10ffff;
list=keysyms;
num=NUM_UCS4_KEYSYM;
while (1) {
	unsigned int h;
	if (num==1) {
		if (list->keysym==keysym) return list->unicode;
		break;
	}
	h=num/2;
	if (keysym < list[h].keysym) {
		num=h;
	} else {
		list=list+h;
		num-=h;
	}
}
return 0;
}
