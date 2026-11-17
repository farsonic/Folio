/*
 * pdfedit.h - what Folio changes in a document: annotations (comments, highlights, notes, text
 * boxes, drawings, shapes) added, edited and deleted, each with an appearance stream so any viewer
 * shows it as Folio does; pages turned, moved, deleted and brought in from another document. The
 * changes stay in memory until the document is saved (pdfwrite.h). And the page's links.
 */
#ifndef FOLIO_PDFEDIT_H
#define FOLIO_PDFEDIT_H

#include "pdf.h"

/* ------------------------------------------------------------------ links */
typedef struct { double rect[4]; int page; double top; char *uri; } PLink;   /* page -1: a web link (uri) */
int  pdf_links(PDoc *d, int page, PLink **out);     /* the page's links; free with pdf_links_free */
void pdf_links_free(PLink *l, int n);

/* ------------------------------------------------------------------ annotations */
enum { AN_OTHER, AN_HIGHLIGHT, AN_UNDERLINE, AN_STRIKE, AN_SQUIGGLY, AN_NOTE, AN_FREETEXT, AN_INK, AN_SQUARE, AN_CIRCLE, AN_LINE,
       AN_LINK, AN_WIDGET, AN_STAMP };
typedef struct {
  int index;                /* its place in the page's /Annots */
  int kind;                 /* AN_ */
  double rect[4];
  u32 color;                /* 0xRRGGBB (0xFFFFFFFF none) */
  char *contents, *author, *date;   /* UTF-8 (may be NULL) */
} PAnnot;
int  pdf_annots(PDoc *d, int page, PAnnot **out);   /* the page's annotations (not links or popups) */
void pdf_annots_free(PAnnot *a, int n);
/* new annotations (each returns 1 if made). Coordinates are the page's default user space.
   Text markup: quads, 8 numbers each (x1 y1 x2 y2 x3 y3 x4 y4: upper left, upper right, lower
   left, lower right), n of them */
int  annot_markup(PDoc *d, int page, int kind, const double *quads, int nquads, u32 color, const char *author, const char *text);
int  annot_note(PDoc *d, int page, double x, double y, u32 color, const char *author, const char *text);
int  annot_freetext(PDoc *d, int page, const double *rect, const char *text, double size, u32 color, const char *author);
/* ink: strokes, each npts[i] points (x, y pairs) one after another in pts */
int  annot_ink(PDoc *d, int page, const double *pts, const int *npts, int nstrokes, double width, u32 color, const char *author);
int  annot_shape(PDoc *d, int page, int kind, const double *coords, double width, u32 color, const char *author);   /* square/circle: rect; line: x1 y1 x2 y2 */
int  annot_delete(PDoc *d, int page, int index);
int  annot_set_text(PDoc *d, int page, int index, const char *text);

/* for undoing: how many entries the page's /Annots has, the object number of one (0 if it is
   direct), and an annotation object put back at index */
int  annot_count(PDoc *d, int page);
int  annot_objnum(PDoc *d, int page, int index);
int  annot_restore(PDoc *d, int page, int index, int objnum);
/* the annotation at the page point x, y (default user space): its /Annots index, -1 none (links,
   popups and hidden ones skipped; the topmost wins) */
int  annot_at(PDoc *d, int page, double x, double y);

/* ------------------------------------------------------------------ pages */
void pdf_rotate_page(PDoc *d, int page, int quarter_turns);   /* clockwise */
int  pdf_delete_page(PDoc *d, int page);                       /* 0 if it is the only one */
int  pdf_move_page(PDoc *d, int from, int to);
/* pages first..last of src copied into d before page at (d->npages: at the end) */
int  pdf_insert_pages(PDoc *d, int at, PDoc *src, int first, int last);
/* a page dictionary (one taken out with pdf_delete_page) put back before at */
int  pdf_restore_page(PDoc *d, int at, PObj *page);
/* a blank page of w x h points inserted before at */
int  pdf_insert_blank(PDoc *d, int at, double w, double h);
/* the document information (Title, Author, Subject, Keywords) set (UTF-8) */
void pdf_set_info(PDoc *d, const char *key, const char *value);
/* (each change sets d->changed) */
#endif
