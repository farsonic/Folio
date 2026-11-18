/*
 * edits.c - what Folio changes in a document, each step kept so Undo can take it back: comments
 * (highlights, underlines, strike-outs, notes, text boxes, drawings, shapes) added, edited and
 * deleted, and pages turned, deleted, moved and inserted (blank or from another file). Undo works
 * by doing the opposite (a comment taken out again, a page put back where it was), so it costs
 * nothing until it is used, however big the document.
 *
 * Also here: each page's text and thumbnail, kept by the page's dictionary so they follow the
 * page when pages move.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

/* ------------------------------------------------------------------ page text and thumbnails */
static PObj *page_obj(DocView *v, int page) { return page >= 0 && page < v->pdf->npages ? v->pdf->pages[page] : NULL; }
static PText *ptext_find(DocView *v, int page)
{
  PObj *pg = page_obj(v, page);
  int i;
  if (!pg) return NULL;
  for (i = 0; i < v->ntexts; i++) if (v->texts[i].pg == pg) return &v->texts[i];
  return NULL;
}
int page_text_ready(DocView *v, int page) { return ptext_find(v, page) != NULL; }
TextPage *page_text(DocView *v, int page)
{
  PText *p = ptext_find(v, page);
  RenderOpts o;
  Pixmap pm;
  if (p) return &p->tp;
  if (!page_obj(v, page)) return NULL;
  if (v->ntexts >= v->captexts) {
    int nc = v->captexts ? v->captexts * 2 : 16;
    PText *n2 = (PText *)realloc(v->texts, (size_t)nc * sizeof(PText));
    if (!n2) return NULL;
    v->texts = n2; v->captexts = nc;
  }
  p = &v->texts[v->ntexts];
  p->pg = page_obj(v, page);
  textpage_init(&p->tp);
  memset(&o, 0, sizeof o);
  memset(&pm, 0, sizeof pm);
  o.scale = 1; o.text_only = 1; o.text = &p->tp;
  pdf_render(v->pdf, v->rc, page, &o, &pm);
  free(pm.rgb);
  textpage_finish(&p->tp);
  v->ntexts++;
  return &p->tp;
}
static void ptext_drop(DocView *v, PObj *pg)
{
  int i;
  for (i = 0; i < v->ntexts; i++) if (v->texts[i].pg == pg) { textpage_free(&v->texts[i].tp); v->texts[i] = v->texts[--v->ntexts]; i--; }
}
Thumb *thumb_get(DocView *v, int page, int make)
{
  PObj *pg = page_obj(v, page);
  double box[4], s;
  int rot, i, w, h;
  RenderOpts o;
  Thumb *t;
  if (!pg) return NULL;
  pdf_page_box(v->pdf, page, box, &rot);
  for (i = 0; i < v->nthumbs; i++) if (v->thumbs[i].pg == pg) {
    if (v->thumbs[i].rot == rot) return &v->thumbs[i];
    free(v->thumbs[i].pm.rgb);
    v->thumbs[i] = v->thumbs[--v->nthumbs];
    break;
  }
  if (!make) return NULL;
  if (v->nthumbs >= v->capthumbs) {
    int nc = v->capthumbs ? v->capthumbs * 2 : 16;
    Thumb *n2 = (Thumb *)realloc(v->thumbs, (size_t)nc * sizeof(Thumb));
    if (!n2) return NULL;
    v->thumbs = n2; v->capthumbs = nc;
  }
  pdf_page_pixels(v->pdf, page, 1, 0, &w, &h);
  s = (double)THUMB_W / (w > 0 ? w : 1);
  if ((double)THUMB_H / (h > 0 ? h : 1) < s) s = (double)THUMB_H / (h > 0 ? h : 1);
  memset(&o, 0, sizeof o);
  o.scale = s; o.annots = 1; o.paper = 0xFFFFFF;
  t = &v->thumbs[v->nthumbs];
  memset(t, 0, sizeof *t);
  t->pg = pg; t->rot = rot;
  pdf_render(v->pdf, v->rc, page, &o, &t->pm);
  v->nthumbs++;
  return t;
}
static void thumb_drop(DocView *v, PObj *pg)
{
  int i;
  for (i = 0; i < v->nthumbs; i++) if (v->thumbs[i].pg == pg) { free(v->thumbs[i].pm.rgb); v->thumbs[i] = v->thumbs[--v->nthumbs]; i--; }
}
void page_changed(DocView *v, int page)
{
  page_edited(v, page, 0);
}
/* (see folio.h) the page's thumbnail dropped, and its picture in the view kept on screen until it is
   drawn again (added: the new annotation drawn over it straight away), rather than dropped, which
   left the page white until it had been drawn again from the start */
