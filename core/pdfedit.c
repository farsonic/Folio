/*
 * pdfedit.c - changes made to a document in memory (see pdfedit.h). New objects go into the
 * document's cross-reference table (pdf_new_object) and changed ones are changed where they are
 * cached, so pdf_save writes whatever the document now holds.
 *
 * Every annotation Folio makes carries its own appearance stream (a form whose bounding box is the
 * annotation's rectangle, drawn in page coordinates), so other viewers show it the same way.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "pdfedit.h"

/* ------------------------------------------------------------------ helpers */
static PObj *mkref(int num)
{
  PObj *r = pobj_new(PO_REF);
  if (r) { r->u.ref.num = num; r->u.ref.gen = 0; }
  return r;
}
static PObj *mkarr(const double *v, int n)
{
  PObj *a = pobj_new(PO_ARRAY);
  int i;
  for (i = 0; a && i < n; i++) pobj_array_add(a, v[i] == floor(v[i]) && fabs(v[i]) < 1e9 ? pobj_int((long)v[i]) : pobj_real(v[i]));
  return a;
}
static PObj *mkcolor(u32 c)
{
  double v[3];
  v[0] = ((c >> 16) & 255) / 255.0; v[1] = ((c >> 8) & 255) / 255.0; v[2] = (c & 255) / 255.0;
  return mkarr(v, 3);
}
/* a UTF-8 string as a PDF text string: as it is when plain ASCII, otherwise UTF-16BE with its mark */
static PObj *mktext(const char *s)
{
  size_t n = s ? strlen(s) : 0, i, off = 0;
  int ascii = 1, used;
  Buf b;
  PObj *o;
  for (i = 0; i < n; i++) if ((u8)s[i] >= 128) { ascii = 0; break; }
  if (ascii) return pobj_string(s ? s : "", (int)n);
  buf_init(&b);
  buf_addc(&b, 0xFE); buf_addc(&b, 0xFF);
  while (off < n) {
    u32 c = utf8_get(s + off, n - off, &used);
    off += (size_t)(used > 0 ? used : 1);
    if (c >= 0x10000) {
      u32 v = c - 0x10000, hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
      buf_addc(&b, (int)(hi >> 8)); buf_addc(&b, (int)(hi & 255)); buf_addc(&b, (int)(lo >> 8)); buf_addc(&b, (int)(lo & 255));
    } else { buf_addc(&b, (int)(c >> 8)); buf_addc(&b, (int)(c & 255)); }
  }
  o = pobj_string(b.data ? (const char *)b.data : "", (int)b.len);
  buf_free(&b);
  return o;
}
static PObj *mkdate(void)
{
  char t[40];
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  if (tm) snprintf(t, sizeof t, "D:%04d%02d%02d%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  else strcpy(t, "D:20000101000000");
  return pobj_string(t, (int)strlen(t));
}
/* the object number a cached object has (0 if it is not an indirect object) */
static int num_of(PDoc *d, PObj *o)
{
  int k;
  if (!o) return 0;
  for (k = 1; k < d->nxref; k++) if (d->xref[k].obj == o) return k;
  return 0;
}
static void put_num(Buf *b, double v)
{
  char t[40];
  int n;
  if (fabs(v) < 1e-6) v = 0;
  n = snprintf(t, sizeof t, "%.3f", v);
  while (n > 0 && t[n - 1] == '0') t[--n] = 0;
  if (n > 0 && t[n - 1] == '.') t[--n] = 0;
  buf_adds(b, t);
  buf_addc(b, ' ');
}
static void put_rgb(Buf *b, u32 c, int stroke)
{
  put_num(b, ((c >> 16) & 255) / 255.0); put_num(b, ((c >> 8) & 255) / 255.0); put_num(b, (c & 255) / 255.0);
  buf_adds(b, stroke ? "RG\n" : "rg\n");
}
/* a form XObject of the content in b with bounding box bb and resources res (taken; may be NULL); its number */
static int mkform(PDoc *d, Buf *b, const double *bb, PObj *res)
{
  PObj *s = pobj_new(PO_STREAM);
  int n;
  if (!s || b->oom) { pobj_free(s); pobj_free(res); return 0; }
  pobj_dict_set(s, "Type", pobj_name("XObject"));
  pobj_dict_set(s, "Subtype", pobj_name("Form"));
  pobj_dict_set(s, "BBox", mkarr(bb, 4));
  pobj_dict_set(s, "Resources", res ? res : pobj_new(PO_DICT));
  s->slen = (long)b->len;
  if (!(s->sdata = (u8 *)malloc(b->len ? b->len : 1))) { pobj_free(s); return 0; }
  if (b->len) memcpy(s->sdata, b->data, b->len);
  n = pdf_new_object(d, s);
  if (n) s->onum = n;
  return n;
}
static void set_ap(PDoc *d, PObj *annot, int form)
{
  PObj *ap = pobj_new(PO_DICT);
  if (!ap) return;
  pobj_dict_set(ap, "N", mkref(form));
  pobj_dict_set(annot, "AP", ap);
}
static PObj *page_of(PDoc *d, int page) { return page >= 0 && page < d->npages ? d->pages[page] : NULL; }
/* the page's /Annots array (made if wanted) */
static PObj *page_annots(PDoc *d, PObj *pg, int create)
{
  PObj *a = pdf_resolve(d, pobj_dict_get(pg, "Annots"));
  if (a && a->type == PO_ARRAY) return a;
  if (!create) return NULL;
  if (!(a = pobj_new(PO_ARRAY))) return NULL;
  pobj_dict_set(pg, "Annots", a);
  return a;
}
/* a new annotation dictionary of the subtype, with the parts all Folio's annotations have */
static PObj *new_annot(PDoc *d, int page, const char *subtype, const double *rect, u32 color, const char *author, const char *text)
{
  PObj *a = pobj_new(PO_DICT);
  PObj *pg = page_of(d, page);
  int pn = num_of(d, pg);
  double r[4];
  if (!a) return NULL;
  r[0] = rect[0] < rect[2] ? rect[0] : rect[2]; r[2] = rect[0] < rect[2] ? rect[2] : rect[0];
  r[1] = rect[1] < rect[3] ? rect[1] : rect[3]; r[3] = rect[1] < rect[3] ? rect[3] : rect[1];
  pobj_dict_set(a, "Type", pobj_name("Annot"));
  pobj_dict_set(a, "Subtype", pobj_name(subtype));
  pobj_dict_set(a, "Rect", mkarr(r, 4));
  pobj_dict_set(a, "F", pobj_int(4));
  if (pn) pobj_dict_set(a, "P", mkref(pn));
  if (color != 0xFFFFFFFFu) pobj_dict_set(a, "C", mkcolor(color));
  if (author && *author) pobj_dict_set(a, "T", mktext(author));
  if (text) pobj_dict_set(a, "Contents", mktext(text));
  pobj_dict_set(a, "M", mkdate());
  pobj_dict_set(a, "CreationDate", mkdate());
  return a;
}
/* the annotation added to its page */
static int add_annot(PDoc *d, int page, PObj *a)
{
  PObj *pg = page_of(d, page), *arr;
  int n;
  if (!pg || !a || !(arr = page_annots(d, pg, 1))) { pobj_free(a); return 0; }
  if (!(n = pdf_new_object(d, a))) return 0;
  pobj_array_add(arr, mkref(n));
  d->changed = 1;
  return 1;
}
static PObj *gs_res(double alpha, int multiply)
{
  PObj *res = pobj_new(PO_DICT), *eg = pobj_new(PO_DICT), *g = pobj_new(PO_DICT);
  if (!res || !eg || !g) { pobj_free(res); pobj_free(eg); pobj_free(g); return NULL; }
  pobj_dict_set(g, "Type", pobj_name("ExtGState"));
  if (multiply) pobj_dict_set(g, "BM", pobj_name("Multiply"));
  pobj_dict_set(g, "CA", pobj_real(alpha));
  pobj_dict_set(g, "ca", pobj_real(alpha));
  pobj_dict_set(eg, "GS0", g);
  pobj_dict_set(res, "ExtGState", eg);
  return res;
}

/* ------------------------------------------------------------------ links */
int pdf_links(PDoc *d, int page, PLink **out)
{
  PObj *pg = page_of(d, page), *arr;
  PLink *l = NULL;
  int n = 0, i;
  *out = NULL;
  if (!pg || !(arr = page_annots(d, pg, 0))) return 0;
  if (!(l = (PLink *)calloc((size_t)arr->u.arr.n + 1, sizeof(PLink)))) return 0;
  for (i = 0; i < arr->u.arr.n; i++) {
    PObj *a = pdf_dict_of(d, arr->u.arr.v[i]), *rect, *dest, *act;
    PLink *k = &l[n];
    int j;
    if (!a || !pobj_is_name(pdf_dget(d, a, "Subtype"), "Link")) continue;
    rect = pdf_dget(d, a, "Rect");
    if (!rect || rect->type != PO_ARRAY || rect->u.arr.n < 4) continue;
    for (j = 0; j < 4; j++) k->rect[j] = pobj_num(pdf_resolve(d, rect->u.arr.v[j]));
    if (k->rect[0] > k->rect[2]) { double t = k->rect[0]; k->rect[0] = k->rect[2]; k->rect[2] = t; }
    if (k->rect[1] > k->rect[3]) { double t = k->rect[1]; k->rect[1] = k->rect[3]; k->rect[3] = t; }
    k->page = -1; k->top = -1;
    dest = pdf_dget(d, a, "Dest");
    act = pdf_dget(d, a, "A");
    if (!dest && act) {
      const char *s = pdf_dname(d, act, "S");
      if (s && !strcmp(s, "GoTo")) dest = pdf_dget(d, act, "D");
      else if (s && !strcmp(s, "URI")) {
        PObj *u = pdf_dget(d, act, "URI");
        if (u && u->type == PO_STRING) k->uri = str_ndup(u->u.str.s, (size_t)u->u.str.len);
      }
    }
    if (dest) pdf_dest(d, dest, &k->page, &k->top);
    if (k->page >= 0 || k->uri) n++;
  }
  if (!n) { free(l); return 0; }
  *out = l;
  return n;
}
void pdf_links_free(PLink *l, int n)
{
  int i;
  if (!l) return;
  for (i = 0; i < n; i++) free(l[i].uri);
  free(l);
}

/* ------------------------------------------------------------------ listing annotations */
static const struct { const char *name; int kind; } kinds[] = {
  { "Highlight", AN_HIGHLIGHT }, { "Underline", AN_UNDERLINE }, { "StrikeOut", AN_STRIKE }, { "Squiggly", AN_SQUIGGLY },
  { "Text", AN_NOTE }, { "FreeText", AN_FREETEXT }, { "Ink", AN_INK }, { "Square", AN_SQUARE }, { "Circle", AN_CIRCLE },
  { "Line", AN_LINE }, { "Link", AN_LINK }, { "Widget", AN_WIDGET }, { "Stamp", AN_STAMP }
};
static int kind_of(PDoc *d, PObj *a)
{
  const char *s = pdf_dname(d, a, "Subtype");
  int i;
  if (!s) return AN_OTHER;
  for (i = 0; i < (int)(sizeof kinds / sizeof kinds[0]); i++) if (!strcmp(s, kinds[i].name)) return kinds[i].kind;
  if (!strcmp(s, "Popup")) return -1;
  return AN_OTHER;
}
/* the colour in a /DA string's "r g b rg" or "g g" (free text) */
static u32 da_color(const char *s, int len, double *size)
{
  double v[4];
  int nv = 0, i = 0;
  u32 col = 0;
  *size = 12;
  while (i < len) {
    char tok[32];
    int k = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
    while (i < len && !(s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') && k < 31) tok[k++] = s[i++];
    while (i < len && !(s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
    tok[k] = 0;
    if (!k) break;
    if ((tok[0] >= '0' && tok[0] <= '9') || tok[0] == '.' || tok[0] == '-') { if (nv == 4) { memmove(v, v + 1, 3 * sizeof(double)); nv = 3; } v[nv++] = atof(tok); }
    else {
      if (!strcmp(tok, "rg") && nv >= 3) col = ((u32)(v[nv - 3] * 255 + 0.5) << 16) | ((u32)(v[nv - 2] * 255 + 0.5) << 8) | (u32)(v[nv - 1] * 255 + 0.5);
      else if (!strcmp(tok, "g") && nv >= 1) { u32 g = (u32)(v[nv - 1] * 255 + 0.5); col = g << 16 | g << 8 | g; }
      else if (!strcmp(tok, "Tf") && nv >= 1) *size = v[nv - 1];
      nv = 0;
    }
  }
  return col & 0xFFFFFF;
}
static char *nice_date(PObj *s)
{
  char t[40];
  const char *p;
  if (!s || s->type != PO_STRING || s->u.str.len < 10) return NULL;
  p = s->u.str.s;
  if (p[0] == 'D' && p[1] == ':') p += 2;
  if ((int)(p - s->u.str.s) + 12 <= s->u.str.len) snprintf(t, sizeof t, "%.4s-%.2s-%.2s %.2s:%.2s", p, p + 4, p + 6, p + 8, p + 10);
  else snprintf(t, sizeof t, "%.4s-%.2s-%.2s", p, p + 4, p + 6);
  return str_dup(t);
}
int pdf_annots(PDoc *d, int page, PAnnot **out)
{
  PObj *pg = page_of(d, page), *arr;
  PAnnot *l;
  int n = 0, i;
  *out = NULL;
  if (!pg || !(arr = page_annots(d, pg, 0))) return 0;
  if (!(l = (PAnnot *)calloc((size_t)arr->u.arr.n + 1, sizeof(PAnnot)))) return 0;
  for (i = 0; i < arr->u.arr.n; i++) {
    PObj *a = pdf_dict_of(d, arr->u.arr.v[i]), *rect, *c, *s;
    PAnnot *k = &l[n];
    int j, kind;
    if (!a || (kind = kind_of(d, a)) < 0 || kind == AN_LINK) continue;
    k->index = i; k->kind = kind;
    rect = pdf_dget(d, a, "Rect");
    if (rect && rect->type == PO_ARRAY && rect->u.arr.n >= 4) for (j = 0; j < 4; j++) k->rect[j] = pobj_num(pdf_resolve(d, rect->u.arr.v[j]));
    if (k->rect[0] > k->rect[2]) { double t = k->rect[0]; k->rect[0] = k->rect[2]; k->rect[2] = t; }
    if (k->rect[1] > k->rect[3]) { double t = k->rect[1]; k->rect[1] = k->rect[3]; k->rect[3] = t; }
    k->color = 0xFFFFFFFFu;
    c = pdf_dget(d, a, "C");
    if (c && c->type == PO_ARRAY) {
      double v[4] = { 0, 0, 0, 0 };
      for (j = 0; j < c->u.arr.n && j < 4; j++) v[j] = pobj_num(pdf_resolve(d, c->u.arr.v[j]));
      if (c->u.arr.n == 1) v[1] = v[2] = v[0];
      if (c->u.arr.n == 4) { double kk = v[3]; v[0] = (1 - v[0]) * (1 - kk); v[1] = (1 - v[1]) * (1 - kk); v[2] = (1 - v[2]) * (1 - kk); }
      if (c->u.arr.n) k->color = ((u32)(v[0] * 255 + 0.5) << 16) | ((u32)(v[1] * 255 + 0.5) << 8) | (u32)(v[2] * 255 + 0.5);
    }
    if (kind == AN_FREETEXT && (s = pdf_dget(d, a, "DA")) && s->type == PO_STRING) { double sz; k->color = da_color(s->u.str.s, s->u.str.len, &sz); }
    if ((s = pdf_dget(d, a, "Contents")) && s->type == PO_STRING) k->contents = pdf_text_string(s);
    if ((s = pdf_dget(d, a, "T")) && s->type == PO_STRING) k->author = pdf_text_string(s);
    k->date = nice_date(pdf_dget(d, a, "M"));
    if (!k->date) k->date = nice_date(pdf_dget(d, a, "CreationDate"));
    n++;
  }
  if (!n) { free(l); return 0; }
  *out = l;
  return n;
}
void pdf_annots_free(PAnnot *a, int n)
{
  int i;
  if (!a) return;
  for (i = 0; i < n; i++) { free(a[i].contents); free(a[i].author); free(a[i].date); }
  free(a);
}

/* ------------------------------------------------------------------ making annotations */
int annot_markup(PDoc *d, int page, int kind, const double *q, int nq, u32 color, const char *author, const char *text)
{
  static const char *const names[] = { "Highlight", "Underline", "StrikeOut", "Squiggly" };
  double r[4], bb[4];
  PObj *a, *res = NULL;
  Buf b;
  int i, j, form;
  if (nq < 1 || kind < AN_HIGHLIGHT || kind > AN_SQUIGGLY) return 0;
  r[0] = r[2] = q[0]; r[1] = r[3] = q[1];
  for (i = 0; i < nq * 4; i++) {
    double x = q[i * 2], y = q[i * 2 + 1];
    if (x < r[0]) r[0] = x;
    if (x > r[2]) r[2] = x;
    if (y < r[1]) r[1] = y;
    if (y > r[3]) r[3] = y;
  }
  bb[0] = r[0] - 2; bb[1] = r[1] - 2; bb[2] = r[2] + 2; bb[3] = r[3] + 2;
  if (!(a = new_annot(d, page, names[kind - AN_HIGHLIGHT], bb, color, author, text))) return 0;
  {
    PObj *qp = mkarr(q, nq * 8);
    pobj_dict_set(a, "QuadPoints", qp);
  }
  buf_init(&b);
  if (kind == AN_HIGHLIGHT) {
    res = gs_res(1.0, 1);
    buf_adds(&b, "/GS0 gs\n");
    put_rgb(&b, color, 0);
    for (i = 0; i < nq; i++) {
      const double *p = q + i * 8;
      put_num(&b, p[0]); put_num(&b, p[1]); buf_adds(&b, "m ");
      put_num(&b, p[2]); put_num(&b, p[3]); buf_adds(&b, "l ");
      put_num(&b, p[6]); put_num(&b, p[7]); buf_adds(&b, "l ");
      put_num(&b, p[4]); put_num(&b, p[5]); buf_adds(&b, "l h\n");
    }
    buf_adds(&b, "f\n");
  } else {
    put_rgb(&b, color, 1);
    for (i = 0; i < nq; i++) {
      const double *p = q + i * 8;
      double h = fabs(p[1] - p[5]), w = h / 14 < 0.6 ? 0.6 : h / 14;
      double y = kind == AN_STRIKE ? (p[1] + p[5]) / 2 : p[5] + h * 0.08, x0 = p[4], x1 = p[6];
      if (h < 1) h = 1;
      if (kind == AN_SQUIGGLY) {
        double step = h / 6, amp = h / 14 + 0.5, x;
        put_num(&b, w * 0.8); buf_adds(&b, "w\n");
        put_num(&b, x0); put_num(&b, y); buf_adds(&b, "m\n");
        for (x = x0 + step, j = 1; x <= x1 + 0.01; x += step, j++) { put_num(&b, x); put_num(&b, y + ((j & 1) ? amp : 0)); buf_adds(&b, "l\n"); }
        buf_adds(&b, "S\n");
      } else {
        put_num(&b, w); buf_adds(&b, "w\n");
        put_num(&b, x0); put_num(&b, y); buf_adds(&b, "m ");
        put_num(&b, x1); put_num(&b, y); buf_adds(&b, "l S\n");
      }
    }
  }
  form = mkform(d, &b, bb, res);
  buf_free(&b);
  if (form) set_ap(d, a, form);
  return add_annot(d, page, a);
}
int annot_note(PDoc *d, int page, double x, double y, u32 color, const char *author, const char *text)
{
  double r[4];
  PObj *a;
  Buf b;
  int form;
  r[0] = x; r[1] = y - 20; r[2] = x + 20; r[3] = y;
  if (!(a = new_annot(d, page, "Text", r, color, author, text))) return 0;
  pobj_dict_set(a, "Name", pobj_name("Comment"));
  pobj_dict_set(a, "Open", pobj_new(PO_BOOL));
  /* a speech bubble */
  buf_init(&b);
  put_rgb(&b, color, 0);
  buf_adds(&b, "0 G 0.8 w 1 j\n");
  buf_printf(&b, "%g %g m %g %g l %g %g l %g %g l %g %g l %g %g l %g %g l h B\n",
             x + 2, y - 2, x + 18, y - 2, x + 18, y - 14, x + 9, y - 14, x + 5, y - 18, x + 5.5, y - 14, x + 2, y - 14);
  buf_printf(&b, "0 g 0.6 w %g %g m %g %g l S %g %g m %g %g l S %g %g m %g %g l S\n",
             x + 5, y - 5.5, x + 15, y - 5.5, x + 5, y - 8, x + 15, y - 8, x + 5, y - 10.5, x + 12, y - 10.5);
  form = mkform(d, &b, r, NULL);
  buf_free(&b);
  if (form) set_ap(d, a, form);
  return add_annot(d, page, a);
}

/* Helvetica's widths (thousandths of an em), space to tilde */
static const short helv_w[95] = {
  278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
  556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
  667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556,
  556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584
};
static double helv_width(const char *s, int n, double size)
{
  double w = 0;
  int i;
  for (i = 0; i < n; i++) { u8 c = (u8)s[i]; w += c >= 32 && c < 127 ? helv_w[c - 32] : 556; }
  return w * size / 1000;
}
static void put_pdfstr(Buf *b, const char *s, int n)
{
  int i;
  buf_addc(b, '(');
  for (i = 0; i < n; i++) {
    u8 c = (u8)s[i];
    if (c == '(' || c == ')' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
    else if (c < 32) buf_printf(b, "\\%03o", c);
    else buf_addc(b, c);
  }
  buf_addc(b, ')');
}
/* a free text annotation's appearance: the text wrapped to the rectangle's width */
static int freetext_form(PDoc *d, const double *r, const char *text, double size, u32 color)
{
  size_t n = text ? strlen(text) : 0;
  char *lat = (char *)malloc(n * 3 + 4);
  Buf b;
  PObj *res, *fonts, *helv;
  double maxw = r[2] - r[0] - 4, y = r[3] - 2 - size * 0.9;
  int len, start = 0, form;
  if (!lat) return 0;
  len = (int)utf8_to_latin1(text ? text : "", n, lat, n * 3 + 4);
  buf_init(&b);
  buf_adds(&b, "/Tx BMC\nq BT\n/Helv ");
  put_num(&b, size); buf_adds(&b, "Tf\n");
  put_rgb(&b, color, 0);
  while (start < len && y > r[1] - size) {
    int end = start, last_space = -1, i;
    /* as many words as fit; a hard line break ends the line */
    for (i = start; i < len && lat[i] != '\n'; i++) {
      if (lat[i] == ' ') last_space = i;
      if (helv_width(lat + start, i - start + 1, size) > maxw && i > start) break;
    }
    end = i;
    if (i < len && lat[i] != '\n' && last_space > start) end = last_space;
    buf_adds(&b, "1 0 0 1 "); put_num(&b, r[0] + 2); put_num(&b, y); buf_adds(&b, "Tm ");
    put_pdfstr(&b, lat + start, end - start);
    buf_adds(&b, " Tj\n");
    start = end;
    if (start < len && (lat[start] == ' ' || lat[start] == '\n')) start++;
    y -= size * 1.15;
  }
  buf_adds(&b, "ET Q\nEMC\n");
  free(lat);
  res = pobj_new(PO_DICT); fonts = pobj_new(PO_DICT); helv = pobj_new(PO_DICT);
  if (res && fonts && helv) {
    pobj_dict_set(helv, "Type", pobj_name("Font"));
    pobj_dict_set(helv, "Subtype", pobj_name("Type1"));
    pobj_dict_set(helv, "BaseFont", pobj_name("Helvetica"));
    pobj_dict_set(helv, "Encoding", pobj_name("WinAnsiEncoding"));
    pobj_dict_set(fonts, "Helv", helv);
    pobj_dict_set(res, "Font", fonts);
  } else { pobj_free(res); pobj_free(fonts); pobj_free(helv); res = NULL; }
  form = mkform(d, &b, r, res);
  buf_free(&b);
  return form;
}
static PObj *freetext_da(double size, u32 color)
{
  char t[96];
  snprintf(t, sizeof t, "/Helv %.2f Tf %.3f %.3f %.3f rg", size, ((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0, (color & 255) / 255.0);
  return pobj_string(t, (int)strlen(t));
}
int annot_freetext(PDoc *d, int page, const double *rect, const char *text, double size, u32 color, const char *author)
{
  PObj *a;
  int form;
  double r[4];
  if (size < 4) size = 4;
  if (!(a = new_annot(d, page, "FreeText", rect, 0xFFFFFFFFu, author, text))) return 0;
  {
    PObj *ro = pdf_dget(d, a, "Rect");
    int j;
    for (j = 0; j < 4; j++) r[j] = pobj_num(ro->u.arr.v[j]);
  }
  pobj_dict_set(a, "DA", freetext_da(size, color));
  {
    PObj *bs = pobj_new(PO_DICT);
    if (bs) { pobj_dict_set(bs, "W", pobj_int(0)); pobj_dict_set(a, "BS", bs); }
  }
  if ((form = freetext_form(d, r, text, size, color))) set_ap(d, a, form);
  return add_annot(d, page, a);
}
int annot_ink(PDoc *d, int page, const double *pts, const int *npts, int ns, double width, u32 color, const char *author)
{
  double r[4];
  PObj *a, *list;
  Buf b;
  int i, j, k = 0, form, total = 0;
  for (i = 0; i < ns; i++) total += npts[i];
  if (ns < 1 || total < 1) return 0;
  r[0] = r[2] = pts[0]; r[1] = r[3] = pts[1];
  for (i = 0; i < total; i++) {
    double x = pts[i * 2], y = pts[i * 2 + 1];
    if (x < r[0]) r[0] = x;
    if (x > r[2]) r[2] = x;
    if (y < r[1]) r[1] = y;
    if (y > r[3]) r[3] = y;
  }
  r[0] -= width + 1; r[1] -= width + 1; r[2] += width + 1; r[3] += width + 1;
  if (!(a = new_annot(d, page, "Ink", r, color, author, NULL))) return 0;
  if ((list = pobj_new(PO_ARRAY))) {
    for (i = 0; i < ns; i++) pobj_array_add(list, mkarr(pts + k * 2, npts[i] * 2)), k += npts[i];
    pobj_dict_set(a, "InkList", list);
  }
  {
    PObj *bs = pobj_new(PO_DICT);
    if (bs) { pobj_dict_set(bs, "W", pobj_real(width)); pobj_dict_set(a, "BS", bs); }
  }
  buf_init(&b);
  put_rgb(&b, color, 1);
  put_num(&b, width); buf_adds(&b, "w 1 J 1 j\n");
  for (i = 0, k = 0; i < ns; i++) {
    for (j = 0; j < npts[i]; j++, k++) {
      put_num(&b, pts[k * 2]); put_num(&b, pts[k * 2 + 1]);
      buf_adds(&b, j ? "l\n" : "m\n");
    }
    if (npts[i] == 1) { put_num(&b, pts[(k - 1) * 2] + 0.01); put_num(&b, pts[(k - 1) * 2 + 1]); buf_adds(&b, "l\n"); }
    buf_adds(&b, "S\n");
  }
  form = mkform(d, &b, r, NULL);
  buf_free(&b);
  if (form) set_ap(d, a, form);
  return add_annot(d, page, a);
}
int annot_shape(PDoc *d, int page, int kind, const double *c, double width, u32 color, const char *author)
{
  double r[4], hw = width / 2 + 1;
  PObj *a;
  Buf b;
  int form;
  const char *sub = kind == AN_SQUARE ? "Square" : kind == AN_CIRCLE ? "Circle" : kind == AN_LINE ? "Line" : NULL;
  if (!sub) return 0;
  r[0] = (c[0] < c[2] ? c[0] : c[2]) - hw; r[2] = (c[0] < c[2] ? c[2] : c[0]) + hw;
  r[1] = (c[1] < c[3] ? c[1] : c[3]) - hw; r[3] = (c[1] < c[3] ? c[3] : c[1]) + hw;
  if (!(a = new_annot(d, page, sub, r, color, author, NULL))) return 0;
  {
    PObj *bs = pobj_new(PO_DICT);
    if (bs) { pobj_dict_set(bs, "W", pobj_real(width)); pobj_dict_set(a, "BS", bs); }
  }
  if (kind == AN_LINE) pobj_dict_set(a, "L", mkarr(c, 4));
  else {
    double rd[4];
    rd[0] = rd[1] = rd[2] = rd[3] = hw;
    pobj_dict_set(a, "RD", mkarr(rd, 4));
  }
  buf_init(&b);
  put_rgb(&b, color, 1);
  put_num(&b, width); buf_adds(&b, "w 1 J 1 j\n");
  if (kind == AN_LINE) {
    put_num(&b, c[0]); put_num(&b, c[1]); buf_adds(&b, "m ");
    put_num(&b, c[2]); put_num(&b, c[3]); buf_adds(&b, "l S\n");
  } else {
    double x0 = r[0] + hw, y0 = r[1] + hw, x1 = r[2] - hw, y1 = r[3] - hw;
    if (kind == AN_SQUARE) {
      put_num(&b, x0); put_num(&b, y0); put_num(&b, x1 - x0); put_num(&b, y1 - y0); buf_adds(&b, "re S\n");
    } else {
      double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = (x1 - x0) / 2, ry = (y1 - y0) / 2, k = 0.5523;
      put_num(&b, cx + rx); put_num(&b, cy); buf_adds(&b, "m\n");
      put_num(&b, cx + rx); put_num(&b, cy + ry * k); put_num(&b, cx + rx * k); put_num(&b, cy + ry); put_num(&b, cx); put_num(&b, cy + ry); buf_adds(&b, "c\n");
      put_num(&b, cx - rx * k); put_num(&b, cy + ry); put_num(&b, cx - rx); put_num(&b, cy + ry * k); put_num(&b, cx - rx); put_num(&b, cy); buf_adds(&b, "c\n");
      put_num(&b, cx - rx); put_num(&b, cy - ry * k); put_num(&b, cx - rx * k); put_num(&b, cy - ry); put_num(&b, cx); put_num(&b, cy - ry); buf_adds(&b, "c\n");
      put_num(&b, cx + rx * k); put_num(&b, cy - ry); put_num(&b, cx + rx); put_num(&b, cy - ry * k); put_num(&b, cx + rx); put_num(&b, cy); buf_adds(&b, "c h S\n");
    }
  }
  form = mkform(d, &b, r, NULL);
  buf_free(&b);
  if (form) set_ap(d, a, form);
  return add_annot(d, page, a);
}
static void array_remove(PObj *arr, int i)
{
  if (i < 0 || i >= arr->u.arr.n) return;
  pobj_free(arr->u.arr.v[i]);
  memmove(arr->u.arr.v + i, arr->u.arr.v + i + 1, (size_t)(arr->u.arr.n - i - 1) * sizeof(PObj *));
  arr->u.arr.n--;
}
int annot_delete(PDoc *d, int page, int index)
{
  PObj *pg = page_of(d, page), *arr, *a, *popup;
  int i;
  if (!pg || !(arr = page_annots(d, pg, 0)) || index < 0 || index >= arr->u.arr.n) return 0;
  a = pdf_dict_of(d, arr->u.arr.v[index]);
  popup = a ? pobj_dict_get(a, "Popup") : NULL;
  /* its popup goes with it */
  if (popup && popup->type == PO_REF)
    for (i = 0; i < arr->u.arr.n; i++) {
      PObj *e = arr->u.arr.v[i];
      if (i != index && e->type == PO_REF && e->u.ref.num == popup->u.ref.num) { array_remove(arr, i); if (i < index) index--; break; }
    }
  array_remove(arr, index);
  d->changed = 1;
  return 1;
}
int annot_set_text(PDoc *d, int page, int index, const char *text)
{
  PObj *pg = page_of(d, page), *arr, *a;
  if (!pg || !(arr = page_annots(d, pg, 0)) || index < 0 || index >= arr->u.arr.n) return 0;
  if (!(a = pdf_dict_of(d, arr->u.arr.v[index]))) return 0;
  pobj_dict_set(a, "Contents", mktext(text ? text : ""));
  pobj_dict_set(a, "M", mkdate());
  /* a text box shows its text: its appearance made again */
  if (kind_of(d, a) == AN_FREETEXT) {
    PObj *ro = pdf_dget(d, a, "Rect"), *da = pdf_dget(d, a, "DA");
    double r[4], size = 12;
    u32 col = 0;
    int j, form;
    if (ro && ro->type == PO_ARRAY && ro->u.arr.n >= 4) {
      for (j = 0; j < 4; j++) r[j] = pobj_num(pdf_resolve(d, ro->u.arr.v[j]));
      if (da && da->type == PO_STRING) col = da_color(da->u.str.s, da->u.str.len, &size);
      if ((form = freetext_form(d, r, text, size, col))) set_ap(d, a, form);
    }
  }
  d->changed = 1;
  return 1;
}

int annot_count(PDoc *d, int page)
{
  PObj *pg = page_of(d, page), *arr = pg ? page_annots(d, pg, 0) : NULL;
  return arr ? arr->u.arr.n : 0;
}
int annot_objnum(PDoc *d, int page, int index)
{
  PObj *pg = page_of(d, page), *arr = pg ? page_annots(d, pg, 0) : NULL;
  if (!arr || index < 0 || index >= arr->u.arr.n || arr->u.arr.v[index]->type != PO_REF) return 0;
  return arr->u.arr.v[index]->u.ref.num;
}
int annot_restore(PDoc *d, int page, int index, int objnum)
{
  PObj *pg = page_of(d, page), *arr = pg ? page_annots(d, pg, 1) : NULL, *r;
  if (!arr || objnum <= 0 || !(r = mkref(objnum))) return 0;
  if (index < 0 || index > arr->u.arr.n) index = arr->u.arr.n;
  pobj_array_add(arr, r);                             /* (grows it; then moved into place) */
  if (arr->u.arr.n - 1 != index) {
    memmove(arr->u.arr.v + index + 1, arr->u.arr.v + index, (size_t)(arr->u.arr.n - 1 - index) * sizeof(PObj *));
    arr->u.arr.v[index] = r;
  }
  d->changed = 1;
  return 1;
}
int annot_at(PDoc *d, int page, double x, double y)
{
  PObj *pg = page_of(d, page), *arr = pg ? page_annots(d, pg, 0) : NULL;
  int i;
  if (!arr) return -1;
  for (i = arr->u.arr.n - 1; i >= 0; i--) {
    PObj *a = pdf_dict_of(d, arr->u.arr.v[i]), *rect;
    double r[4];
    int j, kind;
    if (!a || (kind = kind_of(d, a)) < 0 || kind == AN_LINK || (pdf_dint(d, a, "F", 0) & 2)) continue;
    rect = pdf_dget(d, a, "Rect");
    if (!rect || rect->type != PO_ARRAY || rect->u.arr.n < 4) continue;
    for (j = 0; j < 4; j++) r[j] = pobj_num(pdf_resolve(d, rect->u.arr.v[j]));
    if (r[0] > r[2]) { double t = r[0]; r[0] = r[2]; r[2] = t; }
    if (r[1] > r[3]) { double t = r[1]; r[1] = r[3]; r[3] = t; }
    if (x >= r[0] - 2 && x <= r[2] + 2 && y >= r[1] - 2 && y <= r[3] + 2) return i;
  }
  return -1;
}

/* ------------------------------------------------------------------ pages */
void pdf_rotate_page(PDoc *d, int page, int q)
{
  PObj *pg = page_of(d, page), *r;
  long rot;
  if (!pg) return;
  r = pdf_page_attr(d, pg, "Rotate");
  rot = r ? (long)pobj_num(pdf_resolve(d, r)) : 0;
  rot = ((rot + 90L * q) % 360 + 360) % 360;
  rot -= rot % 90;
  pobj_dict_set(pg, "Rotate", pobj_int(rot));
  d->changed = 1;
}
int pdf_delete_page(PDoc *d, int page)
{
  if (page < 0 || page >= d->npages || d->npages < 2) return 0;
  memmove(d->pages + page, d->pages + page + 1, (size_t)(d->npages - page - 1) * sizeof(PObj *));
  d->npages--;
  d->changed = 1;
  return 1;
}
int pdf_move_page(PDoc *d, int from, int to)
{
  PObj *p;
  if (from < 0 || from >= d->npages || to < 0 || to >= d->npages) return 0;
  if (from == to) return 1;
  p = d->pages[from];
  if (from < to) memmove(d->pages + from, d->pages + from + 1, (size_t)(to - from) * sizeof(PObj *));
  else memmove(d->pages + to + 1, d->pages + to, (size_t)(from - to) * sizeof(PObj *));
  d->pages[to] = p;
  d->changed = 1;
  return 1;
}
/* new pages put into the list before at */
static int pages_insert(PDoc *d, int at, PObj **pg, int n)
{
  PObj **np;
  if (at < 0) at = 0;
  if (at > d->npages) at = d->npages;
  if (!(np = (PObj **)realloc(d->pages, (size_t)(d->npages + n) * sizeof(PObj *)))) return 0;
  d->pages = np;
  memmove(np + at + n, np + at, (size_t)(d->npages - at) * sizeof(PObj *));
  memcpy(np + at, pg, (size_t)n * sizeof(PObj *));
  d->npages += n;
  d->changed = 1;
  return 1;
}
int pdf_restore_page(PDoc *d, int at, PObj *page) { return page ? pages_insert(d, at, &page, 1) : 0; }
int pdf_insert_blank(PDoc *d, int at, double w, double h)
{
  PObj *pg = pobj_new(PO_DICT);
  double box[4];
  if (!pg) return 0;
  box[0] = box[1] = 0; box[2] = w; box[3] = h;
  pobj_dict_set(pg, "Type", pobj_name("Page"));
  pobj_dict_set(pg, "MediaBox", mkarr(box, 4));
  pobj_dict_set(pg, "Resources", pobj_new(PO_DICT));
  if (!pdf_new_object(d, pg)) return 0;
  return pages_insert(d, at, &pg, 1);
}

/* copying objects from another document: each object it refers to gets a number here (a
   placeholder at first) and is copied when its turn in the queue comes */
typedef struct {
  PDoc *d, *s;
  int *map, nmap;           /* source number -> number here */
  int *queue, nq, capq;
  int first, last;          /* the pages being copied (pages outside them become null) */
} Copy;
static PObj *copy_obj(Copy *c, const PObj *o, int depth);
static int copy_ref(Copy *c, int num)
{
  PObj *t;
  int n;
  if (num <= 0 || num >= c->nmap) return 0;
  if (c->map[num]) return c->map[num] > 0 ? c->map[num] : 0;
  t = pdf_get(c->s, num);
  /* a page that isn't being copied (a link's target, say) isn't brought along */
  if (!t || (t->type == PO_DICT && (pobj_is_name(pobj_dict_get(t, "Type"), "Page") || pobj_is_name(pobj_dict_get(t, "Type"), "Pages")))) {
    int idx = t ? pdf_page_index(c->s, t) : -1;
    if (idx < c->first || idx > c->last) { c->map[num] = -1; return 0; }
  }
  if (!(n = pdf_new_object(c->d, pobj_new(PO_NULL)))) { c->map[num] = -1; return 0; }
  c->map[num] = n;
  if (c->nq >= c->capq) {
    int nc = c->capq ? c->capq * 2 : 64, *q = (int *)realloc(c->queue, (size_t)nc * sizeof(int));
    if (!q) return n;
    c->queue = q; c->capq = nc;
  }
  c->queue[c->nq++] = num;
  return n;
}
static PObj *copy_obj(Copy *c, const PObj *o, int depth)
{
  PObj *r;
  int i;
  if (!o || depth > 64) return pobj_new(PO_NULL);
  switch (o->type) {
  case PO_REF: {
    int n = copy_ref(c, o->u.ref.num);
    return n ? mkref(n) : pobj_new(PO_NULL); }
  case PO_ARRAY:
    if (!(r = pobj_new(PO_ARRAY))) return NULL;
    for (i = 0; i < o->u.arr.n; i++) pobj_array_add(r, copy_obj(c, o->u.arr.v[i], depth + 1));
    return r;
  case PO_DICT: case PO_STREAM: {
    int page = o->type == PO_DICT && pobj_is_name(pobj_dict_get(o, "Type"), "Page");
    if (!(r = pobj_new(o->type))) return NULL;
    for (i = 0; i < o->u.dict.n; i++) {
      if (page && !strcmp(o->u.dict.k[i], "Parent")) continue;
      pobj_dict_set(r, o->u.dict.k[i], copy_obj(c, o->u.dict.v[i], depth + 1));
    }
    if (o->type == PO_STREAM) {
      size_t len = 0;
      u8 *data = pdf_stream_bytes(c->s, (PObj *)o, &len);
      r->sdata = data ? data : (u8 *)malloc(1);
      r->slen = data ? (long)len : 0;
      r->soff = -1;
      if (!data) pobj_dict_del(r, "Filter");
    }
    return r; }
  default:
    return pobj_copy(o);
  }
}
int pdf_insert_pages(PDoc *d, int at, PDoc *src, int first, int last)
{
  Copy c;
  PObj **pg;
  int i, n, ok = 0;
  if (!src || first < 0 || last >= src->npages || first > last) return 0;
  n = last - first + 1;
  memset(&c, 0, sizeof c);
  c.d = d; c.s = src; c.first = first; c.last = last;
  c.nmap = src->nxref + 1;
  if (!(c.map = (int *)calloc((size_t)c.nmap, sizeof(int))) || !(pg = (PObj **)calloc((size_t)n, sizeof(PObj *)))) { free(c.map); return 0; }
  /* the pages themselves first, with their inherited attributes made their own */
  for (i = 0; i < n; i++) {
    PObj *sp = src->pages[first + i], *cp;
    static const char *const inh[] = { "Resources", "MediaBox", "CropBox", "Rotate" };
    int a, num = num_of(src, sp), dn;
    if (num) {
      if (!(dn = copy_ref(&c, num))) goto done;
      c.nq--;                                         /* (copied here, not from the queue) */
    } else if (!(dn = pdf_new_object(d, pobj_new(PO_NULL)))) goto done;
    if (!(cp = copy_obj(&c, sp, 0))) goto done;
    for (a = 0; a < 4; a++) if (!pobj_dict_get(cp, inh[a])) { PObj *v = pdf_page_attr(src, sp, inh[a]); if (v) pobj_dict_set(cp, inh[a], copy_obj(&c, v, 0)); }
    pobj_free(d->xref[dn].obj);
    d->xref[dn].obj = cp;
    pg[i] = cp;
  }
  /* then everything they refer to */
  for (i = 0; i < c.nq; i++) {
    int sn = c.queue[i], dn = c.map[sn];
    PObj *cp = copy_obj(&c, pdf_get(src, sn), 0);
    if (!cp) continue;
    if (cp->type == PO_STREAM) cp->onum = dn;
    pobj_free(d->xref[dn].obj);
    d->xref[dn].obj = cp;
  }
  ok = pages_insert(d, at, pg, n);
done:
  free(c.map); free(c.queue); free(pg);
  return ok;
}

/* ------------------------------------------------------------------ document information */
void pdf_set_info(PDoc *d, const char *key, const char *value)
{
  PObj *info = pdf_resolve(d, pobj_dict_get(d->trailer, "Info"));
  if (!info || info->type != PO_DICT) {
    int n;
    if (!(info = pobj_new(PO_DICT)) || !(n = pdf_new_object(d, info))) return;
    pobj_dict_set(d->trailer, "Info", mkref(n));
  }
  if (value && *value) pobj_dict_set(info, key, mktext(value));
  else pobj_dict_del(info, key);
  pobj_dict_set(info, "ModDate", mkdate());
  pobj_dict_set(info, "Producer", pobj_string("Folio", 5));
  d->changed = 1;
}
