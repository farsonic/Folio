/*
 * optimize.c - smaller documents and print production (see optimize.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "optimize.h"
#include "pagecontent.h"
#include "contentedit.h"

/* ------------------------------------------------------------------ audit */
static long stream_size(PObj *o) { return o->sdata ? o->slen : o->slen > 0 ? o->slen : 0; }
static void mark_ref(PDoc *d, PObj *v, u8 *kind, int k)
{
  if (v && v->type == PO_REF && v->u.ref.num > 0 && v->u.ref.num < d->nxref) kind[v->u.ref.num] = (u8)k;
  else if (v && v->type == PO_ARRAY) { int i; for (i = 0; i < v->u.arr.n; i++) mark_ref(d, v->u.arr.v[i], kind, k); }
}
void optimize_audit(PDoc *d, SpaceAudit *a)
{
  u8 *kind = (u8 *)calloc((size_t)d->nxref + 1, 1);
  int k, p;
  long streams = 0;
  memset(a, 0, sizeof *a);
  a->total = (long)d->len;
  if (!kind) return;
  /* page contents and font programs are known by who refers to them */
  for (p = 0; p < d->npages; p++) mark_ref(d, pobj_dict_get(d->pages[p], "Contents"), kind, 1);
  for (k = 1; k < d->nxref; k++) {
    PObj *o = pdf_get(d, k);
    if (o && o->type == PO_DICT && pobj_is_name(pdf_dget(d, o, "Type"), "FontDescriptor")) {
      mark_ref(d, pobj_dict_get(o, "FontFile"), kind, 2); mark_ref(d, pobj_dict_get(o, "FontFile2"), kind, 2); mark_ref(d, pobj_dict_get(o, "FontFile3"), kind, 2);
      a->nfonts++;
    }
  }
  for (k = 1; k < d->nxref; k++) {
    PObj *o = pdf_get(d, k);
    const char *st, *ty;
    long n;
    if (!o) continue;
    a->nobjects++;
    if (o->type != PO_STREAM) continue;
    n = stream_size(o);
    streams += n;
    st = pdf_dname(d, o, "Subtype"); ty = pdf_dname(d, o, "Type");
    if (kind[k] == 1) a->content += n;
    else if (kind[k] == 2) a->fonts += n;
    else if (st && !strcmp(st, "Image")) { a->images += n; a->nimages++; }
    else if (st && !strcmp(st, "Form")) a->forms += n;
    else if (ty && !strcmp(ty, "Metadata")) a->metadata += n;
    else if (ty && !strcmp(ty, "EmbeddedFile")) a->attachments += n;
    else a->other_streams += n;
  }
  a->objects = a->total - streams > 0 ? a->total - streams : 0;
  free(kind);
}

