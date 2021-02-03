/*
 * main.c - setup modules and basic command line
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pty.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#define DEBUG
#include "common/conventions.h"
#include "common/safemem.h"
#include "common/blockmem.h"
#include "common/texttap.h"
#include "config.h"
#include "x11info.h"
#include "xftchar.h"
#include "charcache.h"
#include "pty.h"
#include "event.h"
#include "vte.h"
#include "cursor.h"
#include "xclipboard.h"
#include "xclient.h"
#include "script.h"
#include "cscript.h"

#define MAX_SCRIPT_CMDLINE	63
struct cmdline {
	char script[MAX_SCRIPT_CMDLINE+1];
	unsigned int isnobytecode:1;
	unsigned int isnopython:1;
	unsigned int isstderr:1;
};

static int parsecmdlineB(int *isdone_inout, struct cmdline *cmdline, char *arg) {
if (arg[0]=='-') {
	if (!strcmp(arg,"-nobc")) cmdline->isnobytecode=1;
	else if (!strcmp(arg,"-nopy")) cmdline->isnopython=1;
	else if (!strcmp(arg,"-stderr")) cmdline->isstderr=1;
	else if (!strcmp(arg,"-h")) {
		if (cmdline->isnopython) {
			fprintf(stdout,"Usage: xapterm [-nobc] [-nopy] [-stderr] [-h] [scriptname] [python args...]\n"\
						"-nobc   : disable python's __pycache__ litter\n"\
						"-nopy   : disable python support to save some memory\n"\
						"-stderr : send python's output to caller instead of terminal\n"\
						"-h      : this help, of sorts\n"\
						"scriptname  : filename containing python code for terminal\n"\
						"python args : one or more arguments to send to OnInitBegin() in script\n");
			_exit(0);
		}
		*isdone_inout=1;
	} else *isdone_inout=1;
} else {
	int l;
	l=strlen(arg);
	if (l>MAX_SCRIPT_CMDLINE) GOTOERROR;
	memcpy(cmdline->script,arg,l+1);
	*isdone_inout=2;
}
return 0;
error:
	return -1;
}

static int parsecmdline(int *argc_out, char ***argv_out, struct cmdline *cmdline, int argc, char **argv) {
int i;
int isdone=0;
for (i=1;i<argc;i++) {
	if (parsecmdlineB(&isdone,cmdline,argv[i])) GOTOERROR;
	if (isdone) { i+=isdone-1; break; }
}
if (cmdline->script[0]) {
	int l;
	l=strlen(cmdline->script);
	if ( (l>3) && (!memcmp(cmdline->script+l-3,".py",3))) { // for whatever reason, python doesn't want a .py extension
		cmdline->script[l-3]='\0';
	}
}
*argc_out=argc-i;
*argv_out=argv+i;
return 0;
error:
	return -1;
}

SICLEARFUNC(x11info);
SICLEARFUNC(xftchar);
SICLEARFUNC(charcache);
SICLEARFUNC(pty);
SICLEARFUNC(all_event);
SICLEARFUNC(vte);
SICLEARFUNC(xclient);
SICLEARFUNC(cursor);
SICLEARFUNC(texttap);
SICLEARFUNC(xclipboard);

int main(int argc, char **argv) {
struct config config;
struct x11info x11info;
struct xftchar xftchar;
struct charcache charcache;
struct texttap texttap;
struct xclient xclient;
struct cursor cursor;
struct pty pty;
struct all_event all_event;
struct vte vte;
struct script *script=NULL;
void *cscript=NULL;
struct xclipboard xclipboard;
struct cmdline cmdline;
int pargc;
char **pargv;

(void)reset_config(&config);
clear_x11info(&x11info);
clear_xftchar(&xftchar);
clear_charcache(&charcache);
clear_texttap(&texttap);
clear_pty(&pty);
clear_all_event(&all_event);
clear_vte(&vte);
clear_xclient(&xclient);
clear_cursor(&cursor);
clear_xclipboard(&xclipboard);
cmdline.script[0]='\0'; cmdline.isnobytecode=0;

#ifdef TEST
#warning test
strcpy(cmdline.script,"user"); cmdline.isnobytecode=1; // TODO remove
#endif
if (parsecmdline(&pargc,&pargv,&cmdline,argc,argv)) GOTOERROR;
if (halfinit_x11info(&x11info,NULL)) GOTOERROR;
	config.screen.height=x11info.defscreen.height; config.screen.heightmm=x11info.defscreen.heightmm;
	config.screen.width=x11info.defscreen.width; config.screen.widthmm=x11info.defscreen.widthmm;
	config.depth=x11info.depth;
if (cmdline.isnopython) {
	if (!(cscript=new_cscript(&config))) GOTOERROR;
} else {
	if (!(script=new_script(&x11info,&config,&texttap,cmdline.script,cmdline.isnobytecode,cmdline.isstderr,pargc,pargv))) GOTOERROR;
}
if (config.isnostart) goto done; // could be -h

if (setenv("TERM",config.exportterm,1)) GOTOERROR;
if (setenv("TERMVER",TERMVER_CONFIG,1)) GOTOERROR;
x11info.depth=config.depth;

// if (verify_xclient()) GOTOERROR;
if (init_x11info(&x11info,config.xwidth,config.xheight,NULL,config.bgbgra,config.isfullscreen,TERMXTITLE_CONFIG)) GOTOERROR;
if (init_cursor(&cursor,&config,&x11info)) GOTOERROR;
if (init_xftchar(&xftchar,&config,&x11info)) GOTOERROR;
if (init_charcache(&charcache,&x11info,config.charcache,config.cellw,config.cellh)) GOTOERROR; // count: 2000 has worked fine
if (init_texttap(&texttap)) GOTOERROR;
if (init_pty(&pty,config.columns,config.rows,config.cmdline)) GOTOERROR;
if (init_all_event(&all_event,500)) GOTOERROR; // higher numbers increase delay in key processing
#define INPUTBUFFERSIZE	8192
#define MESSAGEBUFFERSIZE	1024
#define PASTEBUFFERMAX	(1024*1024)
#if INPUTBUFFERSIZE < BUFFSIZE_INSERTION_XCLIENT
#error
#endif
if (init_vte(&vte,&config,&pty,&all_event,&texttap,INPUTBUFFERSIZE,MESSAGEBUFFERSIZE,PASTEBUFFERMAX)) GOTOERROR;
if (init_xclipboard(&xclipboard,&x11info)) GOTOERROR;
if (script) {
	if (init_xclient(&xclient,&config,&x11info,&xftchar,&charcache,&texttap,&pty,&all_event,&vte,&cursor,&xclipboard,script)) GOTOERROR;
	xclient.hooks.key=onkey_script;
	xclient.hooks.control=oncontrolkey_script;
	xclient.hooks.control_s=onsuspend_script;
	xclient.hooks.control_q=onresume_script;
	xclient.hooks.alarmcall=onalarm_script;
	xclient.hooks.bell=onbell_script;
	xclient.hooks.getinsertion=getinsertion_script;
	xclient.hooks.checkinsertion=checkinsertion_script;
	xclient.hooks.message=onmessage_script;
	xclient.hooks.keysym=onkeysym_script;
	xclient.hooks.unkeysym=onkeysymrelease_script;
	xclient.hooks.onresize=onresize_script;
	xclient.hooks.pointer=onpointer_script;
	(void)addxclient_script(script,&xclient);
	if (oninitend_script(script)) GOTOERROR;
} else {
	if (!cscript) GOTOERROR;
	if (init_xclient(&xclient,&config,&x11info,&xftchar,&charcache,&texttap,&pty,&all_event,&vte,&cursor,&xclipboard,cscript)) GOTOERROR;
	xclient.hooks.control_s=onsuspend_cscript;
	xclient.hooks.control_q=onresume_cscript;
	xclient.hooks.message=onmessage_cscript;
	xclient.hooks.keysym=onkeysym_cscript;
	(void)addxclient_cscript(cscript,&xclient);
	if (oninitend_cscript(cscript)) GOTOERROR;
}

if (mainloop_xclient(&xclient)) GOTOERROR;
usleep(200*1000); // it's nice to see exit's lf
#ifdef USE_SAFEMEM
	(void)printout_safemem(stderr,__FILE__,__LINE__);
#endif

done:
deinit_xclipboard(&xclipboard);
deinit_cursor(&cursor);
deinit_xclient(&xclient);
deinit_vte(&vte);
deinit_all_event(&all_event);
deinit_pty(&pty);
deinit_texttap(&texttap);
deinit_charcache(&charcache);
deinit_xftchar(&xftchar);
deinit_x11info(&x11info);
if (script) free_script(script);
if (cscript) free_cscript(cscript);
return 0;
error:
	deinit_xclipboard(&xclipboard);
	deinit_cursor(&cursor);
	deinit_xclient(&xclient);
	deinit_vte(&vte);
	deinit_pty(&pty);
	deinit_texttap(&texttap);
	deinit_charcache(&charcache);
	deinit_xftchar(&xftchar);
	deinit_x11info(&x11info);
	if (script) free_script(script);
	if (cscript) free_cscript(cscript);
	return -1;
}
