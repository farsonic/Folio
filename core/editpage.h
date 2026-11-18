/*
 * editpage.h - editing what a page itself draws (PDFCraft's Edit text & images): a run of text
 * retyped in place (in its own font when that has the glyphs, else the standard font nearest to
 * it; the rest of the line moves along by the difference), pictures replaced, moved, resized or
 * deleted; and the points of the page's paths, for measuring tools to snap to.
 */
#ifndef FOLIO_EDITPAGE_H
#define FOLIO_EDITPAGE_H
#include "pdf.h"

enum { TE_NONE, TE_SAME_FONT, TE_STD_FONT };
/* the text whose glyphs have their centres in sel (x0 y0 x1 y1, page units) replaced by utf8 (may be
   empty: the run is deleted). Only text set horizontally. TE_SAME_FONT or TE_STD_FONT on success
   (which font it was set in), TE_NONE if no glyph was found there */
int text_replace(PDoc *d, int page, const double *sel, const char *utf8);

typedef struct { int num; char name[40]; double m[6]; int inline_img; int w, h; } PageImage;
/* the pictures the page draws, in order (m: its unit square onto the page); malloc'd, how many */
int page_images(PDoc *d, int page, PageImage **out);
/* picture index (as page_images counts) at x, y on the page; -1 if none (the last drawn wins) */
int page_image_at(PDoc *d, int page, double x, double y);
/* picture index drawn instead from a JPEG or PNG file, in the same place and size */
int image_replace(PDoc *d, int page, int index, const u8 *file, size_t n);
/* picture index drawn through the matrix m instead (moved, resized): taken out where it was and
   drawn again (on top of the page) */
int image_set_matrix(PDoc *d, int page, int index, const double *m);
int image_delete(PDoc *d, int page, int index);

/* the ends and corners of the page's paths (x, y pairs, page units; malloc'd), how many points */
int page_snap_points(PDoc *d, int page, double **pts);
/* the point of pts nearest to x, y within dist (into *sx, *sy): 1 if there is one */
int snap_nearest(const double *pts, int n, double x, double y, double dist, double *sx, double *sy);
#endif
