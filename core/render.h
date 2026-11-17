/*
 * render.h - drawing PDF pages: the content stream interpreter (paths, text, images, shadings,
 * patterns, form XObjects, annotations' appearances) onto an RGB pixmap, and the text it draws
 * collected for searching, selecting and copying.
 */
#ifndef FOLIO_RENDER_H
#define FOLIO_RENDER_H

#include "pdf.h"
#include "geom.h"
#include "textpage.h"

typedef struct { int w, h; u8 *rgb; } Pixmap;       /* w * h * 3, rows top down */
typedef struct RCache RCache;                        /* what one document's pages share: loaded fonts */
RCache *rcache_new(PDoc *d);
void    rcache_free(RCache *c);

typedef struct {
  double scale;             /* pixels per PDF point (1/72 inch) */
  int rotate;               /* turned this much more than the page says (0, 90, 180, 270) */
  int x, y, w, h;           /* the part of the whole page's raster to draw (w 0: all of it) */
  int annots;               /* draw the annotations' appearances (comments, form fields) */
  int text_only;            /* only collect the text (nothing drawn; no pixmap needed) */
  TextPage *text;           /* if given, the text drawn is collected into it */
  int (*abort)(void *ud);   /* polled now and then; non-zero stops the drawing */
  void *ud;
  u32 paper;                /* the page's colour, 0xRRGGBB (white) */
  /* adding to a page drawn before (a new comment shown at once, without drawing the page again):
     onto is that drawing (the whole page at this scale and turn), started from instead of blank
     paper; annots_only skips the page's content; annot_first is the first annotation drawn */
  const u8 *onto;
  int annots_only;
  int annot_first;
} RenderOpts;
/* the size the whole page comes out at */
void pdf_page_pixels(PDoc *d, int page, double scale, int rotate, int *w, int *h);
/* the page's default user space (PDF units, y up) to the whole page's pixels (y down) */
Mat  pdf_page_matrix(PDoc *d, int page, double scale, int rotate);
/* the page drawn into out (its pixels allocated here, w * h * 3; free out->rgb); 0 on failure
   (out of memory: out->rgb NULL) or when stopped (out then holds what was drawn so far; free it too) */
int  pdf_render(PDoc *d, RCache *c, int page, const RenderOpts *o, Pixmap *out);
#endif
