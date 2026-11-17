/*
 * textpage.h - a page's text as drawn: each character with its box (in the page's default user
 * space), grouped into lines and words, for searching, selecting and copying.
 */
#ifndef FOLIO_TEXTPAGE_H
#define FOLIO_TEXTPAGE_H

#include "util.h"

typedef struct {
  u32 u;                    /* the character (0 for a glyph without text) */
  float x0, y0, x1, y1;     /* its box: PDF units, y up */
  float size;               /* the font size on the page */
  u8 dir;                   /* 0 left to right, 1 up, 2 right to left (upside down), 3 down */
} TChar;
typedef struct {
  TChar *c; int n, cap;     /* as drawn */
  /* after textpage_finish: in reading order, with spaces and line ends put in (u 32 and 10;
     boxes of zero width) */
  TChar *o; int no;
  int finished;
} TextPage;
void textpage_init(TextPage *t);
void textpage_free(TextPage *t);
void textpage_add(TextPage *t, u32 u, double x0, double y0, double x1, double y1, double size, int dir);
/* the characters sorted into lines (by baseline), words spaced, in reading order */
void textpage_finish(TextPage *t);
/* the text from character a to b (indices into o, inclusive) as UTF-8, malloc'd */
char *textpage_text(TextPage *t, int a, int b);
/* the next match of needle (UTF-8, case-insensitive) at or after index from; its first and last
   indices into o; 0 if none */
int  textpage_find(TextPage *t, const char *needle, int from, int *a, int *b);
/* the same backwards: the last match starting before index from */
int  textpage_find_back(TextPage *t, const char *needle, int from, int *a, int *b);
/* the character nearest to x, y (page units), -1 if none within slop */
int  textpage_hit(TextPage *t, double x, double y, double slop);
#endif
