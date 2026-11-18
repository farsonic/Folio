/*
 * export.h - a PDF's text exported as a web page, RTF, Word (.docx) or plain paragraphs, its
 * pages as a PowerPoint deck (.pptx, a picture of each page per slide), and every picture it
 * holds as JPEG (as stored) or PNG files. The text comes from each page's TextPage (the caller's
 * get gives it, finished): lines are joined into paragraphs, and lines set clearly larger than
 * the body text become headings.
 */
#ifndef FOLIO_EXPORT_H
#define FOLIO_EXPORT_H

#include "pdf.h"
#include "textpage.h"
#include "render.h"

typedef TextPage *(*ExportText)(void *ud, int page);
enum { EXP_HTML, EXP_RTF, EXP_DOCX, EXP_TEXT };
/* the document's text in the format (malloc'd, *len long); NULL if out of memory */
u8 *export_text(PDoc *d, int format, ExportText get, void *ud, size_t *len);
/* a deck of the pages drawn at dpi (each a PNG on its own slide) */
u8 *export_pptx(PDoc *d, RCache *rc, int dpi, size_t *len);
/* every image XObject the pages draw, once each: each(ud, name, data, n) with "image-3.jpg" or
   "image-4.png"; how many */
int export_images(PDoc *d, int (*each)(void *ud, const char *name, const u8 *data, size_t n), void *ud);
#endif