void page_edited(DocView *v, int page, int added)
{
  PObj *pg = page_obj(v, page);
  if (!pg) return;
  thumb_drop(v, pg);
  DoMethod(view, MUIM_View_PageEdited, (ULONG)v, (ULONG)pg, (LONG)added);
}
void doc_pages_changed(DocView *v)
{
  int n = v->pdf->npages;
  free(v->picked);
  v->picked = (u8 *)calloc((size_t)(n > 0 ? n : 1), 1);
  v->sel_page = v->find_page = v->an_page = -1;
  if (v->page >= n) v->page = n - 1;
  if (v->page < 0) v->page = 0;
  DoMethod(view, MUIM_View_Refresh);
}

/* ------------------------------------------------------------------ undo */
static int undo_grp, grp_open;
void undo_begin(void) { undo_grp++; grp_open = 1; }
static void undo_free(Undo *u) { free(u->text); memset(u, 0, sizeof *u); }
void undo_clear(DocView *v)
{
  int i;
  for (i = 0; i < v->nundo; i++) undo_free(&v->undo[i]);
  v->nundo = 0;
}
void undo_push(DocView *v, int kind, int page, int index, int a, int b, PObj *obj, const char *text)
{
  Undo *u;
  if (!grp_open) undo_grp++;
  if (v->nundo >= MAXUNDO) {
    /* the oldest step (and the rest of its group) forgotten */
    int g = v->undo[0].grp, k = 0, i;
    while (k < v->nundo && v->undo[k].grp == g) undo_free(&v->undo[k++]);
    for (i = k; i < v->nundo; i++) v->undo[i - k] = v->undo[i];
    v->nundo -= k;
    if (v->nundo >= MAXUNDO) return;
  }
  u = &v->undo[v->nundo++];
  memset(u, 0, sizeof *u);
  u->kind = kind; u->page = page; u->index = index; u->a = a; u->b = b; u->obj = obj; u->grp = undo_grp;
  u->text = text ? str_dup(text) : NULL;
}
static void undo_end(void) { grp_open = 0; }
const char *undo_name(DocView *v)
{
  static const char *const names[] = { "Add Comment", "Delete Comment", "Edit Comment", "Rotate Pages", "Delete Pages", "Move Page", "Insert Pages", "Change Document" };
  if (!v || !v->nundo) return "";
  return names[v->undo[v->nundo - 1].kind];
}
int undo_last(DocView *v)
{
  int g, pages = 0;
  if (!v || !v->nundo) return 0;
  g = v->undo[v->nundo - 1].grp;
  while (v->nundo && v->undo[v->nundo - 1].grp == g) {
    Undo *u = &v->undo[--v->nundo];
    PDoc *d = v->pdf;
    int k;
    switch (u->kind) {
    case U_ADDANNOT: annot_delete(d, u->page, u->index); page_changed(v, u->page); break;
    case U_DELANNOT: annot_restore(d, u->page, u->index, u->a); page_changed(v, u->page); break;
    case U_ANNOTTEXT: annot_set_text(d, u->page, u->index, u->text ? u->text : ""); page_changed(v, u->page); break;
    case U_ROTATE: pdf_rotate_page(d, u->page, -u->a); page_changed(v, u->page); pages = 1; break;
    case U_DELPAGE: pdf_restore_page(d, u->page, u->obj); pages = 1; break;
    case U_MOVEPAGE: pdf_move_page(d, u->b, u->a); pages = 1; break;
    case U_INSERT: for (k = u->a - 1; k >= 0; k--) pdf_delete_page(d, u->page + k); pages = 1; break;
    case U_SNAPSHOT: doc_restore(v, u); undo_free(u); return 1;   /* (the document was replaced: nothing else to do) */
    }
    undo_free(u);
  }
  v->an_page = -1;
  if (pages) doc_pages_changed(v);
  doc_changed();
  return 1;
}

