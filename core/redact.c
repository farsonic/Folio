/*
 * redact.c - redaction and cleaning (see redact.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "redact.h"
#include "pagecontent.h"
#include "contentedit.h"
#include "forms.h"

void redact_props_default(RedactProps *p)
{
  memset(p, 0, sizeof *p);
  p->fill = 0x000000;
  p->outline = 0xE00000;
}
static int add_annot(PDoc *d, int page, PObj *a)
{
  PObj *pg = pc_page(d, page), *arr;
  int num;
  if (!pg || !a) { pobj_free(a); return 0; }
  arr = pdf_dget(d, pg, "Annots");
  if (!arr || arr->type != PO_ARRAY) { if (!(arr = pobj_new(PO_ARRAY))) { pobj_free(a); return 0; } pobj_dict_set(pg, "Annots", arr); }
  else if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *c = pobj_copy(arr); if (!c) { pobj_free(a); return 0; } pobj_dict_set(pg, "Annots", c); arr = c; }
  if (!(num = pdf_new_object(d, a))) return 0;
  pobj_array_add(arr, om_ref(num));
  d->changed = 1;
  return 1;
}
/* a Redact annotation over rect (and quads), drawn as its outline until applied */
static int mark(PDoc *d, int page, const double *rect, const double *quads, int nq, const RedactProps *p)
{
  PObj *a = pobj_new(PO_DICT);
  double r[4];
  int i, f;
  Buf b;
  RedactProps def;
  if (!p) { redact_props_default(&def); p = &def; }
  if (!a) return 0;
  r[0] = rect[0] < rect[2] ? rect[0] : rect[2]; r[2] = rect[0] < rect[2] ? rect[2] : rect[0];
  r[1] = rect[1] < rect[3] ? rect[1] : rect[3]; r[3] = rect[1] < rect[3] ? rect[3] : rect[1];
  pobj_dict_set(a, "Type", pobj_name("Annot"));
  pobj_dict_set(a, "Subtype", pobj_name("Redact"));
  pobj_dict_set(a, "Rect", om_arr(r, 4));
  pobj_dict_set(a, "F", pobj_int(4));
  pobj_dict_set(a, "C", om_rgb(p->outline));
  if (p->fill != 0xFFFFFFFFu) pobj_dict_set(a, "IC", om_rgb(p->fill));
  if (p->overlay[0]) pobj_dict_set(a, "OverlayText", om_text(p->overlay));
  if (quads && nq > 0) pobj_dict_set(a, "QuadPoints", om_arr(quads, nq * 8));
  pobj_dict_set(a, "M", om_date());
  buf_init(&b);
  om_num(&b, ((p->outline >> 16) & 255) / 255.0); om_num(&b, ((p->outline >> 8) & 255) / 255.0); om_num(&b, (p->outline & 255) / 255.0);
  buf_adds(&b, "RG 1 w\n");
  if (quads && nq > 0) for (i = 0; i < nq; i++) {
    const double *q = quads + i * 8;
    om_num(&b, q[0]); om_num(&b, q[1]); buf_adds(&b, "m "); om_num(&b, q[2]); om_num(&b, q[3]); buf_adds(&b, "l ");
    om_num(&b, q[6]); om_num(&b, q[7]); buf_adds(&b, "l "); om_num(&b, q[4]); om_num(&b, q[5]); buf_adds(&b, "l s\n");
  } else { om_num(&b, r[0] + 0.5); om_num(&b, r[1] + 0.5); om_num(&b, r[2] - r[0] - 1); om_num(&b, r[3] - r[1] - 1); buf_adds(&b, "re S\n"); }
  f = om_form(d, b.data ? b.data : "", b.len, r, NULL);
  buf_free(&b);
  if (f) { PObj *ap = pobj_new(PO_DICT); if (ap) { pobj_dict_set(ap, "N", om_ref(f)); pobj_dict_set(a, "AP", ap); } }
  return add_annot(d, page, a);
}
int redact_mark_area(PDoc *d, int page, const double *rect, const RedactProps *p) { return mark(d, page, rect, NULL, 0, p); }
int redact_mark_quads(PDoc *d, int page, const double *q, int n, const RedactProps *p)
{
  double r[4] = { 1e30, 1e30, -1e30, -1e30 };
  int i, k;
  if (n < 1) return 0;
  for (i = 0; i < n; i++) for (k = 0; k < 4; k++) {
    double x = q[i * 8 + k * 2], y = q[i * 8 + k * 2 + 1];
    if (x < r[0]) r[0] = x;
    if (y < r[1]) r[1] = y;
    if (x > r[2]) r[2] = x;
    if (y > r[3]) r[3] = y;
  }
  return mark(d, page, r, q, n, p);
}
int redact_mark_page(PDoc *d, int page, const RedactProps *p)
{
  double b[4];
  int rot;
  PObj *pg = pc_page(d, page);
  if (!pg) return 0;
  pdf_page_box(d, page, b, &rot);
  {
    /* the whole media box, so nothing outside the crop box survives either */
    PObj *mb = pdf_resolve(d, pdf_page_attr(d, pg, "MediaBox"));
    int i;
    if (mb && mb->type == PO_ARRAY && mb->u.arr.n >= 4) for (i = 0; i < 4; i++) b[i] = pobj_num(pdf_resolve(d, mb->u.arr.v[i]));
  }
  return mark(d, page, b, NULL, 0, p);
}
int redact_mark_text(PDoc *d, int page, TextPage *t, const char *needle, const RedactProps *p)
{
  int from = 0, a, b, n = 0;
  if (!t || !needle || !*needle) return 0;
  while (textpage_find(t, needle, from, &a, &b)) {
    /* one quad per line of the match */
    double *q = (double *)malloc((size_t)(b - a + 1) * 8 * sizeof(double));
    int nq = 0, i = a;
    if (!q) break;
    while (i <= b) {
      int j = i;
      float x0 = t->o[i].x0, x1 = t->o[i].x1, y0 = t->o[i].y0, y1 = t->o[i].y1;
      while (j + 1 <= b && t->o[j + 1].u != 10 && fabs(t->o[j + 1].y0 - t->o[i].y0) < t->o[i].size * 0.5 + 0.5) {
        j++;
        if (t->o[j].x1 > t->o[j].x0 || t->o[j].u > 32) {
          if (t->o[j].x0 < x0) x0 = t->o[j].x0;
          if (t->o[j].x1 > x1) x1 = t->o[j].x1;
          if (t->o[j].y0 < y0) y0 = t->o[j].y0;
          if (t->o[j].y1 > y1) y1 = t->o[j].y1;
        }
      }
      if (x1 > x0 && y1 > y0) {
        double *qq = q + nq * 8;
        qq[0] = x0; qq[1] = y1; qq[2] = x1; qq[3] = y1; qq[4] = x0; qq[5] = y0; qq[6] = x1; qq[7] = y0;
        nq++;
      }
      i = j + 1;
    }
    if (nq && redact_mark_quads(d, page, q, nq, p)) n++;
    free(q);
    from = b + 1;
  }
  return n;
}
static int is_redact(PDoc *d, PObj *a) { return a && a->type == PO_DICT && pobj_is_name(pdf_dget(d, a, "Subtype"), "Redact"); }
int redact_count(PDoc *d, int page)
{
  int p, i, n = 0;
  for (p = page < 0 ? 0 : page; p < d->npages && (page < 0 || p == page); p++) {
    PObj *a = pdf_dget(d, d->pages[p], "Annots");
    if (a && a->type == PO_ARRAY) for (i = 0; i < a->u.arr.n; i++) if (is_redact(d, pdf_resolve(d, a->u.arr.v[i]))) n++;
  }
  return n;
}

