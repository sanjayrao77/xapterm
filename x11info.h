/*
 * x11info.h
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
struct x11info {
	Display *display;
	Visual *visual;
	Window window;
	int screen;
	GC context;
	Colormap colormap;
	XGCValues vls;
	XSetWindowAttributes attr;
//	Font font;
	unsigned int depth,Bpp;
	unsigned int width,height;
	unsigned int isfocused:1;
	unsigned int isnoexposed:1;
	struct {
		unsigned int width,height,widthmm,heightmm;
	} defscreen;
};

#if 0
struct image_x11info {
	struct x11info *x;
	XImage *image;
	unsigned char *frame;
	unsigned int width,height;
};
#endif

int init_x11info(struct x11info *x, unsigned int width, unsigned int height, char *display, unsigned char *bgra_bg, int isfs,
		char *wintitle);
int halfinit_x11info(struct x11info *x, char *display);
void deinit_x11info(struct x11info *x);
#if 0
int init_image_x11info(struct image_x11info *ix, struct x11info *x, unsigned int w, unsigned int h, unsigned char *bgbgra);
void deinit_image_x11info(struct image_x11info *b);
int paint2_image_x11info(struct image_x11info *ix, unsigned int sleft, unsigned int stop, unsigned int dleft, unsigned int dtop,
		unsigned int width, unsigned int height);
#endif
int testforshm_x11info(int *isfound_out, struct x11info *x);
char *evtypetostring_x11info(int type, char *def);
int resizewindow_x11info(struct x11info *x, unsigned int width, unsigned int height);
int waitforevent_x11info(Display *display, XEvent *dest, int type, int line, int seconds);
int movewindow_x11info(struct x11info *xi, int x, int y);
