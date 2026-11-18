/*
 * optimize.h - making documents smaller and print production: pictures stored again at a lower
 * resolution, identical streams found (written once), what the space goes on (Audit Space
 * Usage); hairlines thickened, colours converted to grey, printer's crop marks added.
 * Compressing streams and packing objects are pdf_save_opts' options (pdfwrite.h).
 */
#ifndef FOLIO_OPTIMIZE_H
#define FOLIO_OPTIMIZE_H

#include "pdf.h"

typedef struct {
  long images, fonts, content, forms, metadata, attachments, other_streams, objects, total;
  int nimages, nfonts, nobjects;
} SpaceAudit;
void optimize_audit(PDoc *d, SpaceAudit *a);
/* pictures shown at more than max_dpi (at their largest on any page) stored again at max_dpi:
   JPEG at quality for colour pictures, Flate for the rest; how many */
int  optimize_images(PDoc *d, int max_dpi, int quality);
/* streams with the same dictionary and data: old number -> the first of them (malloc'd, d->nxref
   long, 0 where unique); for SaveOpts.alias. *found how many duplicates */
int *optimize_dedupe(PDoc *d, int *found);
/* lines thinner than min (points; 0 included) drawn width wide instead; pages changed */
int  fix_hairlines(PDoc *d, double min, double width);
/* RGB and CMYK colours (operators and 8-bit pictures) converted to grey; pages changed */
int  convert_to_gray(PDoc *d);
/* (round 2) the same to any device space: CC_GRAY, CC_RGB or CC_CMYK (fill and stroke colours of the
   device spaces and 8-bit device pictures; shadings, ICC-based and spot colours are left as they are) */
enum { CC_GRAY, CC_RGB, CC_CMYK };
int  convert_colors(PDoc *d, int target);
/* crop marks: each page's media box grown by bleed + 18 points, its trim box the old visible
   box, marks drawn at its corners (marked "marks"); pages changed */
int  printer_marks(PDoc *d, double bleed);
#endif
