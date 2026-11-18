/*
 * redact.h - redaction and cleaning: areas, found text and whole pages marked for redaction
 * (Redact annotations, which show where until applied); applying them takes the text under
 * them out of the content (the glyphs, not just covered), clears the covered pixels of images
 * (or removes images it can't decode), removes annotations there and paints the areas over.
 * Remove Hidden Information and Sanitize take out what a reader doesn't see: metadata, scripts,
 * attachments, comments, form fields, hidden layers, invisible text, thumbnails, private data.
 */
#ifndef FOLIO_REDACT_H
#define FOLIO_REDACT_H

#include "pdf.h"
#include "textpage.h"

typedef struct {
  u32 fill;                 /* the colour the areas are painted (0xFFFFFFFF: not painted) */
  char overlay[64];         /* text written over each area (UTF-8, "" none) */
  u32 outline;              /* the marks' outline colour while unapplied */
} RedactProps;
void redact_props_default(RedactProps *p);
/* marks: an area, text's quads (8 numbers each), a whole page; 1 if made */
int  redact_mark_area(PDoc *d, int page, const double *rect, const RedactProps *p);
int  redact_mark_quads(PDoc *d, int page, const double *quads, int n, const RedactProps *p);
int  redact_mark_page(PDoc *d, int page, const RedactProps *p);
/* every match of needle (case-insensitive) on the page's text marked; how many */
int  redact_mark_text(PDoc *d, int page, TextPage *t, const char *needle, const RedactProps *p);
/* the marks on a page / in the document */
int  redact_count(PDoc *d, int page);
/* the marks applied (page -1: every page); how many areas */
int  redact_apply(PDoc *d, int page);

enum {
  HID_METADATA = 1, HID_SCRIPTS = 2, HID_ATTACH = 4, HID_COMMENTS = 8, HID_FORMS = 16, HID_LAYERS = 32,
  HID_INVISIBLE = 64, HID_THUMBS = 128, HID_PRIVATE = 256, HID_BOOKMARKS = 512, HID_LINKS = 1024,
  HID_ALL = 2047, HID_SANITIZE = HID_METADATA | HID_SCRIPTS | HID_ATTACH | HID_COMMENTS | HID_FORMS | HID_LAYERS |
                                 HID_INVISIBLE | HID_THUMBS | HID_PRIVATE | HID_LINKS
};
/* what is there of each kind: HID_ bits */
int  hidden_survey(PDoc *d);
/* the kinds taken out; how many things were */
int  hidden_remove(PDoc *d, int what);
#endif
