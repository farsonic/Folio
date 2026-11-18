/*
 * contentedit.h - a page's content stream walked operator by operator and written out again,
 * changed on the way: glyphs taken out of the text that shows them (the others keep their
 * places), images replaced or removed, operators rewritten or dropped, marked content in hidden
 * layers and invisible text left out. Form XObjects the page draws are walked too; one that
 * changes is written as a new form for this page. Redaction, Remove Hidden Information, Fix
 * Hairlines, Convert Colours and the optimizer's image survey are built on it.
 */
#ifndef FOLIO_CONTENTEDIT_H
#define FOLIO_CONTENTEDIT_H

#include "pdf.h"
#include "font.h"

typedef struct {
  double ctm[6];
  PdfFont *font; double size, tc, tw, th, tl, rise; int tmode;
  double lw;
  /* (round 2: for editing text in place) the glyph being shown: its rendering matrix (text space at
     the glyph's origin onto the page, the font size in it), the font's resource name, its bytes in
     the string; the fill colour (1 grey, 3 RGB or 4 CMYK components); whether it is in a form XObject */
  double trm[6]; char fname[40]; const u8 *bytes; int nbytes;
  double fill[4]; int nfill; int in_form;
} CEState;
typedef struct {
  void *ud;
  /* each glyph shown: its box on the page (default user space: x0 y0 x1 y1); non-zero removes it */
  int (*glyph)(void *ud, const double *box, u32 code, const CEState *s);
  /* an image drawn (an XObject, or an inline image when inline_img): m takes its unit square
     onto the page. Return 0 to keep it, 1 to remove it, 2 to draw instead the image object *newnum */
  int (*image)(void *ud, PObj *img, int inline_img, const double *m, int *newnum);
  /* (the image XObject's name in the resources, for image: set before each call) */
  char xname[40];
  /* any other operator with its operands (which it may change in place, or replace: args has
     room for 8; *nargs may change); kw may be changed to another operator of up to 3 letters
     (kw has room for 4 bytes). Returns 0 to keep it, 1 to drop it, 2 when it changed it */
  int (*op)(void *ud, char *kw, PObj **args, int *nargs, const CEState *s);
  int drop_hidden;          /* marked content in optional content groups that are off left out */
  int drop_invisible;       /* text drawn in render mode 3 (invisible: OCR layers) left out */
} CEOps;
/* the page's content rewritten; 1 if anything changed (then the page has a new content stream) */
int ce_rewrite_page(PDoc *d, int page, const CEOps *ops);
/* the same for any content with its resources (a form's, an appearance's): the new content
   (malloc'd, *n long) or NULL if nothing changed; res_out gets the resources it now needs
   (a copy of res with new names added; caller frees) */
u8 *ce_rewrite(PDoc *d, const u8 *data, size_t len, PObj *res, const double *ctm, const CEOps *ops, size_t *n, PObj **res_out);
/* an image XObject's samples: rows of w x comps samples of bpc bits (a JPEG decoded to 8-bit RGB:
   *as_rgb set); malloc'd, NULL if it can't be read (JPEG 2000, JBIG2, CCITT, a pattern colour space) */
u8 *ce_image_samples(PDoc *d, PObj *img, int *w, int *h, int *comps, int *bpc, int *as_rgb);
/* samples stored as a new image like orig (its colour space, mask, decode...): JPEG at quality
   when it is 8-bit RGB and quality > 0, else Flate. as_rgb: the samples are RGB whatever orig was.
   Its object number, 0 on failure */
int ce_image_store(PDoc *d, PObj *orig, const u8 *samples, int w, int h, int comps, int bpc, int as_rgb, int quality);
#endif
