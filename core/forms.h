/*
 * forms.h - interactive forms (AcroForm): the fields read with their values and widgets, filled
 * in (text, check boxes, radio buttons, drop-down and list boxes), each change given a new
 * appearance stream so every viewer shows it; cleared, flattened into the pages, exported and
 * imported as FDF or XFDF; fields added, renamed and given properties, put in tab order, and
 * found on a page that has blanks drawn but no fields. No JavaScript: formats, validation and
 * calculations that need it are not run.
 */
#ifndef FOLIO_FORMS_H
#define FOLIO_FORMS_H

#include "pdf.h"
#include "textpage.h"

enum { FF_TEXT, FF_CHECK, FF_RADIO, FF_PUSH, FF_COMBO, FF_LIST, FF_SIG };
/* field flags (Ff) */
#define FFL_READONLY   (1L << 0)
#define FFL_REQUIRED   (1L << 1)
#define FFL_NOEXPORT   (1L << 2)
#define FFL_MULTILINE  (1L << 12)
#define FFL_PASSWORD   (1L << 13)
#define FFL_NOTOGGLE   (1L << 14)
#define FFL_RADIO      (1L << 15)
#define FFL_PUSH       (1L << 16)
#define FFL_COMBO      (1L << 17)
#define FFL_EDIT       (1L << 18)
#define FFL_MULTISEL   (1L << 21)
#define FFL_COMB       (1L << 24)

typedef struct {
  PObj *dict;               /* the widget annotation */
  int page, index;          /* its page and place in that page's /Annots (-1 if on no page) */
  double rect[4];
  char on[64];              /* check box and radio button: the name of its "on" state */
} FWidget;
typedef struct {
  PObj *dict;               /* the field (its terminal node) */
  char *name;               /* fully qualified (a.b.c), UTF-8 */
  char *tooltip;            /* TU, UTF-8 (may be NULL) */
  int type;                 /* FF_ */
  long flags;               /* Ff, inherited */
  char *value;              /* UTF-8: the text, the option chosen (its export value), or a button's state ("Off" when off) */
  char **opts, **optx; int nopts;   /* choices: as shown and their export values */
  int maxlen;               /* 0 none */
  int quad;                 /* 0 left, 1 centred, 2 right */
  FWidget *w; int nw;
} FField;
typedef struct { FField *f; int n; int need_appearances; } FForm;

/* the document's fields (f->n 0 if it has no form); free with form_free */
int   form_load(PDoc *d, FForm *f);
void  form_free(FForm *f);
/* the field (its index) and widget at a page point, -1 none */
int   form_field_at(FForm *f, int page, double x, double y, int *widget);
/* values set, the appearances made again; 0 if the field can't take it (read-only, wrong type) */
int   form_set_text(PDoc *d, FField *f, const char *utf8);
int   form_set_check(PDoc *d, FField *f, int widget, int on);    /* check box: every widget; radio button: the one given on */
int   form_set_choice(PDoc *d, FField *f, int option);          /* -1 none */
/* every field back to its default (/DV, else empty or off) */
int   form_reset(PDoc *d, FForm *f);
/* appearances made for every text and choice field (those without one only, unless all) */
int   form_refresh(PDoc *d, FForm *f, int all);
/* the widgets drawn into the pages and the form taken away; how many were flattened */
int   form_flatten(PDoc *d, FForm *f);
/* the values as XFDF (xml) or FDF; malloc'd, *len long */
char *form_export(PDoc *d, FForm *f, int fdf, const char *pdfname, size_t *len);
/* values from XFDF or FDF set (fields matched by full name); how many were set, -1 if it isn't either */
int   form_import(PDoc *d, FForm *f, const u8 *data, size_t len);

/* ------------------------------------------------------------------ preparing a form */
/* a new field on the page (FF_ type; FF_TEXT with multiline 1 for a text box of several lines),
   named name (made unique; NULL: a name of the kind's own), with the options for a choice; 0 on failure.
   A radio button added under the name of an existing radio group joins it. date: a text field for a date */
int   form_add_field(PDoc *d, int page, int type, const double *rect, const char *name, const char *const *opts, int nopts, int multiline);
/* a table of text fields, rows x cols cells in rect, named name.r.c */
int   form_add_table(PDoc *d, int page, const double *rect, int rows, int cols, const char *name);
/* a field's properties changed (NULL/negative: as it is): partial name, tooltip, flags to set and to
   clear, MaxLen, alignment, the text size (0 auto) */
int   form_set_props(PDoc *d, FField *f, const char *name, const char *tooltip, long set, long clear, int maxlen, int quad, double size);
/* a field (and its widgets) deleted */
int   form_delete_field(PDoc *d, FField *f);
/* each page's widgets ordered by rows (top to bottom, then left to right) or by columns; /Tabs set */
int   form_tab_order(PDoc *d, int by_columns);
/* the calculation order (/CO) set to these fields, in order */
int   form_calc_order(PDoc *d, FForm *f, const int *fields, int n);
/* fields made where the page draws blanks: runs of underscores (text fields) and empty boxes
   ("[ ]", U+2610) (check boxes); how many */
int   form_detect(PDoc *d, int page, TextPage *t);
/* (round 2) a picture field (button) shown with a JPEG or PNG picture, fitted into each widget */
int   form_set_picture(PDoc *d, FField *f, const u8 *file, size_t n);
#endif
