/*
 * savemem.h
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
#ifdef USE_SAFEMEM
#define MALLOC(a)	malloc_safemem(a,__FILE__,__LINE__)
#define FREE(a)	free_safemem(a,__FILE__,__LINE__)
#define REALLOC(a,b)	realloc_safemem(a,b,__FILE__,__LINE__)
#define IFFREE(a) if (a) FREE(a)
#define SAFEMEM(a,b)	test_safemem(a,b,__FILE__,__LINE__)
#define debug_printout_safemem(a)	printout_safemem(a,__FILE__,__LINE__)
#else
#define MALLOC(a) malloc(a)
#define FREE(a) free(a)
#define REALLOC(a,b) realloc(a,b)
#define IFFREE(a) if (a) free(a)
#define SAFEMEM(a,b) do {} while (0)
#define debug_printout_safemem(a)	do{}while(0)
#endif

void *malloc_safemem(uint64_t size, char *filename, unsigned int line);
void free_safemem(void *ptr_in, char *filename, unsigned int line);
void *realloc_safemem(void *ptr_in, uint64_t size, char *filename, unsigned int line);
void test_safemem(void *ptr_in, uint64_t len, char *filename, unsigned int line);
void printout_safemem(FILE *fout, char *filename, unsigned int line);
