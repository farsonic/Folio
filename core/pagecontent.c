/*
 * pagecontent.c - adding to a page's content and the object helpers the editing files share
 * (see pagecontent.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "pagecontent.h"

/* ------------------------------------------------------------------ objects */
PObj *om_ref(int num)
{
  PObj *r = pobj_new(PO_REF);
  if (r) { r->u.ref.num = num; r->u.ref.gen = 0; }
  return r;
}
PObj *om_arr(const double *v, int n)
{
  PObj *a = pobj_new(PO_ARRAY);
  int i;
  for (i = 0; a && i < n; i++) pobj_array_add(a, v[i] == floor(v[i]) && fabs(v[i]) < 1e9 ? pobj_int((long)v[i]) : pobj_real(v[i]));
  return a;
}
PObj *om_rgb(u32 c)
{
  double v[3];
  v[0] = ((c >> 16) & 255) / 255.0; v[1] = ((c >> 8) & 255) / 255.0; v[2] = (c & 255) / 255.0;
  return om_arr(v, 3);
}
PObj *om_text(const char *s)
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
PObj *om_date(void)
{
  char t[40];
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  if (tm) snprintf(t, sizeof t, "D:%04d%02d%02d%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  else strcpy(t, "D:20000101000000");
  return pobj_string(t, (int)strlen(t));
}
int om_num_of(PDoc *d, const PObj *o)
{
  int k;
  if (!o) return 0;
  for (k = 1; k < d->nxref; k++) if (d->xref[k].obj == o) return k;
  return 0;
}
int om_stream(PDoc *d, PObj *dict, const void *data, size_t n)
{
  PObj *s = pobj_new(PO_STREAM);
  int i, num;
  if (!s) { pobj_free(dict); return 0; }
  if (dict) {
    for (i = 0; i < dict->u.dict.n; i++) { pobj_dict_set(s, dict->u.dict.k[i], dict->u.dict.v[i]); dict->u.dict.v[i] = NULL; }
    pobj_free(dict);
  }
  if (!(s->sdata = (u8 *)malloc(n ? n : 1))) { pobj_free(s); return 0; }
  if (n) memcpy(s->sdata, data, n);
  s->slen = (long)n;
  s->soff = -1;
  num = pdf_new_object(d, s);
  if (num) s->onum = num;
  return num;
}
int om_form(PDoc *d, const char *ops, size_t n, const double *bbox, PObj *res)
{
  PObj *dict = pobj_new(PO_DICT);
  if (!dict) { pobj_free(res); return 0; }
  pobj_dict_set(dict, "Type", pobj_name("XObject"));
  pobj_dict_set(dict, "Subtype", pobj_name("Form"));
  pobj_dict_set(dict, "BBox", om_arr(bbox, 4));
  pobj_dict_set(dict, "Resources", res ? res : pobj_new(PO_DICT));
  return om_stream(d, dict, ops, n);
}
PObj *om_std_font(const char *name)
{
  PObj *f = pobj_new(PO_DICT);
  if (!f) return NULL;
  pobj_dict_set(f, "Type", pobj_name("Font"));
  pobj_dict_set(f, "Subtype", pobj_name("Type1"));
  pobj_dict_set(f, "BaseFont", pobj_name(name));
  if (strcmp(name, "ZapfDingbats") && strcmp(name, "Symbol")) {
    /* the widths written out, so every reader (and Folio without its stand-in fonts) spaces the text alike */
    int font = !strcmp(name, "Helvetica-Bold") ? 1 : !strncmp(name, "Times", 5) ? 2 : !strncmp(name, "Courier", 7) ? 3 : 0, c;
    PObj *w = pobj_new(PO_ARRAY);
    pobj_dict_set(f, "Encoding", pobj_name("WinAnsiEncoding"));
    for (c = 32; w && c < 256; c++) { char ch = (char)c; pobj_array_add(w, pobj_int((long)(om_text_width(&ch, 1, 1000, font) + 0.5))); }
    pobj_dict_set(f, "FirstChar", pobj_int(32));
    pobj_dict_set(f, "LastChar", pobj_int(255));
    pobj_dict_set(f, "Widths", w);
  }
  return f;
}
void om_num(Buf *b, double v)
{
  char t[340];
  int n;
  if (fabs(v) < 1e-6 || !(v == v) || fabs(v) > 1e300) v = 0;
  n = snprintf(t, sizeof t, "%.3f", v);
  while (n > 0 && t[n - 1] == '0') t[--n] = 0;
  if (n > 0 && t[n - 1] == '.') t[--n] = 0;
  buf_adds(b, t);
  buf_addc(b, ' ');
}
void om_str(Buf *b, const char *s, int n)
{
  int i;
  buf_addc(b, '(');
  for (i = 0; i < n; i++) {
    u8 c = (u8)s[i];
    if (c == '(' || c == ')' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
    else if (c < 32 || c >= 127) buf_printf(b, "\\%03o", c);
    else buf_addc(b, c);
  }
  buf_addc(b, ')');
}
/* the standard fonts' widths, space to tilde (1/1000 em) */
static const short helv_w[95] = {
  278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
  556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
  667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556,
  556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584
};
static const short helvb_w[95] = {
  278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
  556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
  667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611,
  611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584
};
static const short times_w[95] = {
  250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278, 500, 500, 500, 500, 500, 500, 500, 500,
  500, 500, 278, 278, 564, 564, 564, 444, 921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722,
  556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500, 333, 444, 500, 444, 500, 444, 333, 500,
  500, 278, 278, 500, 278, 778, 500, 500, 500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541
};
double om_text_width(const char *s, int n, double size, int font)
{
  double w = 0;
  int i;
  const short *t = font == 1 ? helvb_w : font == 2 ? times_w : helv_w;
  for (i = 0; i < n; i++) {
    u8 c = (u8)s[i];
    if (font == 3) w += 600;
    else w += c >= 32 && c < 127 ? t[c - 32] : font == 2 ? 500 : 556;
  }
  return w * size / 1000;
}

/* ------------------------------------------------------------------ pages */
PObj *pc_page(PDoc *d, int page) { return page >= 0 && page < d->npages ? d->pages[page] : NULL; }
PObj *pc_resources(PDoc *d, PObj *pg)
{
  PObj *r = pobj_dict_get(pg, "Resources"), *v;
  if (r && r->type == PO_DICT) return r;
  v = pdf_resolve(d, pdf_page_attr(d, pg, "Resources"));
  r = v && v->type == PO_DICT ? pobj_copy(v) : pobj_new(PO_DICT);
  if (!r) return NULL;
  pobj_dict_set(pg, "Resources", r);
  return r;
}
int pc_add_resource(PDoc *d, PObj *res, const char *cat, const char *prefix, PObj *value, char *name, int cap)
{
  PObj *c = pobj_dict_get(res, cat), *cv;
  int i;
  char t[64];
  if (!value) return 0;
  if (!c || c->type != PO_DICT) {
    cv = pdf_resolve(d, c);
    c = cv && cv->type == PO_DICT ? pobj_copy(cv) : pobj_new(PO_DICT);
    if (!c) { pobj_free(value); return 0; }
    pobj_dict_set(res, cat, c);
  }
  for (i = 1; i < 100000; i++) {
    snprintf(t, sizeof t, "%s%d", prefix, i);
    if (!pobj_dict_get(c, t)) break;
  }
  pobj_dict_set(c, t, value);
  if (name) { strncpy(name, t, (size_t)cap - 1); name[cap - 1] = 0; }
  return 1;
}
/* the page's /Contents as an array of references (made one if it was a single stream) */
static PObj *contents_array(PDoc *d, PObj *pg)
{
  PObj *c = pobj_dict_get(pg, "Contents"), *a, *r;
  if (c && c->type == PO_ARRAY) return c;
  r = pdf_resolve(d, c);
  if (r && r->type == PO_ARRAY) { a = pobj_copy(r); if (a) pobj_dict_set(pg, "Contents", a); return a; }
  if (!(a = pobj_new(PO_ARRAY))) return NULL;
  if (c && c->type == PO_REF) pobj_array_add(a, pobj_copy(c));
  pobj_dict_set(pg, "Contents", a);
  return a;
}
static int stream_marked(PDoc *d, PObj *s, const char *mark)
{
  size_t n, ml = strlen(mark);
  u8 *data;
  int r;
  s = pdf_resolve(d, s);
  if (!s || s->type != PO_STREAM) return 0;
  if (s->sdata) return (size_t)s->slen >= ml + 7 && !memcmp(s->sdata, "%Folio:", 7) && !memcmp(s->sdata + 7, mark, ml);
  if (!(data = pdf_stream_data(d, s, &n))) return 0;
  r = n >= ml + 7 && !memcmp(data, "%Folio:", 7) && !memcmp(data + 7, mark, ml);
  free(data);
  return r;
}
int pc_add_content(PDoc *d, int page, const char *ops, size_t n, int under, const char *mark)
{
  PObj *pg = pc_page(d, page), *a;
  Buf b;
  int num, i, wrapped = 0;
  if (!pg || !(a = contents_array(d, pg))) return 0;
  /* the old content wrapped in q ... Q once (Folio's q stream marked "wrap") */
  for (i = 0; i < a->u.arr.n; i++) if (stream_marked(d, a->u.arr.v[i], "wrap")) wrapped = 1;
  if (!wrapped && a->u.arr.n) {
    int q = om_stream(d, NULL, "%Folio:wrap\nq\n", 14), e = om_stream(d, NULL, "%Folio:wrapend\nQ\n", 17);
    PObj **nv;
    if (!q || !e || !(nv = (PObj **)realloc(a->u.arr.v, (size_t)(a->u.arr.n + 2) * sizeof(PObj *)))) return 0;
    a->u.arr.v = nv; a->u.arr.cap = a->u.arr.n + 2;
    memmove(nv + 1, nv, (size_t)a->u.arr.n * sizeof(PObj *));
    nv[0] = om_ref(q);
    a->u.arr.n++;
    nv[a->u.arr.n++] = om_ref(e);
  }
  buf_init(&b);
  if (mark) buf_printf(&b, "%%Folio:%s\n", mark);
  buf_adds(&b, "q\n");
  buf_add(&b, ops, n);
  buf_adds(&b, "\nQ\n");
  if (b.oom || !(num = om_stream(d, NULL, b.data, b.len))) { buf_free(&b); return 0; }
  buf_free(&b);
  if (under) {
    PObj *r = om_ref(num), **nv;
    if (!(nv = (PObj **)realloc(a->u.arr.v, (size_t)(a->u.arr.n + 1) * sizeof(PObj *)))) { pobj_free(r); return 0; }
    a->u.arr.v = nv; a->u.arr.cap = a->u.arr.n + 1;
    memmove(nv + 1, nv, (size_t)a->u.arr.n * sizeof(PObj *));
    nv[0] = r; a->u.arr.n++;
  } else pobj_array_add(a, om_ref(num));
  d->changed = 1;
  return 1;
}
int pc_has_marked(PDoc *d, int page, const char *mark)
{
  PObj *pg = pc_page(d, page), *c;
  int i;
  if (!pg) return 0;
  c = pdf_resolve(d, pobj_dict_get(pg, "Contents"));
  if (!c) return 0;
  if (c->type != PO_ARRAY) return stream_marked(d, c, mark);
  for (i = 0; i < c->u.arr.n; i++) if (stream_marked(d, c->u.arr.v[i], mark)) return 1;
  return 0;
}
int pc_remove_marked(PDoc *d, int page, const char *mark)
{
  PObj *pg = pc_page(d, page), *a;
  int i, n = 0;
  if (!pg || !pc_has_marked(d, page, mark) || !(a = contents_array(d, pg))) return 0;
  for (i = 0; i < a->u.arr.n; ) {
    if (stream_marked(d, a->u.arr.v[i], mark)) {
      pobj_free(a->u.arr.v[i]);
      memmove(a->u.arr.v + i, a->u.arr.v + i + 1, (size_t)(a->u.arr.n - i - 1) * sizeof(PObj *));
      a->u.arr.n--; n++;
    } else i++;
  }
  if (n) d->changed = 1;
  return n;
}
u8 *pc_content(PDoc *d, PObj *pg, size_t *n)
{
  PObj *c = pdf_resolve(d, pobj_dict_get(pg, "Contents"));
  Buf b;
  int i;
  *n = 0;
  if (!c) return NULL;
  if (c->type == PO_STREAM) return pdf_stream_data(d, c, n);
  if (c->type != PO_ARRAY) return NULL;
  buf_init(&b);
  for (i = 0; i < c->u.arr.n; i++) {
    size_t k;
    u8 *p = pdf_stream_data(d, pdf_resolve(d, c->u.arr.v[i]), &k);
    if (p) { buf_add(&b, p, k); buf_addc(&b, '\n'); free(p); }
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *n = b.len;
  return (u8 *)buf_take(&b);
}
int pc_set_content(PDoc *d, PObj *pg, const u8 *data, size_t n)
{
  int num = om_stream(d, NULL, data, n);
  if (!num) return 0;
  pobj_dict_set(pg, "Contents", om_ref(num));
  d->changed = 1;
  return 1;
}

/* ------------------------------------------------------------------ annotations into content */
static void mat_mul(const double a[6], const double b[6], double r[6])
{
  double t[6];
  t[0] = a[0] * b[0] + a[1] * b[2]; t[1] = a[0] * b[1] + a[1] * b[3];
  t[2] = a[2] * b[0] + a[3] * b[2]; t[3] = a[2] * b[1] + a[3] * b[3];
  t[4] = a[4] * b[0] + a[5] * b[2] + b[4]; t[5] = a[4] * b[1] + a[5] * b[3] + b[5];
  memcpy(r, t, sizeof t);
}
static int get_nums(PDoc *d, PObj *a, double *v, int n)
{
  int i;
  a = pdf_resolve(d, a);
  if (!a || a->type != PO_ARRAY || a->u.arr.n < n) return 0;
  for (i = 0; i < n; i++) v[i] = pobj_num(pdf_resolve(d, a->u.arr.v[i]));
  return 1;
}
PObj *pc_appearance(PDoc *d, PObj *annot)
{
  PObj *ap = pdf_dget(d, annot, "AP"), *n;
  if (!ap || ap->type != PO_DICT) return NULL;
  n = pdf_dget(d, ap, "N");
  if (!n) return NULL;
  if (n->type == PO_STREAM) return n;
  if (n->type == PO_DICT) {
    const char *as = pdf_dname(d, annot, "AS");
    PObj *s = as ? pdf_dget(d, n, as) : NULL;
    if (!s && n->u.dict.n == 1) s = pdf_resolve(d, n->u.dict.v[0]);
    return s && s->type == PO_STREAM ? s : NULL;
  }
  return NULL;
}
void pc_appearance_matrix(PDoc *d, PObj *annot, PObj *form, double m[6])
{
  double bb[4], fm[6] = { 1, 0, 0, 1, 0, 0 }, r[4], x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30, t[6];
  int i;
  if (!get_nums(d, pdf_dget(d, form, "BBox"), bb, 4)) { bb[0] = bb[1] = 0; bb[2] = bb[3] = 1; }
  get_nums(d, pdf_dget(d, form, "Matrix"), fm, 6);
  if (!get_nums(d, pdf_dget(d, annot, "Rect"), r, 4)) { r[0] = r[1] = 0; r[2] = r[3] = 1; }
  if (r[0] > r[2]) { double s = r[0]; r[0] = r[2]; r[2] = s; }
  if (r[1] > r[3]) { double s = r[1]; r[1] = r[3]; r[3] = s; }
  for (i = 0; i < 4; i++) {
    double x = i & 1 ? bb[2] : bb[0], y = i & 2 ? bb[3] : bb[1];
    double tx = fm[0] * x + fm[2] * y + fm[4], ty = fm[1] * x + fm[3] * y + fm[5];
    if (tx < x0) x0 = tx;
    if (tx > x1) x1 = tx;
    if (ty < y0) y0 = ty;
    if (ty > y1) y1 = ty;
  }
  t[1] = t[2] = 0;
  t[0] = x1 - x0 > 1e-6 ? (r[2] - r[0]) / (x1 - x0) : 1;
  t[3] = y1 - y0 > 1e-6 ? (r[3] - r[1]) / (y1 - y0) : 1;
  t[4] = r[0] - x0 * t[0]; t[5] = r[1] - y0 * t[3];
  mat_mul(fm, t, m);
}
int pc_flatten_annot(PDoc *d, int page, PObj *annot, Buf *ops, PObj *res)
{
  PObj *form = pc_appearance(d, annot);
  double m[6];
  char name[32];
  int num, i, fl = (int)pdf_dint(d, annot, "F", 0);
  if (!form || (fl & 2) || (fl & 32)) return 0;        /* hidden, or no view */
  num = om_num_of(d, form);
  pc_appearance_matrix(d, annot, form, m);
  if (num) { if (!pc_add_resource(d, res, "XObject", "FlA", om_ref(num), name, sizeof name)) return 0; }
  else {
    /* a direct appearance stream: made an object of its own */
    PObj *c = pobj_copy(form);
    if (!c || !(num = pdf_new_object(d, c))) return 0;
    c->onum = form->onum;                           /* (decrypted as the original) */
    if (!pc_add_resource(d, res, "XObject", "FlA", om_ref(num), name, sizeof name)) return 0;
  }
  buf_adds(ops, "q ");
  for (i = 0; i < 6; i++) om_num(ops, m[i]);
  buf_printf(ops, "cm /%s Do Q\n", name);
  return 1;
}