/* ------------------------------------------------------------------ comments */
static const char *author(void) { static char u[200]; size_t n = 0, i; for (i = 0; opt.author[i] && n + 3 < sizeof u; i++) n += (size_t)utf8_put(u + n, (u8)opt.author[i]); u[n] = 0; return u; }
/* Latin-1 typed in a dialog, as UTF-8 for the document */
static char *utf8_of(const char *lat)
{
  size_t n = strlen(lat), i, o = 0;
  char *u = (char *)malloc(n * 2 + 1);
  if (!u) return NULL;
  for (i = 0; i < n; i++) o += (size_t)utf8_put(u + o, (u8)lat[i]);
  u[o] = 0;
  return u;
}
static void added(DocView *v, int page)
{
  undo_push(v, U_ADDANNOT, page, annot_count(v->pdf, page) - 1, 0, 0, NULL, NULL);
  page_edited(v, page, 1);
  doc_changed_panes();                  /* (the view has shown it: no need to draw it all again) */
}
void edit_markup(DocView *v, int page, int kind, const double *quads, int nquads)
{
  int t = kind == AN_HIGHLIGHT ? TL_HIGHLIGHT : kind == AN_UNDERLINE ? TL_UNDERLINE : kind == AN_SQUIGGLY ? TL_SQUIGGLY : TL_STRIKE;
  if (annot_markup(v->pdf, page, kind, quads, nquads, opt.color[t], author(), NULL)) added(v, page);
}
void edit_note(DocView *v, int page, double x, double y)
{
  char buf[1024] = "";
  char *u;
  if (!ask_text("Sticky Note", "The comment:", buf, sizeof buf)) return;
  if (!(u = utf8_of(buf))) return;
  if (annot_note(v->pdf, page, x, y, opt.color[TL_NOTE], author(), u)) added(v, page);
  free(u);
}
void edit_textbox(DocView *v, int page, const double *rect)
{
  char buf[1024] = "";
  char *u;
  double r[4], need;
  int lines = 1, len = 0, i;
  if (!ask_text("Add Text", "The text:", buf, sizeof buf) || !buf[0]) return;
  /* tall enough for the text (Helvetica averages about half an em) */
  memcpy(r, rect, sizeof r);
  for (i = 0; buf[i]; i++) { if (buf[i] == '\n') { lines++; len = 0; } else if (++len * opt.text_size * 0.5 > r[2] - r[0] - 4) { lines++; len = 0; } }
  need = lines * opt.text_size * 1.15 + 6;
  if (r[3] - r[1] < need) r[1] = r[3] - need;
  if (!(u = utf8_of(buf))) return;
  if (annot_freetext(v->pdf, page, r, u, opt.text_size, opt.color[TL_TEXTBOX], author())) added(v, page);
  free(u);
}
void edit_ink(DocView *v, int page, const double *pts, int npts)
{
  if (annot_ink(v->pdf, page, pts, &npts, 1, opt.width, opt.color[TL_INK], author())) added(v, page);
}
void edit_shape(DocView *v, int page, int kind, const double *c)
{
  int t = kind == AN_SQUARE ? TL_RECT : kind == AN_CIRCLE ? TL_OVAL : TL_LINE;
  if (annot_shape(v->pdf, page, kind, c, opt.width, opt.color[t], author())) added(v, page);
}
void edit_delete_annot(DocView *v)
{
  int page = v->an_page, index = v->an_index, num;
  if (page < 0 || index < 0) return;
  num = annot_objnum(v->pdf, page, index);
  if (!annot_delete(v->pdf, page, index)) return;
  if (num) undo_push(v, U_DELANNOT, page, index, num, 0, NULL, NULL);
  v->an_page = v->an_index = -1;
  page_edited(v, page, 0);
  doc_changed_panes();
}
void edit_annot_text(DocView *v)
{
  PAnnot *an;
  int n, k;
  char buf[1024] = "";
  char *old = NULL, *u;
  if (v->an_page < 0) return;
  n = pdf_annots(v->pdf, v->an_page, &an);
  for (k = 0; k < n; k++) if (an[k].index == v->an_index) {
    if (an[k].contents) { to_latin1(an[k].contents, buf, sizeof buf); old = str_dup(an[k].contents); }
    else old = str_dup("");
  }
  pdf_annots_free(an, n);
  if (!old) return;
  if (!ask_text("Edit Comment", "The comment:", buf, sizeof buf) || !(u = utf8_of(buf))) { free(old); return; }
  if (annot_set_text(v->pdf, v->an_page, v->an_index, u)) {
    undo_push(v, U_ANNOTTEXT, v->an_page, v->an_index, 0, 0, NULL, old);
    page_changed(v, v->an_page);
    doc_changed();
  }
  free(u); free(old);
}

