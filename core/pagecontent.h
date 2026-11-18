/*
 * pagecontent.h - adding to what a page draws (headers and footers, watermarks, backgrounds,
 * stamps, flattened annotations and form fields, redaction boxes, text and pictures) and the
 * small object helpers the editing files share. New content goes into a stream of its own,
 * appended (or, for backgrounds, put first) to the page's /Contents, the old content wrapped in
 * q ... Q so whatever it leaves set doesn't move the new. A stream can carry a mark (its first
 * line, "%Folio:<mark>") so a later Update or Remove finds the content it made.
 */
#ifndef FOLIO_PAGECONTENT_H
#define FOLIO_PAGECONTENT_H

#include "pdf.h"

/* ------------------------------------------------------------------ objects */
PObj *om_ref(int num);
PObj *om_arr(const double *v, int n);
PObj *om_text(const char *utf8);                 /* a text string: ASCII as it is, else UTF-16BE with its mark */
PObj *om_date(void);                             /* D:YYYYMMDDHHMMSS, now */
PObj *om_rgb(u32 c);                             /* [r g b] 0-1 */
int   om_num_of(PDoc *d, const PObj *o);         /* the object number o is cached under, 0 if none */
/* a new stream object holding a copy of data, dict (taken, may be NULL) its dictionary; its number (0 on failure) */
int   om_stream(PDoc *d, PObj *dict, const void *data, size_t n);
/* a Form XObject of ops, bbox and resources (taken, may be NULL); its number */
int   om_form(PDoc *d, const char *ops, size_t n, const double *bbox, PObj *res);
/* the standard 14 font dictionary for name ("Helvetica", "Helvetica-Bold", "Times-Roman", "Courier",
   "ZapfDingbats"...), WinAnsiEncoding for the text fonts */
PObj *om_std_font(const char *name);
/* a number as content-stream text, followed by a space */
void  om_num(Buf *b, double v);
/* a Latin-1 string as a literal string */
void  om_str(Buf *b, const char *s, int n);
/* Helvetica's (and Arial's) widths in 1/1000 em for Latin-1 text; Times and Courier the same way */
double om_text_width(const char *latin1, int n, double size, int font);   /* font: 0 Helvetica, 1 Helvetica-Bold, 2 Times, 3 Courier */

/* ------------------------------------------------------------------ pages */
PObj *pc_page(PDoc *d, int page);                /* d->pages[page], NULL if out of range */
/* the page's own resources dictionary (inherited or shared ones copied into the page first) */
PObj *pc_resources(PDoc *d, PObj *pg);
/* value (taken) added to the resources' category ("Font", "XObject", "ExtGState") under a name
   starting with prefix not used yet; the name (into name, cap bytes); 0 on failure */
int   pc_add_resource(PDoc *d, PObj *res, const char *cat, const char *prefix, PObj *value, char *name, int cap);
/* ops added to the page's content: after it (under 0) or before it (under 1). mark: the stream's
   first line is "%Folio:mark" (NULL none) */
int   pc_add_content(PDoc *d, int page, const char *ops, size_t n, int under, const char *mark);
/* the content streams marked mark taken out of the page; how many */
int   pc_remove_marked(PDoc *d, int page, const char *mark);
/* whether the page has a content stream marked mark */
int   pc_has_marked(PDoc *d, int page, const char *mark);
/* the page's whole content, its streams joined (malloc'd, *n long); NULL if it has none */
u8   *pc_content(PDoc *d, PObj *pg, size_t *n);
/* the page's content replaced by data (one new stream) */
int   pc_set_content(PDoc *d, PObj *pg, const u8 *data, size_t n);

/* ------------------------------------------------------------------ annotations into content */
/* the matrix taking an appearance form's space onto the annotation's rectangle (algorithm 8.1) */
void  pc_appearance_matrix(PDoc *d, PObj *annot, PObj *form, double m[6]);
/* the annotation's normal appearance (the state its /AS names, for forms with several); NULL none */
PObj *pc_appearance(PDoc *d, PObj *annot);
/* the annotation drawn into the page's content (its appearance as an XObject); 0 if it has none.
   It isn't removed from /Annots: the caller does that */
int   pc_flatten_annot(PDoc *d, int page, PObj *annot, Buf *ops, PObj *res);
#endif