/* ------------------------------------------------------------------ pictures */
typedef struct { PDoc *d; double *minscale; } Survey;
static int survey_image(void *ud, PObj *img, int inl, const double *m, int *nn)
{
  Survey *s = (Survey *)ud;
  int num;
  double w, h, sx, sy, sc;
  if (inl || !(num = om_num_of(s->d, img))) return 0;
  w = pdf_dnum(s->d, img, "Width", 1); h = pdf_dnum(s->d, img, "Height", 1);
  sx = sqrt(m[0] * m[0] + m[1] * m[1]); sy = sqrt(m[2] * m[2] + m[3] * m[3]);
  /* the resolution it is shown at: pixels per inch, the lesser across and down */
  if (sx < 1e-6 || sy < 1e-6) return 0;
  sc = w / (sx / 72.0);
  if (h / (sy / 72.0) < sc) sc = h / (sy / 72.0);
  if (s->minscale[num] <= 0 || sc < s->minscale[num]) s->minscale[num] = sc;
  return 0;
}
static u8 *shrink(const u8 *s, int w, int h, int comps, int nw, int nh)
{
  u8 *o = (u8 *)malloc((size_t)nw * nh * comps);
  int x, y, c;
  if (!o) return NULL;
  for (y = 0; y < nh; y++) {
    int y0 = (int)((long)y * h / nh), y1 = (int)((long)(y + 1) * h / nh);
    if (y1 <= y0) y1 = y0 + 1;
    for (x = 0; x < nw; x++) {
      int x0 = (int)((long)x * w / nw), x1 = (int)((long)(x + 1) * w / nw), xx, yy;
      if (x1 <= x0) x1 = x0 + 1;
      for (c = 0; c < comps; c++) {
        long sum = 0, cnt = 0;
        for (yy = y0; yy < y1 && yy < h; yy++) for (xx = x0; xx < x1 && xx < w; xx++) { sum += s[((long)yy * w + xx) * comps + c]; cnt++; }
        o[((long)y * nw + x) * comps + c] = (u8)(cnt ? (sum + cnt / 2) / cnt : 0);
      }
    }
  }
  return o;
}
/* object nn's object moved to number num (the old one freed) */
static void replace_obj(PDoc *d, int num, int nn)
{
  PObj *n = d->xref[nn].obj;
  d->xref[nn].obj = pobj_new(PO_NULL);
  pobj_free(d->xref[num].obj);
  d->xref[num].obj = n;
  if (d->xref[num].type == 0) d->xref[num].type = 1;
  n->onum = num;
  d->changed = 1;
}
int optimize_images(PDoc *d, int max_dpi, int quality)
{
  Survey s;
  CEOps ops;
  int p, k, n = 0, nx = d->nxref;
  if (max_dpi < 36) max_dpi = 36;
  s.d = d;
  if (!(s.minscale = (double *)calloc((size_t)nx + 1, sizeof(double)))) return 0;
  memset(&ops, 0, sizeof ops);
  ops.ud = &s; ops.image = survey_image;
  for (p = 0; p < d->npages; p++) {
    PObj *pg = d->pages[p];
    size_t len, nl;
    u8 *data = pc_content(d, pg, &len), *r;
    if (!data) continue;
    r = ce_rewrite(d, data, len, pdf_page_attr(d, pg, "Resources"), NULL, &ops, &nl, NULL);
    free(r); free(data);
  }
  for (k = 1; k < nx; k++) {
    PObj *img;
    int w, h, comps, bpc, rgb, nw, nh, nn;
    u8 *smp, *sm;
    if (s.minscale[k] <= max_dpi * 1.3 || !(img = pdf_get(d, k)) || img->type != PO_STREAM) continue;
    if (!(smp = ce_image_samples(d, img, &w, &h, &comps, &bpc, &rgb))) continue;
    if (bpc != 8 || pdf_dget(d, img, "Decode") || (pobj_is_name(pdf_dget(d, img, "ColorSpace"), "Indexed"))) { free(smp); continue; }
    {
      PObj *cs = pdf_dget(d, img, "ColorSpace");
      if (cs && cs->type == PO_ARRAY && cs->u.arr.n && pobj_is_name(pdf_resolve(d, cs->u.arr.v[0]), "Indexed") && !rgb) { free(smp); continue; }
    }
    nw = (int)(w * max_dpi / s.minscale[k] + 0.5); nh = (int)(h * max_dpi / s.minscale[k] + 0.5);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    if (nw >= w || !(sm = shrink(smp, w, h, comps, nw, nh))) { free(smp); continue; }
    free(smp);
    nn = ce_image_store(d, img, sm, nw, nh, comps, 8, rgb, comps == 3 ? (quality > 0 ? quality : 75) : 0);
    free(sm);
    if (nn) { replace_obj(d, k, nn); n++; }
  }
  free(s.minscale);
  return n;
}

