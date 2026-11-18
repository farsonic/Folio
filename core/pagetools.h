/*
 * pagetools.h - PDFCraft's page and document tools that change what is in the document: page
 * boxes and cropping, page labels, duplicating, reversing, replacing and splitting pages;
 * headers and footers, Bates numbers, watermarks and backgrounds (each can be updated or
 * removed later); text and pictures added to a page; links; attachments; bookmarks written
 * back (and made from the text's styles); comments flattened; stamps, arrows and clouds; new
 * documents (blank, from pictures, from text); XMP metadata kept in step with the information.
 */
#ifndef FOLIO_PAGETOOLS_H
#define FOLIO_PAGETOOLS_H

#include "pdf.h"
#include "textpage.h"

/* ------------------------------------------------------------------ boxes */
enum { BOX_MEDIA, BOX_CROP, BOX_BLEED, BOX_TRIM, BOX_ART };
/* a page's box (own or inherited; crop defaults to media, the others to crop); 1 if it has one of its own or inherited */
int  page_get_box(PDoc *d, int page, int which, double r[4]);
/* set (r NULL: taken away; the media box can't be) */
int  page_set_box(PDoc *d, int page, int which, const double *r);
/* pages first..last cropped by margins (points) from their visible box: left, bottom, right, top */
int  page_crop(PDoc *d, int first, int last, double l, double b, double r, double t);
/* the matrix from a page's box as displayed (rotation applied; origin its bottom left) to its user
   space, and its displayed width and height */
void page_display_matrix(PDoc *d, int page, double m[6], double *w, double *h);

/* ------------------------------------------------------------------ page labels */
typedef struct { int start; char style; char prefix[32]; int first; } PLabel;   /* style 'D' 'R' 'r' 'A' 'a', 0 none */
int  page_labels_get(PDoc *d, PLabel **out);             /* the ranges (malloc'd); how many */
int  page_labels_set(PDoc *d, const PLabel *l, int n);   /* n 0: labels taken away */
void page_label(PDoc *d, int page, char *out, int cap);  /* "iv", "A-3", or the page number */

/* ------------------------------------------------------------------ pages */
int  page_duplicate(PDoc *d, int page, int copies);      /* copies inserted after it */
int  page_reverse(PDoc *d, int first, int last);
/* n pages from at replaced by src's first..last */
int  page_replace(PDoc *d, int at, int n, PDoc *src, int first, int last);
/* where to split: mode 0 every value pages, 1 at each top-level bookmark, 2 into value files of
   equal size; the first page of each part (malloc'd into *starts); how many parts */
int  split_points(PDoc *d, int mode, int value, int **starts);

/* ------------------------------------------------------------------ headers, footers, Bates, watermarks, backgrounds */
typedef struct {
  char text[6][200];        /* header left, centre, right; footer left, centre, right (UTF-8). Tokens:
                               <<page>> <<pages>> <<date>> <<bates>> <<label>> */
  double size;              /* points (0: 10) */
  u32 color;
  int font;                 /* 0 Helvetica, 1 Helvetica-Bold, 2 Times-Roman, 3 Courier */
  double top, bottom, side; /* margins (points; 0: 36) */
  int first, last;          /* pages (last -1: to the end) */
  int bates_start, bates_digits;
  char bates_prefix[32], bates_suffix[32];
} HFOpts;
/* added to the pages, marked mark ("header" or "bates": each removed or updated by its mark) */
int  hf_apply(PDoc *d, const HFOpts *o, const char *mark);
int  marks_remove(PDoc *d, const char *mark);            /* every page's content marked so taken out; pages changed */
int  marks_present(PDoc *d, const char *mark);           /* pages that have it */
typedef struct {
  char text[200];           /* UTF-8; or a picture: */
  const u8 *image; size_t image_len;
  double size;              /* text points (0: 72), or the picture's scale against the page (0: 0.5) */
  u32 color;
  double opacity;           /* 0-1 (0: 0.3) */
  double angle;             /* degrees anticlockwise */
  int font;
  int behind;               /* under the page's content */
  int first, last;
} WMOpts;
int  watermark_apply(PDoc *d, const WMOpts *o);          /* marked "watermark" */
/* a colour (or picture, stretched to the page) under the pages; marked "background" */
int  background_apply(PDoc *d, u32 color, const u8 *image, size_t image_len, int first, int last);