/* ------------------------------------------------------------------ applying */
typedef struct { double r[4]; u32 fill; char overlay[64]; } Area;
typedef struct { PDoc *d; Area *a; int n; } RCtx;
static int overlaps(const double *a, const double *b) { return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3]; }
static int glyph_cb(void *ud, const double *box, u32 code, const CEState *s)
{
  RCtx *c = (RCtx *)ud;
  double cx = (box[0] + box[2]) / 2, cy = (box[1] + box[3]) / 2;
  int i;
  for (i = 0; i < c->n; i++) {
    const double *r = c->a[i].r;
    /* a glyph goes when its middle is in the area, or most of it is */
    if (cx >= r[0] && cx <= r[2] && cy >= r[1] && cy <= r[3]) return 1;
    if (overlaps(box, r)) {
      double ox = (box[2] < r[2] ? box[2] : r[2]) - (box[0] > r[0] ? box[0] : r[0]);
      double oy = (box[3] < r[3] ? box[3] : r[3]) - (box[1] > r[1] ? box[1] : r[1]);
      if (ox * oy > 0.4 * (box[2] - box[0]) * (box[3] - box[1])) return 1;
    }
  }
  return 0;
}
static void inv(const double m[6], double r[6])
{
  double det = m[0] * m[3] - m[1] * m[2];
  if (fabs(det) < 1e-12) { memset(r, 0, 6 * sizeof(double)); return; }
  r[0] = m[3] / det; r[1] = -m[1] / det; r[2] = -m[2] / det; r[3] = m[0] / det;
  r[4] = -(m[4] * r[0] + m[5] * r[2]); r[5] = -(m[4] * r[1] + m[5] * r[3]);
}
static void image_box(const double *m, double *b)
{
  double xs[4], ys[4];
  int k;
  xs[0] = m[4]; ys[0] = m[5];
  xs[1] = m[0] + m[4]; ys[1] = m[1] + m[5];
  xs[2] = m[2] + m[4]; ys[2] = m[3] + m[5];
  xs[3] = m[0] + m[2] + m[4]; ys[3] = m[1] + m[3] + m[5];
  b[0] = b[2] = xs[0]; b[1] = b[3] = ys[0];
  for (k = 1; k < 4; k++) { if (xs[k] < b[0]) b[0] = xs[k]; if (xs[k] > b[2]) b[2] = xs[k]; if (ys[k] < b[1]) b[1] = ys[k]; if (ys[k] > b[3]) b[3] = ys[k]; }
}
static int image_cb(void *ud, PObj *img, int inl, const double *m, int *newnum)
{
  RCtx *c = (RCtx *)ud;
  double b[4], mi[6];
  int i, hit = 0, w, h, comps, bpc, rgb, mask;
  u8 *s;
  image_box(m, b);
  for (i = 0; i < c->n; i++) if (overlaps(b, c->a[i].r)) hit = 1;
  if (!hit) return 0;
  if (inl) return 1;                                   /* (an inline image under an area goes) */
  if (!(s = ce_image_samples(c->d, img, &w, &h, &comps, &bpc, &rgb)) || (bpc != 8 && bpc != 1)) { free(s); return 1; }
  mask = !rgb && pdf_dget(c->d, img, "ImageMask") != NULL;
  inv(m, mi);
  for (i = 0; i < c->n; i++) {
    const double *r = c->a[i].r;
    double u0 = 1e30, v0 = 1e30, u1 = -1e30, v1 = -1e30;
    int k, x, y, x0, x1, y0, y1;
    size_t row = ((size_t)w * comps * bpc + 7) / 8;
    for (k = 0; k < 4; k++) {
      double px = k & 1 ? r[2] : r[0], py = k & 2 ? r[3] : r[1];
      double u = mi[0] * px + mi[2] * py + mi[4], v = mi[1] * px + mi[3] * py + mi[5];
      if (u < u0) u0 = u;
      if (u > u1) u1 = u;
      if (v < v0) v0 = v;
      if (v > v1) v1 = v;
    }
    x0 = (int)floor(u0 * w); x1 = (int)ceil(u1 * w); y0 = (int)floor((1 - v1) * h); y1 = (int)ceil((1 - v0) * h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
      u8 *p = s + row * y;
      if (bpc == 8) {
        int k2;
        for (k2 = 0; k2 < comps; k2++) p[x * comps + k2] = (u8)(comps == 4 && k2 == 3 ? 255 : 0);
      } else {
        /* one bit: a mask's sample 1 paints nothing (with the usual Decode), else 0 is black */
        if (mask) p[x / 8] |= (u8)(0x80 >> (x & 7)); else p[x / 8] &= (u8)~(0x80 >> (x & 7));
      }
    }
  }
  *newnum = ce_image_store(c->d, img, s, w, h, comps, bpc, rgb, 0);
  free(s);
  return *newnum ? 2 : 1;
}
static void array_remove(PObj *arr, int i)
{
  pobj_free(arr->u.arr.v[i]);
  memmove(arr->u.arr.v + i, arr->u.arr.v + i + 1, (size_t)(arr->u.arr.n - i - 1) * sizeof(PObj *));
  arr->u.arr.n--;
}
static void get_color(PDoc *d, PObj *a, const char *key, u32 *c)
{
  PObj *v = pdf_dget(d, a, key);
  if (v && v->type == PO_ARRAY && v->u.arr.n == 3) {
    int i;
    u32 x = 0;
    for (i = 0; i < 3; i++) { double f = pobj_num(pdf_resolve(d, v->u.arr.v[i])); x = (x << 8) | (u32)(f < 0 ? 0 : f > 1 ? 255 : f * 255 + 0.5); }
    *c = x;
  }
}
int redact_apply(PDoc *d, int page)
{
  int p, total = 0;
  for (p = page < 0 ? 0 : page; p < d->npages && (page < 0 || p == page); p++) {
    PObj *pg = d->pages[p], *arr = pdf_dget(d, pg, "Annots");
    RCtx c;
    CEOps ops;
    int i, cap = 0;
    if (!arr || arr->type != PO_ARRAY) continue;
    memset(&c, 0, sizeof c);
    c.d = d;
    /* the areas: each quad (or the rectangle) of each mark */
    for (i = 0; i < arr->u.arr.n; i++) {
      PObj *a = pdf_resolve(d, arr->u.arr.v[i]), *q, *ov;
      double r[4];
      int k, nq;
      u32 fill = 0xFFFFFFFFu;
      if (!is_redact(d, a)) continue;
      get_color(d, a, "IC", &fill);
      q = pdf_dget(d, a, "QuadPoints");
      nq = q && q->type == PO_ARRAY ? q->u.arr.n / 8 : 0;
      ov = pdf_dget(d, a, "OverlayText");
      for (k = 0; k < (nq ? nq : 1); k++) {
        Area *na;
        if (nq) {
          int j;
          r[0] = r[1] = 1e30; r[2] = r[3] = -1e30;
          for (j = 0; j < 4; j++) {
            double x = pobj_num(pdf_resolve(d, q->u.arr.v[k * 8 + j * 2])), y = pobj_num(pdf_resolve(d, q->u.arr.v[k * 8 + j * 2 + 1]));
            if (x < r[0]) r[0] = x;
            if (x > r[2]) r[2] = x;
            if (y < r[1]) r[1] = y;
            if (y > r[3]) r[3] = y;
          }
        } else {
          PObj *ra = pdf_dget(d, a, "Rect");
          int j;
          if (!ra || ra->type != PO_ARRAY || ra->u.arr.n < 4) continue;
          for (j = 0; j < 4; j++) r[j] = pobj_num(pdf_resolve(d, ra->u.arr.v[j]));
          if (r[0] > r[2]) { double t = r[0]; r[0] = r[2]; r[2] = t; }
          if (r[1] > r[3]) { double t = r[1]; r[1] = r[3]; r[3] = t; }
        }
        if (c.n >= cap) { int nc = cap ? cap * 2 : 8; if (!(na = (Area *)realloc(c.a, (size_t)nc * sizeof(Area)))) continue; c.a = na; cap = nc; }
        memcpy(c.a[c.n].r, r, sizeof r);
        c.a[c.n].fill = fill;
        c.a[c.n].overlay[0] = 0;
        if (ov && ov->type == PO_STRING) { char *u = pdf_text_string(ov); if (u) { strncpy(c.a[c.n].overlay, u, sizeof c.a[0].overlay - 1); c.a[c.n].overlay[sizeof c.a[0].overlay - 1] = 0; free(u); } }
        c.n++;
      }
    }
    if (!c.n) { free(c.a); continue; }
    /* the content: glyphs and pixels under the areas taken out */
    memset(&ops, 0, sizeof ops);
    ops.ud = &c; ops.glyph = glyph_cb; ops.image = image_cb;
    ce_rewrite_page(d, p, &ops);
    /* annotations there (and the marks) taken away */
    if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *cp = pobj_copy(arr); if (cp) { pobj_dict_set(pg, "Annots", cp); arr = cp; } }
    for (i = 0; i < arr->u.arr.n; ) {
      PObj *a = pdf_resolve(d, arr->u.arr.v[i]), *ra;
      double r[4];
      int k, gone = is_redact(d, a);
      if (!gone && a && a->type == PO_DICT && (ra = pdf_dget(d, a, "Rect")) && ra->type == PO_ARRAY && ra->u.arr.n >= 4) {
        for (k = 0; k < 4; k++) r[k] = pobj_num(pdf_resolve(d, ra->u.arr.v[k]));
        if (r[0] > r[2]) { double t = r[0]; r[0] = r[2]; r[2] = t; }
        if (r[1] > r[3]) { double t = r[1]; r[1] = r[3]; r[3] = t; }
        for (k = 0; k < c.n; k++) if (overlaps(r, c.a[k].r)) gone = 1;
      }
      if (gone) array_remove(arr, i); else i++;
    }
    /* the areas painted */
    {
      Buf b;
      PObj *res = pc_resources(d, pg);
      char fname[32] = "";
      buf_init(&b);
      for (i = 0; i < c.n; i++) {
        const double *r = c.a[i].r;
        u32 f = c.a[i].fill;
        if (f == 0xFFFFFFFFu) continue;
        om_num(&b, ((f >> 16) & 255) / 255.0); om_num(&b, ((f >> 8) & 255) / 255.0); om_num(&b, (f & 255) / 255.0); buf_adds(&b, "rg ");
        om_num(&b, r[0]); om_num(&b, r[1]); om_num(&b, r[2] - r[0]); om_num(&b, r[3] - r[1]); buf_adds(&b, "re f\n");
        if (c.a[i].overlay[0] && res) {
          char lat[200];
          int len = (int)utf8_to_latin1(c.a[i].overlay, strlen(c.a[i].overlay), lat, sizeof lat);
          double size = r[3] - r[1] > 14 ? 10 : (r[3] - r[1]) * 0.7;
          if (!fname[0]) pc_add_resource(d, res, "Font", "FoRd", om_std_font("Helvetica"), fname, sizeof fname);
          buf_adds(&b, ((f >> 16) & 255) + ((f >> 8) & 255) + (f & 255) > 380 ? "0 g " : "1 g ");
          buf_printf(&b, "BT /%s ", fname); om_num(&b, size); buf_adds(&b, "Tf "); om_num(&b, r[0] + 2); om_num(&b, (r[1] + r[3]) / 2 - size * 0.35); buf_adds(&b, "Td ");
          om_str(&b, lat, len); buf_adds(&b, " Tj ET\n");
        }
      }
      if (b.len) pc_add_content(d, p, b.data, b.len, 0, "redact");
      buf_free(&b);
    }
    total += c.n;
    free(c.a);
    d->changed = 1;
  }
  return total;
}

