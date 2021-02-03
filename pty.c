/*
 * pty.c - handle linux's pty
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
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>
#include <termios.h>
#include <pty.h>
#include <utmp.h>
#define DEBUG
#include "common/conventions.h"

#include "pty.h"

int init_pty(struct pty *p, unsigned int cols, unsigned int rows, char **args) {
int ptym=-1,ptys=-1;
pid_t pid;
struct termios termios;
struct winsize winsize;

// fprintf(stderr,"%s:%d cols:%u rows:%u\n",__FILE__,__LINE__,cols,rows);

if (tcgetattr(STDOUT_FILENO,&termios)) GOTOERROR;
#if 0 //	(void)cfmakeraw(&termios);
termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
termios.c_cflag &= ~(CSIZE | PARENB);
termios.c_cflag |= CS8;
#endif
termios.c_lflag |= ECHO;
winsize.ws_row=rows; winsize.ws_col=cols; winsize.ws_xpixel=0; winsize.ws_ypixel=0;
if (openpty(&ptym,&ptys,NULL,&termios,&winsize)) GOTOERROR;

pid=fork();
if (pid<0) GOTOERROR;
if (!pid) {
	(ignore)close(ptym);
	(ignore)setsid();
	if (ioctl(ptys,TIOCSCTTY,NULL)) _exit(1); // controlling terminal
	if (0>dup2(ptys,STDIN_FILENO)) _exit(2);
	if (0>dup2(ptys,STDOUT_FILENO)) _exit(2);
	if (0>dup2(ptys,STDERR_FILENO)) _exit(3);
	if (args && args[0]) _exit(execvp(args[0],args));
	_exit(execl("/bin/bash","bash","-i",NULL));
}
(ignore)close(ptys);
p->master=ptym;
return 0;
error:
	ifclose(ptym);
	ifclose(ptys);
	return -1;
}
void deinit_pty(struct pty *p) {
ifclose(p->master);
}
int resize_pty(struct pty *p, unsigned int cols, unsigned int rows) {
struct winsize winsize;
winsize.ws_row=rows; winsize.ws_col=cols; winsize.ws_xpixel=0; winsize.ws_ypixel=0;
if (ioctl(p->master,TIOCSWINSZ,&winsize)) GOTOERROR;
return 0;
error:
	return -1;
}