/* ------------------------------------------------------------------ identical streams */
typedef struct { u32 h; int num; } HEnt;
static int cmp_h(const void *a, const void *b)
{
  const HEnt *x = (const HEnt *)a, *y = (const HEnt *)b;
  if (x->h != y->h) return x->h < y->h ? -1 : 1;
  return x->num - y->num;
}
static void put_dict(Buf *b, const PObj *o, int depth)
{
  int i;
  if (!o || depth > 16) return;
  switch (o->type) {
  case PO_INT: buf_printf(b, "i%ld", o->u.i); break;
  case PO_REAL: buf_printf(b, "r%.6g", o->u.r); break;
  case PO_BOOL: buf_printf(b, "b%d", o->u.b); break;
  case PO_NAME: buf_addc(b, '/'); buf_add(b, o->u.str.s, (size_t)o->u.str.len); break;
  case PO_STRING: buf_printf(b, "(%d:", o->u.str.len); buf_add(b, o->u.str.s, (size_t)o->u.str.len); break;
  case PO_REF: buf_printf(b, "R%d", o->u.ref.num); break;
  case PO_ARRAY: buf_addc(b, '['); for (i = 0; i < o->u.arr.n; i++) put_dict(b, o->u.arr.v[i], depth + 1); buf_addc(b, ']'); break;
  case PO_DICT: case PO_STREAM:
    buf_adds(b, "<<");
    for (i = 0; i < o->u.dict.n; i++) { if (!strcmp(o->u.dict.k[i], "Length")) continue; buf_adds(b, o->u.dict.k[i]); buf_addc(b, ' '); put_dict(b, o->u.dict.v[i], depth + 1); }
    buf_adds(b, ">>");
    break;
  default: buf_addc(b, 'n');
  }
}
static u8 *stream_key(PDoc *d, PObj *o, size_t *n)
{
  Buf b;
  size_t len;
  u8 *data = pdf_stream_bytes(d, o, &len);
  if (!data) return NULL;
  buf_init(&b);
  put_dict(&b, o, 0);
  buf_addc(&b, 0);
  buf_add(&b, data, len);
  free(data);
  if (b.oom) { buf_free(&b); return NULL; }
  *n = b.len;
  return (u8 *)buf_take(&b);
}
int *optimize_dedupe(PDoc *d, int *found)
{
  HEnt *e = (HEnt *)malloc((size_t)d->nxref * sizeof(HEnt));
  int *alias = (int *)calloc((size_t)d->nxref + 1, sizeof(int)), k, n = 0, i, j;
  *found = 0;
  if (!e || !alias) { free(e); free(alias); return NULL; }
  for (k = 1; k < d->nxref; k++) {
    PObj *o = pdf_get(d, k);
    size_t len, x;
    u8 *key;
    u32 h = 2166136261u;
    if (!o || o->type != PO_STREAM || !(key = stream_key(d, o, &len))) continue;
    for (x = 0; x < len; x++) { h ^= key[x]; h *= 16777619u; }
    free(key);
    e[n].h = h; e[n].num = k; n++;
  }
  qsort(e, (size_t)n, sizeof(HEnt), cmp_h);
  for (i = 0; i < n; i = j) {
    for (j = i + 1; j < n && e[j].h == e[i].h; j++) ;
    if (j - i > 1) {
      int a, b;
      for (a = i; a < j; a++) {
        size_t la;
        u8 *ka;
        if (alias[e[a].num]) continue;
        if (!(ka = stream_key(d, pdf_get(d, e[a].num), &la))) continue;
        for (b = a + 1; b < j; b++) {
          size_t lb;
          u8 *kb;
          if (alias[e[b].num] || !(kb = stream_key(d, pdf_get(d, e[b].num), &lb))) continue;
          if (la == lb && !memcmp(ka, kb, la)) { alias[e[b].num] = e[a].num; (*found)++; }
          free(kb);
        }
        free(ka);
      }
    }
  }
  free(e);
  return alias;
}

