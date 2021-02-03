
/*
 * xftchar.h
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

struct xftchar {
	struct {
		int font0shift,font0line;
		int fontulline,fontullines;
	} config,effective;
	struct {
		int isallocated:1;
		XftColor xftc;
	} colors[16];
	struct x11info *x;
	unsigned int width,height;
	XftFont *font;
	XftDraw *draw;
	Pixmap pixmap;
//	unsigned char *data;
	int isotherpixmap:1;
};

int init_xftchar(struct xftchar *xc, struct config *config, struct x11info *x);
void deinit_xftchar(struct xftchar *xc);
int changefont_xftchar(struct xftchar *xc, char *fontname);
int resize_xftchar(struct xftchar *xc, unsigned int width, unsigned int height);
void setparams_xftchar(struct xftchar *xc, int font0shift, int font0line, int fontulline, int fontullines);
int setcolor_xftchar(struct xftchar *xc, unsigned int index, unsigned short red, unsigned short green, unsigned short blue);
void drawchar2_xftchar(Pixmap dest, struct xftchar *xc, unsigned int ucs4, unsigned int bgindex, unsigned int fgindex, int isunderline);

struct queryfont_xftchar {
	int ismatch;
	int ascent,descent,width;
};

int queryfont_xftchar(struct queryfont_xftchar *qf, struct x11info *xi, char *name);
