/*
 * script.h
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
struct script {
	unsigned char opaque;
};

#if 0
void clear_script(struct script *script_in);
int init_script(struct script *script_in, struct config *config, char *pyname, int isnobytecode, int args, char **argv);
void deinit_script(struct script *script);
#endif
struct script *new_script(struct x11info *xi, struct config *config, struct texttap *texttap, char *pyname, unsigned int isnobytecode,
		int isstderr, int argc, char **argv);
void free_script(struct script *script);
#ifdef _XCLIENT_H
void addxclient_script(struct script *script_in, struct xclient *xclient);
#endif
int shutdown_script(struct script *script);
int oninitend_script(struct script *script_in);
int onsuspend_script(void *script_in, int ign);
int onresume_script(void *script_in, int ign);
int oncontrolkey_script(void *script_in, int key);
int onbell_script(void *script_in);
int onalarm_script(void *script_in, int t32);
int onkey_script(void *script_in, int key);
char *getinsertion_script(unsigned int *len_out, void *script_in);
int checkinsertion_script(void *script_in);
int onmessage_script(void *script_in, char *str, unsigned int len);
int onkeysym_script(void *script_in, unsigned int keysym, unsigned int modifiers);
int onresize_script(void *script_in, unsigned int width, unsigned int height);
int onpointer_script(void *script_in, unsigned int type, unsigned int mods, unsigned int button, unsigned int row, unsigned int col);
int onkeysymrelease_script(void *script_in, unsigned int keysym, unsigned int modifiers);