/* ------------------------------------------------------------------ text, pictures, links */
/* text in the page's content, lines broken at \n, top left at x, y (page user space) */
int  page_add_text(PDoc *d, int page, double x, double y, const char *utf8, double size, u32 color, int font);
/* a JPEG or PNG file as an image XObject: its object number, size in pixels; 0 if it can't be */
int  image_xobject(PDoc *d, const u8 *file, size_t n, int *w, int *h);
/* a picture into the rectangle (kept in proportion, centred) */
int  page_add_image(PDoc *d, int page, const double *rect, const u8 *file, size_t n);
/* a link over rect to a page (dest_page >= 0, with top: its y, -1 the top) or a web address */
int  page_add_link(PDoc *d, int page, const double *rect, int dest_page, double top, const char *uri);

/* ------------------------------------------------------------------ attachments */
typedef struct { char *name, *desc; long size; } PAttach;
int  attach_list(PDoc *d, PAttach **out);
void attach_free(PAttach *a, int n);
int  attach_add(PDoc *d, const char *name, const u8 *data, size_t n, const char *desc);
u8  *attach_data(PDoc *d, const char *name, size_t *n);
int  attach_delete(PDoc *d, const char *name);

/* ------------------------------------------------------------------ bookmarks */
POutline *outline_item(const char *title, int page, double top);   /* a new item (malloc'd; free with pdf_outline_free) */
/* the document's bookmarks replaced by tree (NULL: none) */
int  outline_write(PDoc *d, const POutline *tree);
/* bookmarks from the text's styles: lines in sizes larger than the body text become headings,
   up to three levels by size. get gives a page's text (finished) */
POutline *outline_from_styles(PDoc *d, TextPage *(*get)(void *ud, int page), void *ud);

/* ------------------------------------------------------------------ annotations */
/* comments (not links, form fields or pop-ups) drawn into the page's content and taken away;
   page -1: every page. How many */
int  annots_flatten(PDoc *d, int page);
/* a stamp: text ("APPROVED", "DRAFT"...) in a rounded box, its centre at x, y */
int  annot_stamp(PDoc *d, int page, double x, double y, const char *text, u32 color, const char *author);
/* an arrow (a line annotation ending in an open arrowhead) from x1 y1 to x2 y2 */
int  annot_arrow(PDoc *d, int page, const double *c, double width, u32 color, const char *author);
/* a cloud: a polygon annotation with a cloudy border round rect */
int  annot_cloud(PDoc *d, int page, const double *rect, double width, u32 color, const char *author);
/* a distance measured between two points: a line annotation with its length as its caption,
   scale points per unit (e.g. 72 / 2.54 * 100 for 1 cm = 1 m), unit its name */
int  annot_measure(PDoc *d, int page, const double *c, double scale, const char *unit, u32 color, const char *author);
/* perimeter or area of a polygon (n points): a polygon annotation captioned with it */
int  annot_measure_area(PDoc *d, int page, const double *pts, int n, int area, double scale, const char *unit, u32 color, const char *author);

/* ------------------------------------------------------------------ new documents */
PDoc *doc_new_blank(double w, double h, int pages);
/* each picture (JPEG or PNG) on a page of its own: the page the picture's size at 72 dpi (fit 0)
   or the picture fitted to w x h pages */
PDoc *doc_from_images(const u8 *const *files, const size_t *lens, int n, double w, double h);
/* plain text set in Courier or Helvetica on w x h pages */
PDoc *doc_from_text(const char *utf8, size_t n, double w, double h, double size, int font);

/* ------------------------------------------------------------------ metadata */
/* the XMP metadata stream written from the document information (title, author, subject,
   keywords, producer, dates) */
int  xmp_update(PDoc *d);
#endif