/* ------------------------------------------------------------------ hidden information */
static PObj *root_of(PDoc *d) { return pdf_dget(d, d->trailer, "Root"); }
static int is_js_action(PDoc *d, PObj *a)
{
  a = pdf_resolve(d, a);
  return a && a->type == PO_DICT && (pobj_is_name(pdf_dget(d, a, "S"), "JavaScript") || pobj_is_name(pdf_dget(d, a, "S"), "Launch"));
}
int hidden_survey(PDoc *d)
{
  PObj *root = root_of(d), *names = root ? pdf_dget(d, root, "Names") : NULL;
  int what = 0, p, i;
  if (!root) return 0;
  if (pobj_dict_get(d->trailer, "Info") || pdf_dget(d, root, "Metadata")) what |= HID_METADATA;
  if ((names && pdf_dget(d, names, "JavaScript")) || is_js_action(d, pdf_dget(d, root, "OpenAction")) || pdf_dget(d, root, "AA")) what |= HID_SCRIPTS;
  if (names && pdf_dget(d, names, "EmbeddedFiles")) what |= HID_ATTACH;
  if (pdf_dget(d, root, "AcroForm")) what |= HID_FORMS;
  if (pdf_dget(d, root, "OCProperties")) what |= HID_LAYERS;
  if (pdf_dget(d, root, "PieceInfo")) what |= HID_PRIVATE;
  if (pdf_dget(d, root, "Outlines")) what |= HID_BOOKMARKS;
  for (p = 0; p < d->npages; p++) {
    PObj *pg = d->pages[p], *a = pdf_dget(d, pg, "Annots");
    if (pdf_dget(d, pg, "Thumb")) what |= HID_THUMBS;
    if (pdf_dget(d, pg, "PieceInfo")) what |= HID_PRIVATE;
    if (pdf_dget(d, pg, "AA")) what |= HID_SCRIPTS;
    if (pdf_dget(d, pg, "Metadata")) what |= HID_METADATA;
    if (a && a->type == PO_ARRAY) for (i = 0; i < a->u.arr.n; i++) {
      PObj *an = pdf_resolve(d, a->u.arr.v[i]);
      const char *st = an && an->type == PO_DICT ? pdf_dname(d, an, "Subtype") : NULL;
      if (!st) continue;
      if (!strcmp(st, "Link")) { PObj *ac = pdf_dget(d, an, "A"); if (ac && (pobj_is_name(pdf_dget(d, ac, "S"), "URI") || pobj_is_name(pdf_dget(d, ac, "S"), "Launch"))) what |= HID_LINKS; }
      else if (!strcmp(st, "FileAttachment")) what |= HID_ATTACH;
      else if (strcmp(st, "Widget") && strcmp(st, "Popup")) what |= HID_COMMENTS;
      if (is_js_action(d, pdf_dget(d, an, "A")) || pdf_dget(d, an, "AA")) what |= HID_SCRIPTS;
    }
  }
  return what;
}
int hidden_remove(PDoc *d, int what)
{
  PObj *root = root_of(d), *names;
  int n = 0, p, i;
  if (!root) return 0;
  names = pdf_dget(d, root, "Names");
  if (what & HID_FORMS) {
    FForm f;
    if (form_load(d, &f) || pdf_dget(d, root, "AcroForm")) { n += form_flatten(d, &f) + 1; }
    form_free(&f);
  }
  if (what & (HID_LAYERS | HID_INVISIBLE)) {
    CEOps ops;
    memset(&ops, 0, sizeof ops);
    ops.drop_hidden = (what & HID_LAYERS) != 0;
    ops.drop_invisible = (what & HID_INVISIBLE) != 0;
    for (p = 0; p < d->npages; p++) n += ce_rewrite_page(d, p, &ops);
    if ((what & HID_LAYERS) && pdf_dget(d, root, "OCProperties")) { pobj_dict_del(root, "OCProperties"); n++; }
  }
  if (what & HID_METADATA) {
    if (pobj_dict_get(d->trailer, "Info")) { pobj_dict_del(d->trailer, "Info"); n++; }
    if (pdf_dget(d, root, "Metadata")) { pobj_dict_del(root, "Metadata"); n++; }
  }
  if ((what & HID_SCRIPTS)) {
    if (names && pdf_dget(d, names, "JavaScript")) { pobj_dict_del(names, "JavaScript"); n++; }
    if (is_js_action(d, pdf_dget(d, root, "OpenAction"))) { pobj_dict_del(root, "OpenAction"); n++; }
    if (pdf_dget(d, root, "AA")) { pobj_dict_del(root, "AA"); n++; }
  }
  if ((what & HID_ATTACH) && names && pdf_dget(d, names, "EmbeddedFiles")) { pobj_dict_del(names, "EmbeddedFiles"); n++; }
  if ((what & HID_PRIVATE) && pdf_dget(d, root, "PieceInfo")) { pobj_dict_del(root, "PieceInfo"); n++; }
  if ((what & HID_BOOKMARKS) && pdf_dget(d, root, "Outlines")) { pobj_dict_del(root, "Outlines"); n++; }
  for (p = 0; p < d->npages; p++) {
    PObj *pg = d->pages[p], *a = pdf_dget(d, pg, "Annots");
    if ((what & HID_THUMBS) && pdf_dget(d, pg, "Thumb")) { pobj_dict_del(pg, "Thumb"); n++; }
    if ((what & HID_PRIVATE) && pdf_dget(d, pg, "PieceInfo")) { pobj_dict_del(pg, "PieceInfo"); n++; }
    if ((what & HID_SCRIPTS) && pdf_dget(d, pg, "AA")) { pobj_dict_del(pg, "AA"); n++; }
    if ((what & HID_METADATA) && pdf_dget(d, pg, "Metadata")) { pobj_dict_del(pg, "Metadata"); n++; }
    if (!a || a->type != PO_ARRAY) continue;
    if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *cp = pobj_copy(a); if (!cp) continue; pobj_dict_set(pg, "Annots", cp); a = cp; }
    for (i = 0; i < a->u.arr.n; ) {
      PObj *an = pdf_resolve(d, a->u.arr.v[i]);
      const char *st = an && an->type == PO_DICT ? pdf_dname(d, an, "Subtype") : NULL;
      int gone = 0;
      if (st) {
        if (!strcmp(st, "Link")) {
          PObj *ac = pdf_dget(d, an, "A");
          if ((what & HID_LINKS) && ac && (pobj_is_name(pdf_dget(d, ac, "S"), "URI") || pobj_is_name(pdf_dget(d, ac, "S"), "Launch"))) gone = 1;
        } else if (!strcmp(st, "FileAttachment")) gone = (what & (HID_ATTACH | HID_COMMENTS)) != 0;
        else if (strcmp(st, "Widget")) gone = (what & HID_COMMENTS) != 0;
        if (!gone && (what & HID_SCRIPTS)) {
          if (is_js_action(d, pdf_dget(d, an, "A"))) { pobj_dict_del(an, "A"); n++; }
          if (pdf_dget(d, an, "AA")) { pobj_dict_del(an, "AA"); n++; }
        }
      }
      if (gone) { array_remove(a, i); n++; } else i++;
    }
  }
  if (n) d->changed = 1;
  return n;
}
