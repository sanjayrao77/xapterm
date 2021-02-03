
/*
 * xclipboard.h
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
#define MAX_SELECTION_XCLIPBOARD	15 
struct xclipboard {
	Display *display;
	Window window;
	Atom utf8,string,incr;
	int isowner:1;
	int isloopback:1;
	struct {
		Atom selection;
		char str_selection[MAX_SELECTION_XCLIPBOARD+1];
		unsigned int valuelen,valuemax;
		unsigned char *value;
	} copy;
	struct {
		Atom xseldata;
		Atom selection;
		char str_selection[MAX_SELECTION_XCLIPBOARD+1];
	} paste;
};

// selection is commonly PRIMARY, SECONDARY, CLIPBOARD

int init_xclipboard(struct xclipboard *xclip, struct x11info *xi);
void deinit_xclipboard(struct xclipboard *xclip);
int copy_xclipboard(struct xclipboard *xclip, char *str_selection, unsigned char *value, unsigned int len);
int onselectionrequest_xclipboard(struct xclipboard *xclip, XSelectionRequestEvent *e);
int paste_xclipboard(unsigned int *newlen_out, struct xclipboard *xclip, char *str_selection, int timeout);
int getpaste_xclipboard(struct xclipboard *xclip, unsigned char *dest, unsigned int destlen);
void onselectionclear_xclipboard(struct xclipboard *xclip);
int reserve_copy_xclipboard(struct xclipboard *xclip, char *str_selection, unsigned int len);
int add_copy_xclipboard(struct xclipboard *xclip, unsigned char *str, unsigned int len);
int finish_copy_xclipboard(struct xclipboard *xclip);
