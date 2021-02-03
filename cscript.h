/*
 * cscript.h
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
void *new_cscript(struct config *config);
void free_cscript(void *v);
int onsuspend_cscript(void *v, int ign);
int onresume_cscript(void *v, int ign);
int onkeysym_cscript(void *v, unsigned int keysym, unsigned int modifiers);
int oninitend_cscript(void *v);
void addxclient_cscript(void *v, struct xclient *xclient);
int onmessage_cscript(void *v, char *str, unsigned int len);