/* ------------------------------------------------------------------ pages */
/* the pages a page command works on: those picked (organizing), else the one in view */
static int target_pages(DocView *v, int *out)
{
  int i, n = 0;
  if (v->mode == VM_ORGANIZE && v->picked) for (i = 0; i < v->pdf->npages; i++) if (v->picked[i]) out[n++] = i;
  if (!n) out[n++] = v->page;
  return n;
}
void edit_rotate(DocView *v, int quarter)
{
  int *p = (int *)malloc((size_t)(v->pdf->npages + 1) * sizeof(int)), n, i;
  if (!p) return;
  n = target_pages(v, p);
  undo_begin();
  for (i = 0; i < n; i++) {
    pdf_rotate_page(v->pdf, p[i], quarter);
    undo_push(v, U_ROTATE, p[i], 0, quarter, 0, NULL, NULL);
    page_changed(v, p[i]);
  }
  undo_end();
  free(p);
  DoMethod(view, MUIM_View_Refresh);
  doc_changed();
}
void edit_delete_pages(DocView *v)
{
  int *p = (int *)malloc((size_t)(v->pdf->npages + 1) * sizeof(int)), n, i;
  char msg[120];
  if (!p) return;
  n = target_pages(v, p);
  if (n >= v->pdf->npages) { free(p); ask("OK", "%s", tr("A document needs at least one page: pick fewer to delete.")); return; }
  if (n == 1) sprintf(msg, tr("Delete page %d?"), p[0] + 1);
  else sprintf(msg, tr("Delete the %d pages picked?"), n);
  if (ask("Delete|Cancel", "%s", msg) != 1) { free(p); return; }
  undo_begin();
  for (i = n - 1; i >= 0; i--) {
    PObj *pg = v->pdf->pages[p[i]];
    if (pdf_delete_page(v->pdf, p[i])) undo_push(v, U_DELPAGE, p[i], 0, 0, 0, pg, NULL);
  }
  undo_end();
  free(p);
  doc_pages_changed(v);
  doc_changed();
}
void edit_move(DocView *v, int from, int to)
{
  if (!pdf_move_page(v->pdf, from, to)) return;
  undo_push(v, U_MOVEPAGE, 0, 0, from, to, NULL, NULL);
  doc_pages_changed(v);
  if (v->picked) v->picked[to] = 1;
  v->page = to;
  doc_changed();
}
void edit_insert_blank(DocView *v, int at)
{
  double w, h;
  if (!dlg_blank(&w, &h)) return;
  if (!pdf_insert_blank(v->pdf, at, w, h)) return;
  undo_push(v, U_INSERT, at, 0, 1, 0, NULL, NULL);
  doc_pages_changed(v);
  v->page = at;
  DoMethod(view, MUIM_View_GoPage, at, -1);
  doc_changed();
}
void edit_insert_file(DocView *v, int at)
{
  static char path[512];
  PDoc *src;
  int enc, n;
  if (!path[0]) strcpy(path, v->path);
  if (!file_request(0, "Insert the pages of", path, sizeof path, "#?.pdf")) return;
  set(app, MUIA_Application_Sleep, TRUE);
  src = open_pdf(path, &enc);
  if (!src) { set(app, MUIA_Application_Sleep, FALSE); return; }
  n = src->npages;
  if (n > 0 && pdf_insert_pages(v->pdf, at, src, 0, n - 1)) {
    undo_push(v, U_INSERT, at, 0, n, 0, NULL, NULL);
    doc_pages_changed(v);
    v->page = at;
  } else ask("OK", "%s", tr("The pages couldn't be inserted (out of memory?)."));
  pdf_close(src);
  set(app, MUIA_Application_Sleep, FALSE);
  DoMethod(view, MUIM_View_GoPage, at, -1);
  doc_changed();
}

/* ------------------------------------------------------------------ the selection as quads */
int selection_quads(DocView *v, double **out)
{
  TextPage *t = v->sel_page >= 0 ? page_text(v, v->sel_page) : NULL;
  double *q;
  int n = 0, k, a, b;
  *out = NULL;
  if (!t || v->sel_a < 0) return 0;
  a = v->sel_a; b = v->sel_b < t->no ? v->sel_b : t->no - 1;
  if (!(q = (double *)malloc((size_t)(b - a + 2) * 8 * sizeof(double)))) return 0;
  k = a;
  while (k <= b) {
    double x0, y0, x1, y1;
    int s, e;
    /* a run on one line: up to a line end, or a jump back or down */
    while (k <= b && (t->o[k].u == 10 || t->o[k].u == 32)) k++;
    if (k > b) break;
    s = e = k;
    x0 = t->o[k].x0; y0 = t->o[k].y0; x1 = t->o[k].x1; y1 = t->o[k].y1;
    for (k++; k <= b && t->o[k].u != 10; k++) {
      TChar *c = &t->o[k];
      if (c->dir != t->o[s].dir || fabs((c->y0 + c->y1) / 2 - (y0 + y1) / 2) > (y1 - y0) * 0.6) break;
      if (c->u != 32) {
        e = k;
        if (c->x0 < x0) x0 = c->x0;
        if (c->x1 > x1) x1 = c->x1;
        if (c->y0 < y0) y0 = c->y0;
        if (c->y1 > y1) y1 = c->y1;
      }
    }
    (void)e;
    q[n * 8 + 0] = x0; q[n * 8 + 1] = y1; q[n * 8 + 2] = x1; q[n * 8 + 3] = y1;
    q[n * 8 + 4] = x0; q[n * 8 + 5] = y0; q[n * 8 + 6] = x1; q[n * 8 + 7] = y0;
    n++;
  }
  if (!n) { free(q); return 0; }
  *out = q;
  return n;
}