/* ------------------------------------------------------------------ print production */
typedef struct { double min, width; int fill_cs, stroke_cs; PDoc *d; int target; } PP;
static void set_num(PObj *o, double v) { o->type = PO_REAL; o->u.r = v; }
static int hair_op(void *ud, char *kw, PObj **args, int *nargs, const CEState *s)
{
  PP *p = (PP *)ud;
  if (!strcmp(kw, "w") && *nargs >= 1 && pobj_num(args[*nargs - 1]) < p->min) { set_num(args[*nargs - 1], p->width); return 2; }
  return 0;
}
int fix_hairlines(PDoc *d, double min, double width)
{
  PP p;
  CEOps ops;
  int pg, n = 0;
  memset(&p, 0, sizeof p);
  p.min = min; p.width = width;
  memset(&ops, 0, sizeof ops);
  ops.ud = &p; ops.op = hair_op;
  for (pg = 0; pg < d->npages; pg++) n += ce_rewrite_page(d, pg, &ops);
  return n;
}
/* a colour (n components of a device space: 1 grey, 3 RGB, 4 CMYK) in the target space (CC_) */
static int conv(const double *in, int n, int target, double *out)
{
  double r, g, b, k;
  if (n == 1) r = g = b = in[0];
  else if (n == 3) { r = in[0]; g = in[1]; b = in[2]; }
  else { r = (1 - in[0]) * (1 - in[3]); g = (1 - in[1]) * (1 - in[3]); b = (1 - in[2]) * (1 - in[3]); }
  if (target == CC_GRAY) {
    if (n == 4) { double v = 1 - (0.3 * in[0] + 0.59 * in[1] + 0.11 * in[2] + in[3]); out[0] = v < 0 ? 0 : v; }
    else out[0] = 0.3 * r + 0.59 * g + 0.11 * b;
    return 1;
  }
  if (target == CC_RGB) { out[0] = r; out[1] = g; out[2] = b; return 3; }
  if (n == 4) { memcpy(out, in, 4 * sizeof(double)); return 4; }
  k = 1 - (r > g ? (r > b ? r : b) : (g > b ? g : b));
  if (k >= 1) { out[0] = out[1] = out[2] = 0; out[3] = 1; return 4; }
  out[0] = (1 - r - k) / (1 - k); out[1] = (1 - g - k) / (1 - k); out[2] = (1 - b - k) / (1 - k); out[3] = k;
  return 4;
}
static const char *const cc_space[3] = { "DeviceGray", "DeviceRGB", "DeviceCMYK" };
static const char *const cc_fill[3] = { "g", "rg", "k" };
static int colour_op(void *ud, char *kw, PObj **args, int *nargs, const CEState *s)
{
  PP *p = (PP *)ud;
  int stroke = kw[0] >= 'A' && kw[0] <= 'Z', n = 0, tn = p->target == CC_GRAY ? 1 : p->target == CC_RGB ? 3 : 4, i, sc = 0;
  double in[4], out[4];
  if ((!strcmp(kw, "g") || !strcmp(kw, "G")) && *nargs >= 1) n = 1;
  else if ((!strcmp(kw, "rg") || !strcmp(kw, "RG")) && *nargs >= 3) n = 3;
  else if ((!strcmp(kw, "k") || !strcmp(kw, "K")) && *nargs >= 4) n = 4;
  else if ((!strcmp(kw, "cs") || !strcmp(kw, "CS")) && *nargs >= 1 && args[*nargs - 1]->type == PO_NAME) {
    const char *nm = args[*nargs - 1]->u.str.s;
    int c = !strcmp(nm, "DeviceRGB") ? 3 : !strcmp(nm, "DeviceCMYK") ? 4 : !strcmp(nm, "DeviceGray") ? 1 : 0;
    if (stroke) p->stroke_cs = c; else p->fill_cs = c;
    if (c && c != tn) { pobj_free(args[*nargs - 1]); args[*nargs - 1] = pobj_name(cc_space[p->target]); return 2; }
    return 0;
  } else if (!strcmp(kw, "sc") || !strcmp(kw, "scn") || !strcmp(kw, "SC") || !strcmp(kw, "SCN")) {
    n = stroke ? p->stroke_cs : p->fill_cs;
    if (!n || *nargs < n) return 0;
    sc = 1;
  }
  if (!n || n == tn) return 0;
  for (i = 0; i < n; i++) in[i] = pobj_num(args[*nargs - n + i]);
  conv(in, n, p->target, out);
  for (i = 0; i < *nargs; i++) pobj_free(args[i]);
  for (i = 0; i < tn; i++) args[i] = pobj_real(out[i]);
  *nargs = tn;
  if (!sc) { strcpy(kw, cc_fill[p->target]); if (stroke) kw[0] = (char)(kw[0] - 32), kw[1] = kw[1] ? (char)(kw[1] - 32) : 0; }
  return 2;
}
static int colour_image(void *ud, PObj *img, int inl, const double *m, int *nn)
{
  PP *p = (PP *)ud;
  int w, h, comps, bpc, rgb, i, num, tn = p->target == CC_GRAY ? 1 : p->target == CC_RGB ? 3 : 4, k;
  u8 *s, *g;
  PObj *tmp;
  if (inl || (num = om_num_of(p->d, img)) == 0) return 0;
  if (!(s = ce_image_samples(p->d, img, &w, &h, &comps, &bpc, &rgb))) return 0;
  {
    PObj *cs = pdf_dget(p->d, img, "ColorSpace");
    int dev = rgb || (cs && cs->type == PO_NAME && (!strcmp(cs->u.str.s, "DeviceRGB") || !strcmp(cs->u.str.s, "DeviceCMYK") || !strcmp(cs->u.str.s, "DeviceGray")));
    if (rgb) comps = 3;
    if (bpc != 8 || (comps != 1 && comps != 3 && comps != 4) || comps == tn || !dev || pdf_dget(p->d, img, "Decode") || pobj_is_name(pdf_dget(p->d, img, "ImageMask"), "true")) { free(s); return 0; }
  }
  if (!(g = (u8 *)malloc((size_t)w * h * (size_t)tn))) { free(s); return 0; }
  for (i = 0; i < w * h; i++) {
    double in[4], out[4];
    for (k = 0; k < comps; k++) in[k] = s[(size_t)i * comps + k] / 255.0;
    conv(in, comps, p->target, out);
    for (k = 0; k < tn; k++) { double v = out[k] * 255 + 0.5; g[(size_t)i * tn + k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }
  }
  free(s);
  if (!(tmp = pobj_copy(img))) { free(g); return 0; }
  tmp->type = PO_DICT; free(tmp->sdata); tmp->sdata = NULL;
  pobj_dict_set(tmp, "ColorSpace", pobj_name(cc_space[p->target]));
  pobj_dict_set(tmp, "BitsPerComponent", pobj_int(8));
  *nn = ce_image_store(p->d, tmp, g, w, h, tn, 8, tn == 3, 0);
  pobj_free(tmp);
  free(g);
  if (*nn) { replace_obj(p->d, num, *nn); *nn = 0; }
  return 0;
}
int convert_colors(PDoc *d, int target)
{
  PP p;
  CEOps ops;
  int pg, n = 0;
  if (target < CC_GRAY || target > CC_CMYK) return 0;
  memset(&p, 0, sizeof p);
  p.d = d; p.target = target;
  memset(&ops, 0, sizeof ops);
  ops.ud = &p; ops.op = colour_op; ops.image = colour_image;
  for (pg = 0; pg < d->npages; pg++) { p.fill_cs = p.stroke_cs = 0; n += ce_rewrite_page(d, pg, &ops); }
  return n;
}
int convert_to_gray(PDoc *d) { return convert_colors(d, CC_GRAY); }
int printer_marks(PDoc *d, double bleed)
{
  int p, n = 0;
  double margin;
  if (bleed < 0) bleed = 0;
  margin = bleed + 18;
  for (p = 0; p < d->npages; p++) {
    PObj *pg = d->pages[p];
    double b[4], mb[4];
    int rot;
    Buf o;
    pdf_page_box(d, p, b, &rot);
    mb[0] = b[0] - margin; mb[1] = b[1] - margin; mb[2] = b[2] + margin; mb[3] = b[3] + margin;
    pobj_dict_set(pg, "TrimBox", om_arr(b, 4));
    {
      double bl[4];
      bl[0] = b[0] - bleed; bl[1] = b[1] - bleed; bl[2] = b[2] + bleed; bl[3] = b[3] + bleed;
      pobj_dict_set(pg, "BleedBox", om_arr(bl, 4));
    }
    pobj_dict_set(pg, "MediaBox", om_arr(mb, 4));
    pobj_dict_del(pg, "CropBox");
    buf_init(&o);
    buf_adds(&o, "0 0 0 1 K 0.25 w\n");
    {
      /* at each corner two short lines along the trim edges, outside the bleed */
      int c;
      for (c = 0; c < 4; c++) {
        double x = c & 1 ? b[2] : b[0], y = c & 2 ? b[3] : b[1], sx = c & 1 ? 1 : -1, sy = c & 2 ? 1 : -1;
        om_num(&o, x + sx * (bleed + 3)); om_num(&o, y); buf_adds(&o, "m "); om_num(&o, x + sx * margin); om_num(&o, y); buf_adds(&o, "l S\n");
        om_num(&o, x); om_num(&o, y + sy * (bleed + 3)); buf_adds(&o, "m "); om_num(&o, x); om_num(&o, y + sy * margin); buf_adds(&o, "l S\n");
      }
    }
    if (!o.oom && pc_add_content(d, p, o.data, o.len, 0, "marks")) n++;
    buf_free(&o);
  }
  d->changed = 1;
  return n;
}
