/*
 * render.c - the content stream interpreter (see render.h): the graphics state, colour spaces,
 * paths filled, stroked and clipped, text (with a cache of glyph bitmaps), form XObjects,
 * optional content and annotations' appearances. Images are drawn by pimage.c, shadings and
 * patterns by shade.c.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "render_int.h"

#define MAXOPS 64                    /* operands an operator may have */
#define MAXFORMDEPTH 12              /* forms within forms (and patterns and Type 3 glyphs) */

/* ------------------------------------------------------------------ the cache */
typedef struct GEntry {
  const PFont *prog; int gid;
  i32 ka, kb, kc, kd, fx, fy;        /* the glyph matrix (1/2^20) and the origin's fraction (quarter pixels) */
  int x0, y0, w, h;                  /* the bitmap's place relative to the whole-pixel origin */
  u8 *bm;
  struct GEntry *next;
} GEntry;
#define GBUCKETS 509
#define GBUDGET (1536L * 1024)
typedef struct { PObj *dict; PdfFont *font; } FEntry;
struct RCache {
  PDoc *d;
  FEntry *fonts; int nfonts, capfonts;
  GEntry *g[GBUCKETS]; long gbytes;
};
RCache *rcache_new(PDoc *d)
{
  RCache *c = (RCache *)calloc(1, sizeof *c);
  if (c) c->d = d;
  return c;
}
static void glyphs_clear(RCache *c)
{
  int i;
  for (i = 0; i < GBUCKETS; i++) {
    GEntry *e = c->g[i];
    while (e) { GEntry *n = e->next; free(e->bm); free(e); e = n; }
    c->g[i] = NULL;
  }
  c->gbytes = 0;
}
void rcache_free(RCache *c)
{
  int i;
  if (!c) return;
  for (i = 0; i < c->nfonts; i++) font_free(c->fonts[i].font);
  free(c->fonts);
  glyphs_clear(c);
  free(c);
}
/* a font dictionary's loaded font (loaded once per document) */
static PdfFont *cached_font(RCache *c, PDoc *d, PObj *dict)
{
  int i;
  PdfFont *f;
  dict = pdf_dict_of(d, dict);
  if (!dict) return NULL;
  for (i = 0; i < c->nfonts; i++) if (c->fonts[i].dict == dict) return c->fonts[i].font;
  if (c->nfonts >= c->capfonts) {
    int nc = c->capfonts ? c->capfonts * 2 : 16;
    FEntry *n2 = (FEntry *)realloc(c->fonts, (size_t)nc * sizeof(FEntry));
    if (!n2) return NULL;
    c->fonts = n2; c->capfonts = nc;
  }
  f = font_load(d, dict);
  c->fonts[c->nfonts].dict = dict; c->fonts[c->nfonts].font = f;
  c->nfonts++;
  return f;
}

/* ------------------------------------------------------------------ colour spaces */
static CSpace dev_gray = { CS_GRAY, 1, NULL, NULL, 0, NULL, { 0 }, 1 << 30 };
static CSpace dev_rgb = { CS_RGB, 3, NULL, NULL, 0, NULL, { 0 }, 1 << 30 };
static CSpace dev_cmyk = { CS_CMYK, 4, NULL, NULL, 0, NULL, { 0 }, 1 << 30 };
static CSpace pat_plain = { CS_PATTERN, 0, NULL, NULL, 0, NULL, { 0 }, 1 << 30 };
CSpace *cs_device(int kind) { return kind == CS_RGB ? &dev_rgb : kind == CS_CMYK ? &dev_cmyk : kind == CS_PATTERN ? &pat_plain : &dev_gray; }
CSpace *cs_retain(CSpace *c) { if (c && c->refs < (1 << 30)) c->refs++; return c; }
void cs_release(CSpace *c)
{
  if (!c || c->refs >= (1 << 30)) return;
  if (--c->refs > 0) return;
  cs_release(c->base);
  free(c->lookup);
  func_free(c->tint);
  free(c);
}
static CSpace *cs_new(int kind, int n)
{
  CSpace *c = (CSpace *)calloc(1, sizeof *c);
  if (c) { c->kind = kind; c->n = n; c->refs = 1; }
  return c;
}
CSpace *cs_load(PDoc *d, PObj *o, PObj *res, int depth)
{
  const char *n;
  PObj *a;
  if (depth > 8) return NULL;
  o = pdf_resolve(d, o);
  if (!o) return NULL;
  if (o->type == PO_NAME) {
    n = o->u.str.s;
    if (!strcmp(n, "DeviceGray") || !strcmp(n, "G") || !strcmp(n, "CalGray")) return &dev_gray;
    if (!strcmp(n, "DeviceRGB") || !strcmp(n, "RGB") || !strcmp(n, "CalRGB")) return &dev_rgb;
    if (!strcmp(n, "DeviceCMYK") || !strcmp(n, "CMYK")) return &dev_cmyk;
    if (!strcmp(n, "Pattern")) return &pat_plain;
    if (!strcmp(n, "I") || !strcmp(n, "Indexed")) return NULL;
    /* a named space in the resources */
    if (res) {
      PObj *csd = pdf_dget(d, res, "ColorSpace"), *v = csd ? pdf_dget(d, csd, n) : NULL;
      if (v && v != o) return cs_load(d, v, res, depth + 1);
    }
    return NULL;
  }
  if (o->type != PO_ARRAY || o->u.arr.n < 1) return NULL;
  a = pdf_resolve(d, o->u.arr.v[0]);
  if (!a || a->type != PO_NAME) return NULL;
  n = a->u.str.s;
  if (!strcmp(n, "DeviceGray") || !strcmp(n, "CalGray")) return &dev_gray;
  if (!strcmp(n, "DeviceRGB") || !strcmp(n, "CalRGB")) return &dev_rgb;
  if (!strcmp(n, "DeviceCMYK")) return &dev_cmyk;
  if (!strcmp(n, "ICCBased") && o->u.arr.n >= 2) {
    PObj *s = pdf_resolve(d, o->u.arr.v[1]);
    long k = s ? pdf_dint(d, s, "N", 0) : 0;
    PObj *alt = s ? pdf_dget(d, s, "Alternate") : NULL;
    if (k == 1) return &dev_gray;
    if (k == 3) return &dev_rgb;
    if (k == 4) return &dev_cmyk;
    if (alt) return cs_load(d, alt, res, depth + 1);
    return &dev_rgb;
  }
  if (!strcmp(n, "Lab")) {
    CSpace *c = cs_new(CS_LAB, 3);
    PObj *dict = o->u.arr.n >= 2 ? pdf_resolve(d, o->u.arr.v[1]) : NULL, *r = dict ? pdf_dget(d, dict, "Range") : NULL;
    int i;
    if (!c) return NULL;
    c->range[0] = -100; c->range[1] = 100; c->range[2] = -100; c->range[3] = 100;
    if (r && r->type == PO_ARRAY && r->u.arr.n >= 4) for (i = 0; i < 4; i++) c->range[i] = pobj_num(pdf_resolve(d, r->u.arr.v[i]));
    return c;
  }
  if ((!strcmp(n, "Indexed") || !strcmp(n, "I")) && o->u.arr.n >= 4) {
    CSpace *base = cs_load(d, o->u.arr.v[1], res, depth + 1), *c;
    PObj *lk = pdf_resolve(d, o->u.arr.v[3]);
    int hival = dbl_int(pobj_num(pdf_resolve(d, o->u.arr.v[2])));
    size_t need, have = 0;
    u8 *data = NULL;
    if (!base || base->kind == CS_PATTERN || base->kind == CS_INDEXED) { cs_release(base); return NULL; }
    if (hival < 0) hival = 0;
    if (hival > 255) hival = 255;
    if (!(c = cs_new(CS_INDEXED, 1))) { cs_release(base); return NULL; }
    c->base = base; c->hival = hival;
    need = (size_t)(hival + 1) * (size_t)base->n;
    if (lk && lk->type == PO_STRING) { data = (u8 *)lk->u.str.s; have = (size_t)lk->u.str.len; }
    else if (lk && lk->type == PO_STREAM) { data = pdf_stream_data(d, lk, &have); }
    if (!(c->lookup = (u8 *)calloc(need + 1, 1))) { if (lk && lk->type == PO_STREAM) free(data); cs_release(c); return NULL; }
    if (data) memcpy(c->lookup, data, have < need ? have : need);
    if (lk && lk->type == PO_STREAM) free(data);
    return c;
  }
  if ((!strcmp(n, "Separation") || !strcmp(n, "DeviceN")) && o->u.arr.n >= 4) {
    int sep = n[0] == 'S';
    PObj *names = pdf_resolve(d, o->u.arr.v[1]);
    int k = sep ? 1 : names && names->type == PO_ARRAY ? names->u.arr.n : 1;
    CSpace *alt = cs_load(d, o->u.arr.v[2], res, depth + 1), *c;
    if (sep && names && names->type == PO_NAME && !strcmp(names->u.str.s, "None")) { cs_release(alt); return cs_new(CS_NONE, 1); }
    if (!alt) alt = &dev_gray;
    if (k < 1) k = 1;
    if (k > 32) k = 32;
    if (!(c = cs_new(CS_SEP, k))) { cs_release(alt); return NULL; }
    c->base = alt;
    c->tint = func_load(d, o->u.arr.v[3]);
    if (sep && names && names->type == PO_NAME && !strcmp(names->u.str.s, "All")) { func_free(c->tint); c->tint = NULL; }
    return c;
  }
  if (!strcmp(n, "Pattern")) {
    CSpace *c = cs_new(CS_PATTERN, 0);
    if (!c) return NULL;
    if (o->u.arr.n >= 2) { c->base = cs_load(d, o->u.arr.v[1], res, depth + 1); if (c->base) c->n = c->base->n; }
    return c;
  }
  return NULL;
}
void cs_default(const CSpace *c, double *v)
{
  int i;
  for (i = 0; i < 32; i++) v[i] = 0;
  if (c->kind == CS_CMYK) v[3] = 1;
  else if (c->kind == CS_SEP || c->kind == CS_NONE) for (i = 0; i < c->n; i++) v[i] = 1;
  else if (c->kind == CS_LAB) v[0] = 0;
}
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
void cs_to_rgb(const CSpace *c, const double *v, double *rgb)
{
  switch (c->kind) {
  case CS_GRAY: rgb[0] = rgb[1] = rgb[2] = clamp01(v[0]); return;
  case CS_RGB: rgb[0] = clamp01(v[0]); rgb[1] = clamp01(v[1]); rgb[2] = clamp01(v[2]); return;
  case CS_CMYK: {
    /* a little better than the naive formula: black mixes in a little warmth, as press inks do */
    double cc = clamp01(v[0]), m = clamp01(v[1]), y = clamp01(v[2]), k = clamp01(v[3]);
    rgb[0] = (1 - cc) * (1 - k); rgb[1] = (1 - m) * (1 - k); rgb[2] = (1 - y) * (1 - k);
    return; }
  case CS_LAB: {
    /* L*a*b* (D50) to sRGB */
    double L = v[0], a = v[1], b = v[2], fy = (L + 16) / 116, fx = fy + a / 500, fz = fy - b / 200, X, Y, Z, r, g, bb;
    X = fx > 6.0 / 29 ? fx * fx * fx : (fx - 16.0 / 116) * 3 * (6.0 / 29) * (6.0 / 29);
    Y = fy > 6.0 / 29 ? fy * fy * fy : (fy - 16.0 / 116) * 3 * (6.0 / 29) * (6.0 / 29);
    Z = fz > 6.0 / 29 ? fz * fz * fz : (fz - 16.0 / 116) * 3 * (6.0 / 29) * (6.0 / 29);
    X *= 0.9642; Z *= 0.8249;
    r = 3.1339 * X - 1.6169 * Y - 0.4906 * Z; g = -0.9788 * X + 1.9161 * Y + 0.0335 * Z; bb = 0.0719 * X - 0.2290 * Y + 1.4052 * Z;
    rgb[0] = clamp01(r > 0.0031308 ? 1.055 * pow(r, 1 / 2.4) - 0.055 : 12.92 * r);
    rgb[1] = clamp01(g > 0.0031308 ? 1.055 * pow(g, 1 / 2.4) - 0.055 : 12.92 * g);
    rgb[2] = clamp01(bb > 0.0031308 ? 1.055 * pow(bb, 1 / 2.4) - 0.055 : 12.92 * bb);
    return; }
  case CS_INDEXED: {
    int i = dbl_int(v[0] + 0.5), k;
    double b[4];
    if (i < 0) i = 0;
    if (i > c->hival) i = c->hival;
    for (k = 0; k < c->base->n && k < 4; k++) b[k] = c->lookup[i * c->base->n + k] / 255.0;
    if (c->base->kind == CS_LAB) { b[0] *= 100; b[1] = b[1] * 255 - 128; b[2] = b[2] * 255 - 128; }
    cs_to_rgb(c->base, b, rgb);
    return; }
  case CS_SEP: {
    double o[FN_MAXIO];
    if (!c->tint) { rgb[0] = rgb[1] = rgb[2] = 1 - clamp01(v[0]); return; }
    func_eval(c->tint, v, c->n, o);
    cs_to_rgb(c->base, o, rgb);
    return; }
  case CS_PATTERN:
    if (c->base) { cs_to_rgb(c->base, v, rgb); return; }
    /* fall through */
  default: rgb[0] = rgb[1] = rgb[2] = 0;
  }
}
static void set_rgb(u8 *out, const CSpace *cs, const double *v)
{
  double rgb[3];
  int i;
  cs_to_rgb(cs, v, rgb);
  for (i = 0; i < 3; i++) out[i] = (u8)(rgb[i] * 255 + 0.5);
}

/* ------------------------------------------------------------------ the clip */
typedef struct Mask { int refs; u8 *m; } Mask;   /* (shared between clips that only narrowed the bounds) */
static Clip *clip_new(int x0, int y0, int x1, int y1)
{
  Clip *c = (Clip *)calloc(1, sizeof *c);
  if (c) { c->refs = 1; c->x0 = x0; c->y0 = y0; c->x1 = x1; c->y1 = y1; }
  return c;
}
static Mask *mask_of(const Clip *c) { return (Mask *)(void *)c->mask; }
static void clip_release(Clip *c)
{
  if (!c || --c->refs > 0) return;
  if (c->mask) { Mask *m = mask_of(c); if (--m->refs <= 0) { free(m->m); free(m); } }
  free(c);
}
static Clip *clip_retain(Clip *c) { if (c) c->refs++; return c; }
u8 clip_at(const Clip *c, int W, int x, int y)
{
  if (!c->mask) return 255;
  return mask_of(c)->m[(long)y * W + x];
}
int clip_box(const Render *r, int *x0, int *y0, int *x1, int *y1)
{
  const Clip *c = r->gs[r->ngs].clip;
  *x0 = c->x0; *y0 = c->y0; *x1 = c->x1; *y1 = c->y1;
  return *x1 > *x0 && *y1 > *y0;
}
typedef struct { u8 *nm; const u8 *old; int W; } ClipSpan;
static void clip_span(void *ud, int y, int x0, int x1, const u8 *cov)
{
  ClipSpan *s = (ClipSpan *)ud;
  long o = (long)y * s->W;
  int x;
  for (x = x0; x < x1; x++) {
    int v = cov[x - x0];
    if (s->old) v = v * s->old[o + x] / 255;
    s->nm[o + x] = (u8)v;
  }
}
/* whether a device path is an axis-aligned rectangle; its pixel bounds if so */
static int path_is_rect(const Path *p, int *x0, int *y0, int *x1, int *y1)
{
  int i, n;
  double xs[5], ys[5], a, b, c, d;
  if (p->nops < 4 || p->nops > 6 || p->ops[0] != PATH_M) return 0;
  for (i = 1, n = 1; i < p->nops; i++) {
    if (p->ops[i] == PATH_L) { if (n >= 5) return 0; n++; }
    else if (p->ops[i] == PATH_Z) { if (i != p->nops - 1) return 0; }
    else return 0;
  }
  if (n == 5) { if (fabs(p->pts[8] - p->pts[0]) > 0.01 || fabs(p->pts[9] - p->pts[1]) > 0.01) return 0; n = 4; }
  if (n != 4) return 0;
  for (i = 0; i < 4; i++) { xs[i] = p->pts[i * 2]; ys[i] = p->pts[i * 2 + 1]; }
  /* each side horizontal or vertical */
  for (i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    if (fabs(xs[i] - xs[j]) > 0.01 && fabs(ys[i] - ys[j]) > 0.01) return 0;
  }
  a = xs[0]; b = xs[0]; c = ys[0]; d = ys[0];
  for (i = 1; i < 4; i++) { if (xs[i] < a) a = xs[i]; if (xs[i] > b) b = xs[i]; if (ys[i] < c) c = ys[i]; if (ys[i] > d) d = ys[i]; }
  /* (far beyond any page, kept to where the arithmetic below is defined) */
  if (!(a > -1e8)) a = -1e8;
  if (!(b < 1e8)) b = 1e8;
  if (!(c > -1e8)) c = -1e8;
  if (!(d < 1e8)) d = 1e8;
  if (a > b || c > d) return 0;
  *x0 = dbl_int(floor(a + 0.5)); *x1 = dbl_int(floor(b + 0.5)); *y0 = dbl_int(floor(c + 0.5)); *y1 = dbl_int(floor(d + 0.5));
  if (*x1 <= *x0 && b - a > 0.1) *x1 = *x0 + 1;     /* (a thin rectangle keeps a pixel) */
  if (*y1 <= *y0 && d - c > 0.1) *y1 = *y0 + 1;
  return 1;
}
/* the current clip narrowed to a device path */
static void clip_to(Render *r, const Path *dev, int evenodd)
{
  GState *g = GS(r);
  Clip *old = g->clip, *nc;
  int x0, y0, x1, y1;
  double bx0, by0, bx1, by1;
  if (path_is_rect(dev, &x0, &y0, &x1, &y1)) {
    if (x0 < old->x0) x0 = old->x0;
    if (y0 < old->y0) y0 = old->y0;
    if (x1 > old->x1) x1 = old->x1;
    if (y1 > old->y1) y1 = old->y1;
    if (!(nc = clip_new(x0, y0, x1 > x0 ? x1 : x0, y1 > y0 ? y1 : y0))) return;
    if (old->mask) { nc->mask = old->mask; mask_of(old)->refs++; }
  } else {
    Mask *m;
    ClipSpan cs;
    if (!path_bounds(dev, &bx0, &by0, &bx1, &by1)) { bx0 = by0 = bx1 = by1 = 0; }
    x0 = dbl_int(floor(bx0)); y0 = dbl_int(floor(by0)); x1 = dbl_int(ceil(bx1)); y1 = dbl_int(ceil(by1));
    if (x0 < old->x0) x0 = old->x0;
    if (y0 < old->y0) y0 = old->y0;
    if (x1 > old->x1) x1 = old->x1;
    if (y1 > old->y1) y1 = old->y1;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    if (!(nc = clip_new(x0, y0, x1, y1))) return;
    if (!(m = (Mask *)calloc(1, sizeof *m)) || !(m->m = (u8 *)calloc((size_t)r->W * r->H, 1))) { free(m); free(nc); return; }
    m->refs = 1;
    nc->mask = (u8 *)(void *)m;
    cs.nm = m->m; cs.old = old->mask ? mask_of(old)->m : NULL; cs.W = r->W;
    rast_path(dev, evenodd, x0, y0, x1, y1, clip_span, &cs);
  }
  clip_release(old);
  g->clip = nc;
}

/* ------------------------------------------------------------------ painting */
void put_pixel(Render *r, int x, int y, const u8 *rgb, int a, int blend)
{
  long o = ((long)y * r->W + x);
  u8 *p = r->rgb + o * 3;
  int k;
  if (a <= 0) return;
  if (r->alpha) {
    /* a pattern cell being drawn: colour over transparency, coverage kept */
    int da = r->alpha[o], na = a + da * (255 - a) / 255;
    if (na > 0) for (k = 0; k < 3; k++) p[k] = (u8)((rgb[k] * a + p[k] * da * (255 - a) / 255) / na);
    r->alpha[o] = (u8)na;
    return;
  }
  for (k = 0; k < 3; k++) {
    int s = rgb[k], dv = p[k];
    switch (blend) {
    case 1: s = s * dv / 255; break;
    case 2: s = s + dv - s * dv / 255; break;
    case 3: s = s < dv ? s : dv; break;
    case 4: s = s > dv ? s : dv; break;
    }
    p[k] = a >= 255 ? (u8)s : (u8)((s * a + dv * (255 - a) + 127) / 255);
  }
}
void paint_span(void *ud, int y, int x0, int x1, const u8 *cov)
{
  Paint *p = (Paint *)ud;
  Render *r = p->r;
  const Clip *c = p->clip;
  const u8 *mrow = c->mask ? mask_of(c)->m + (long)y * r->W : NULL;
  int x;
  if (y < c->y0 || y >= c->y1) return;
  if (x0 < c->x0) { cov += c->x0 - x0; x0 = c->x0; }
  if (x1 > c->x1) x1 = c->x1;
  for (x = x0; x < x1; x++) {
    int a = cov[x - x0];
    if (!a) continue;
    if (mrow) { a = a * mrow[x] / 255; if (!a) continue; }
    if (p->shade) {
      u8 rgb[3];
      int sa = 255;
      p->shade(p->sud, x + 0.5, y + 0.5, rgb, &sa);
      put_pixel(r, x, y, rgb, a * sa / 255 * p->alpha / 255, p->blend);
    } else put_pixel(r, x, y, p->rgb, p->alpha >= 255 ? a : a * p->alpha / 255, p->blend);
  }
}
/* a device path filled with a solid colour through the clip */
static void fill_solid(Render *r, const Path *dev, int evenodd, const u8 *rgb, double alpha)
{
  Paint p;
  const Clip *c = GS(r)->clip;
  if (r->hidden || r->o->text_only) return;
  memset(&p, 0, sizeof p);
  p.r = r; p.rgb = rgb; p.alpha = dbl_int(alpha * 255 + 0.5); p.clip = c; p.blend = GS(r)->blend;
  if (p.alpha <= 0) return;
  rast_path(dev, evenodd, c->x0, c->y0, c->x1, c->y1, paint_span, &p);
}
static void stroke_style(Render *r, StrokeStyle *st)
{
  GState *g = GS(r);
  st->width = g->lw; st->cap = g->cap; st->join = g->join; st->miter = g->miter > 1 ? g->miter : 1;
  st->dash = g->ndash ? g->dash : NULL; st->ndash = g->ndash; st->phase = g->phase;
  st->min_width = 1.0;
}
/* the current path painted: fill and/or stroke, then the clip if one is pending */
static void paint_path(Render *r, int fill, int stroke, int evenodd)
{
  GState *g = GS(r);
  Path dev;
  path_init(&dev);
  path_append(&dev, &r->path, &g->ctm);
  if (fill && !r->hidden && !r->o->text_only) {
    if (g->fcs->kind == CS_PATTERN && g->fpat) { if (!fill_with_pattern(r, g->fpat, &dev, evenodd, 0) && g->fcs->base) fill_solid(r, &dev, evenodd, g->frgb, g->fa); }
    else if (g->fcs->kind != CS_NONE) fill_solid(r, &dev, evenodd, g->frgb, g->fa);
  }
  if (stroke && !r->hidden && !r->o->text_only) {
    StrokeStyle st;
    Path out;
    path_init(&out);
    stroke_style(r, &st);
    stroke_path(&r->path, &st, &g->ctm, &out);
    if (g->scs->kind == CS_PATTERN && g->spat) fill_with_pattern(r, g->spat, &out, 0, 1);
    else if (g->scs->kind != CS_NONE) fill_solid(r, &out, 0, g->srgb, g->sa);
    path_free(&out);
  }
  if (r->clip_pending) clip_to(r, &dev, r->clip_pending == 2);
  r->clip_pending = 0;
  path_free(&dev);
  path_reset(&r->path);
}

/* ------------------------------------------------------------------ text */
static u32 hashg(const PFont *p, int gid, i32 a, i32 b, i32 c, i32 d, i32 fx, i32 fy)
{
  u32 h = (u32)(size_t)p * 2654435761u;
  h ^= (u32)gid * 40503u; h ^= (u32)a * 7u + (u32)b * 131u + (u32)c * 1031u + (u32)d * 8191u + (u32)fx * 3u + (u32)fy * 17u;
  return h % GBUCKETS;
}
typedef struct { u8 *bm; int x0, y0, w; } BmSpan;
static void bm_span(void *ud, int y, int x0, int x1, const u8 *cov)
{
  BmSpan *b = (BmSpan *)ud;
  memcpy(b->bm + (long)(y - b->y0) * b->w + (x0 - b->x0), cov, (size_t)(x1 - x0));
}
/* a glyph drawn by its cached bitmap (made if need be); 0 if it is too big to cache */
static int glyph_cached(Render *r, const PFont *prog, int gid, const Mat *gm, const u8 *rgb, double alpha)
{
  RCache *c = r->c;
  i32 ka, kb, kc, kd, fx, fy;
  double ox, oy;
  u32 h;
  GEntry *e;
  int x, y, a0 = dbl_int(alpha * 255 + 0.5);
  const Clip *cl = GS(r)->clip;
  if (!(fabs(gm->a) + fabs(gm->b) <= 0.4) || !(fabs(gm->c) + fabs(gm->d) <= 0.4)) return 0;   /* (over about 400 pixels: drawn directly) */
  /* (the keys worked out after that test, and a glyph far off the page is nothing) */
  if (!(fabs(gm->e) < 1e8 && fabs(gm->f) < 1e8)) return 1;
  ka = (i32)floor(gm->a * 1048576 + 0.5); kb = (i32)floor(gm->b * 1048576 + 0.5); kc = (i32)floor(gm->c * 1048576 + 0.5); kd = (i32)floor(gm->d * 1048576 + 0.5);
  ox = floor(gm->e); oy = floor(gm->f);
  fx = (i32)floor((gm->e - ox) * 4); fy = (i32)floor((gm->f - oy) * 4);
  h = hashg(prog, gid, ka, kb, kc, kd, fx, fy);
  for (e = c->g[h]; e; e = e->next)
    if (e->prog == prog && e->gid == gid && e->ka == ka && e->kb == kb && e->kc == kc && e->kd == kd && e->fx == fx && e->fy == fy) break;
  if (!e) {
    Path gp;
    Mat m;
    double bx0, by0, bx1, by1;
    BmSpan bs;
    path_init(&gp);
    if (!pf_glyph_outline((PFont *)prog, gid, &gp)) { path_free(&gp); return 1; }
    m = *gm; m.e = fx / 4.0 + 0.125; m.f = fy / 4.0 + 0.125;
    path_transform(&gp, &m);
    if (!(e = (GEntry *)calloc(1, sizeof *e))) { path_free(&gp); return 0; }
    e->prog = prog; e->gid = gid; e->ka = ka; e->kb = kb; e->kc = kc; e->kd = kd; e->fx = fx; e->fy = fy;
    if (path_bounds(&gp, &bx0, &by0, &bx1, &by1)) {
      e->x0 = dbl_int(floor(bx0)); e->y0 = dbl_int(floor(by0));
      e->w = dbl_int(ceil(bx1)) - e->x0 + 1; e->h = dbl_int(ceil(by1)) - e->y0 + 1;
      if (e->w > 0 && e->h > 0 && e->w < 2000 && e->h < 2000 && (e->bm = (u8 *)calloc((size_t)e->w * e->h, 1))) {
        bs.bm = e->bm; bs.x0 = e->x0; bs.y0 = e->y0; bs.w = e->w;
        rast_path(&gp, 0, e->x0, e->y0, e->x0 + e->w, e->y0 + e->h, bm_span, &bs);
      } else e->w = e->h = 0;
    }
    path_free(&gp);
    if (c->gbytes > GBUDGET) glyphs_clear(c);
    c->gbytes += (long)e->w * e->h + (long)sizeof *e;
    e->next = c->g[h]; c->g[h] = e;
  }
  if (!e->bm || r->hidden) return 1;
  /* blitted through the clip */
  for (y = 0; y < e->h; y++) {
    int dy = (int)oy + e->y0 + y;
    const u8 *row = e->bm + (long)y * e->w;
    if (dy < cl->y0 || dy >= cl->y1) continue;
    for (x = 0; x < e->w; x++) {
      int dx = (int)ox + e->x0 + x, a = row[x];
      if (!a || dx < cl->x0 || dx >= cl->x1) continue;
      if (cl->mask) a = a * mask_of(cl)->m[(long)dy * r->W + dx] / 255;
      if (a0 < 255) a = a * a0 / 255;
      put_pixel(r, dx, dy, rgb, a, GS(r)->blend);
    }
  }
  return 1;
}
static void run_type3_glyph(Render *r, PdfFont *f, u32 code, const Mat *trm);
/* one glyph of the current font at the text matrix: drawn, clipped and/or recorded */
static void show_glyph(Render *r, PdfFont *f, u32 code, int nbytes, double w0)
{
  GState *g = GS(r);
  double th = g->th / 100;
  Mat tfm = mat_make(g->size * th, 0, 0, g->size, 0, g->rise), tu = mat_mul(tfm, r->tm);
  int mode = g->tmode;
  /* the text: its box on the page */
  if (r->o->text && !r->hidden) {
    u32 u[8];
    int n = font_text(f, code, nbytes, u, 8), i;
    Mat tp = mat_mul(tu, mat_mul(g->ctm, r->page_inv));
    double adv = w0 / 1000.0, x[4], y[4], bx0, by0, bx1, by1, dx, dy;
    double lo = -0.22, hi = 0.78;
    int dir;
    mat_apply(&tp, 0, lo, &x[0], &y[0]); mat_apply(&tp, adv, lo, &x[1], &y[1]);
    mat_apply(&tp, adv, hi, &x[2], &y[2]); mat_apply(&tp, 0, hi, &x[3], &y[3]);
    bx0 = bx1 = x[0]; by0 = by1 = y[0];
    for (i = 1; i < 4; i++) { if (x[i] < bx0) bx0 = x[i]; if (x[i] > bx1) bx1 = x[i]; if (y[i] < by0) by0 = y[i]; if (y[i] > by1) by1 = y[i]; }
    dx = tp.a; dy = tp.b;
    dir = fabs(dx) >= fabs(dy) ? (dx >= 0 ? 0 : 2) : (dy > 0 ? 1 : 3);
    if (!n) { u[0] = 0; n = 1; }
    for (i = 0; i < n; i++) {
      /* (a ligature's characters share its box, split along the baseline) */
      double fa = (double)i / n, fb = (double)(i + 1) / n;
      if (dir == 0) textpage_add(r->o->text, u[i], bx0 + (bx1 - bx0) * fa, by0, bx0 + (bx1 - bx0) * fb, by1, mat_expansion(&tp), dir);
      else textpage_add(r->o->text, u[i], bx0, by0, bx1, by1, mat_expansion(&tp), dir);
    }
  }
  if (mode == 3 || r->o->text_only || r->hidden) return;
  if (f->kind == PFT_TYPE3) { if (mode != 7) run_type3_glyph(r, f, code, &tu); return; }
  {
    int gid = font_gid(f, code, nbytes);
    double fm[6];
    Mat gm, gmu;
    Path gp, dev;
    if (gid < 0 || !f->prog || (gid == 0 && f->substitute)) return;   /* (a stand-in's missing glyph: nothing, rather than its box) */
    pf_font_matrix(f->prog, fm);
    gmu = mat_mul(mat_make(fm[0], fm[1], fm[2], fm[3], fm[4], fm[5]), tu);
    if (f->substitute && f->kind == PFT_SIMPLE && w0 > 0) {
      /* a stand-in squeezed or stretched to the PDF's own width */
      double aw = pf_glyph_advance(f->prog, gid) * fm[0] * 1000;
      if (aw > 1 && (w0 / aw < 0.85 || w0 / aw > 1.15)) gmu = mat_mul(mat_make(w0 / aw, 0, 0, 1, 0, 0), gmu);
    }
    gm = mat_mul(gmu, g->ctm);
    if ((mode == 0 || mode == 4) && g->fcs->kind != CS_PATTERN) {
      if (mode == 0 && glyph_cached(r, f->prog, gid, &gm, g->frgb, g->fa)) return;
    }
    path_init(&gp); path_init(&dev);
    if (!pf_glyph_outline(f->prog, gid, &gp)) { path_free(&gp); return; }
    path_append(&dev, &gp, &gm);
    if (mode == 0 || mode == 2 || mode == 4 || mode == 6) {
      if (g->fcs->kind == CS_PATTERN && g->fpat) fill_with_pattern(r, g->fpat, &dev, 0, 0);
      else fill_solid(r, &dev, 0, g->frgb, g->fa);
    }
    if (mode == 1 || mode == 2 || mode == 5 || mode == 6) {
      StrokeStyle st;
      Path out, up;
      path_init(&out); path_init(&up);
      path_append(&up, &gp, &gmu);
      stroke_style(r, &st);
      st.min_width = 0.5;
      stroke_path(&up, &st, &g->ctm, &out);
      fill_solid(r, &out, 0, g->srgb, g->sa);
      path_free(&out); path_free(&up);
    }
    if (mode >= 4) { path_append(&r->textclip, &dev, NULL); r->textclip_used = 1; }
    path_free(&gp); path_free(&dev);
  }
}
/* a string shown with the current font, moving the text matrix */
static void show_string(Render *r, const u8 *s, int len)
{
  GState *g = GS(r);
  PdfFont *f = g->font;
  int i = 0;
  if (!f) return;
  while (i < len) {
    u32 code;
    int nb = font_next(f, s + i, len - i, &code);
    double w0 = font_width(f, code, nb), tx;
    show_glyph(r, f, code, nb, w0);
    if (f->vertical) {
      double ty = -1000 / 1000.0 * g->size + g->cs;
      r->tm = mat_mul(mat_make(1, 0, 0, 1, 0, ty), r->tm);
    } else {
      tx = (w0 / 1000.0 * g->size + g->cs + (nb == 1 && code == 32 ? g->ws : 0)) * g->th / 100;
      r->tm = mat_mul(mat_make(1, 0, 0, 1, tx, 0), r->tm);
    }
    i += nb;
  }
}
static void run_type3_glyph(Render *r, PdfFont *f, u32 code, const Mat *tu)
{
  PObj *proc;
  const char *name = code < 256 ? f->names[code] : NULL;
  GState *g;
  size_t len;
  u8 *data;
  if (!name || !f->charprocs || r->depth >= MAXFORMDEPTH || r->ngs >= MAXSAVE - 2) return;
  proc = pdf_dget(r->d, f->charprocs, name);
  if (!proc || proc->type != PO_STREAM || !(data = pdf_stream_data(r->d, proc, &len))) return;
  /* q, the glyph's space, run, Q */
  r->gs[r->ngs + 1] = r->gs[r->ngs];
  r->ngs++;
  g = GS(r);
  cs_retain(g->fcs); cs_retain(g->scs); clip_retain(g->clip);
  g->ctm = mat_mul(mat_mul(f->matrix, *tu), g->ctm);
  {
    Mat stm = r->tm, stlm = r->tlm;
    Path sp = r->path;
    path_init(&r->path);
    r->depth++;
    run_content(r, data, len, f->resources ? f->resources : NULL);
    r->depth--;
    path_free(&r->path);
    r->path = sp;
    r->tm = stm; r->tlm = stlm;
  }
  free(data);
  r->t3_uncoloured = 0;
  cs_release(g->fcs); cs_release(g->scs); clip_release(g->clip);
  r->ngs--;
}

/* ------------------------------------------------------------------ the graphics state */
static void gs_push(Render *r)
{
  GState *g;
  if (r->ngs >= MAXSAVE - 1) return;
  r->gs[r->ngs + 1] = r->gs[r->ngs];
  r->ngs++;
  g = GS(r);
  cs_retain(g->fcs); cs_retain(g->scs); clip_retain(g->clip);
}
static void gs_pop(Render *r, int floor_)
{
  GState *g;
  if (r->ngs <= floor_) return;
  g = GS(r);
  cs_release(g->fcs); cs_release(g->scs); clip_release(g->clip);
  r->ngs--;
}
static void ext_gstate(Render *r, PObj *res, const char *name)
{
  PObj *egs = res ? pdf_dget(r->d, res, "ExtGState") : NULL, *e = egs ? pdf_dget(r->d, egs, name) : NULL, *v;
  GState *g = GS(r);
  if (!e || e->type != PO_DICT) return;
  if ((v = pdf_dget(r->d, e, "LW")) && pobj_isnum(v)) g->lw = pobj_num(v);
  if ((v = pdf_dget(r->d, e, "LC")) && pobj_isnum(v)) g->cap = dbl_int(pobj_num(v));
  if ((v = pdf_dget(r->d, e, "LJ")) && pobj_isnum(v)) g->join = dbl_int(pobj_num(v));
  if ((v = pdf_dget(r->d, e, "ML")) && pobj_isnum(v)) g->miter = pobj_num(v);
  if ((v = pdf_dget(r->d, e, "CA")) && pobj_isnum(v)) g->sa = clamp01(pobj_num(v));
  if ((v = pdf_dget(r->d, e, "ca")) && pobj_isnum(v)) g->fa = clamp01(pobj_num(v));
  if ((v = pdf_dget(r->d, e, "D")) && v->type == PO_ARRAY && v->u.arr.n >= 2) {
    PObj *a = pdf_resolve(r->d, v->u.arr.v[0]);
    int i;
    g->ndash = 0;
    if (a && a->type == PO_ARRAY) for (i = 0; i < a->u.arr.n && i < MAXDASH; i++) g->dash[g->ndash++] = pobj_num(pdf_resolve(r->d, a->u.arr.v[i]));
    g->phase = pobj_num(pdf_resolve(r->d, v->u.arr.v[1]));
  }
  if ((v = pdf_dget(r->d, e, "Font")) && v->type == PO_ARRAY && v->u.arr.n >= 2) {
    g->font = cached_font(r->c, r->d, v->u.arr.v[0]);
    g->size = pobj_num(pdf_resolve(r->d, v->u.arr.v[1]));
  }
  if ((v = pdf_dget(r->d, e, "BM"))) {
    if (v->type == PO_ARRAY && v->u.arr.n) v = pdf_resolve(r->d, v->u.arr.v[0]);
    g->blend = pobj_is_name(v, "Multiply") ? 1 : pobj_is_name(v, "Screen") ? 2 : pobj_is_name(v, "Darken") ? 3 : pobj_is_name(v, "Lighten") ? 4 : 0;
  }
}

/* ------------------------------------------------------------------ optional content */
static int ocg_hidden(Render *r, PObj *oc)
{
  PObj *ocgs;
  int i;
  oc = pdf_resolve(r->d, oc);
  if (!oc || !r->ocoff) return 0;
  ocgs = pdf_dget(r->d, oc, "OCGs");
  if (ocgs) {
    /* a membership dictionary: hidden when its groups are (AnyOn, the default: all of them off) */
    const char *pol = pdf_dname(r->d, oc, "P");
    int any = !pol || !strcmp(pol, "AnyOn") || !strcmp(pol, "AllOn"), off = 0, n = 0;
    if (ocgs->type != PO_ARRAY) { ocgs = NULL; }
    if (!ocgs) return 0;
    for (i = 0; i < ocgs->u.arr.n; i++) { n++; if (ocg_hidden(r, ocgs->u.arr.v[i])) off++; }
    return any ? n > 0 && off == n : off > 0;
  }
  for (i = 0; i < r->ocoff->u.arr.n; i++) if (pdf_resolve(r->d, r->ocoff->u.arr.v[i]) == oc) return 1;
  return 0;
}

/* ------------------------------------------------------------------ the interpreter */
static double num(PObj **ops, int n, int i) { return i < n ? pobj_num(ops[i]) : 0; }
static void set_color(Render *r, int stroke, PObj **ops, int n)
{
  GState *g = GS(r);
  CSpace *cs = stroke ? g->scs : g->fcs;
  double *v = stroke ? g->sc : g->fc;
  int i, k = 0;
  if (r->t3_uncoloured) return;
  for (i = 0; i < n && k < 32; i++) if (pobj_isnum(ops[i])) v[k++] = pobj_num(ops[i]);
  if (cs->kind == CS_PATTERN) {
    /* the pattern's name is looked up in the resources by the caller, after the operator (the
       operand itself is freed then, so it is never kept: a page with sc and no resources kept a
       pointer to it) */
    if (stroke) g->spat = NULL; else g->fpat = NULL;
    if (cs->base) set_rgb(stroke ? g->srgb : g->frgb, cs->base, v);
    return;
  }
  set_rgb(stroke ? g->srgb : g->frgb, cs, v);
}
static void set_space(Render *r, int stroke, CSpace *cs)
{
  GState *g = GS(r);
  if (r->t3_uncoloured) { cs_release(cs); return; }
  if (!cs) cs = &dev_gray;
  if (stroke) { cs_release(g->scs); g->scs = cs; cs_default(cs, g->sc); g->spat = NULL; if (cs->kind != CS_PATTERN) set_rgb(g->srgb, cs, g->sc); }
  else { cs_release(g->fcs); g->fcs = cs; cs_default(cs, g->fc); g->fpat = NULL; if (cs->kind != CS_PATTERN) set_rgb(g->frgb, cs, g->fc); }
}
/* an inline image: BI <key value ...> ID <data> EI; l is just past BI */
static void inline_image(Render *r, Lex *l, PObj *res)
{
  PObj *dict = pobj_new(PO_DICT);
  const u8 *start, *p;
  size_t len;
  if (!dict) return;
  for (;;) {
    char *kw = NULL;
    PObj *k = lex_object(l, &kw), *v;
    if (kw) { int id = !strcmp(kw, "ID"); free(kw); if (id) break; pobj_free(dict); return; }
    if (!k) { pobj_free(dict); return; }
    v = lex_object(l, &kw);
    free(kw);
    if (k->type == PO_NAME) pobj_dict_set(dict, k->u.str.s, v); else pobj_free(v);
    pobj_free(k);
  }
  /* one whitespace byte after ID, then the data up to EI (between whitespace) */
  if (l->p < l->end && (*l->p == ' ' || *l->p == 10 || *l->p == 13 || *l->p == 9)) {
    if (*l->p == 13 && l->p + 1 < l->end && l->p[1] == 10) l->p++;
    l->p++;
  }
  start = l->p;
  {
    /* an unfiltered image's length is known from its size */
    PObj *f = pobj_dict_get(dict, "F"), *ff = f ? f : pobj_dict_get(dict, "Filter");
    long w = dbl_long(pobj_num(pobj_dict_get(dict, "W") ? pobj_dict_get(dict, "W") : pobj_dict_get(dict, "Width")));
    long h = dbl_long(pobj_num(pobj_dict_get(dict, "H") ? pobj_dict_get(dict, "H") : pobj_dict_get(dict, "Height")));
    long bpc = dbl_long(pobj_num(pobj_dict_get(dict, "BPC") ? pobj_dict_get(dict, "BPC") : pobj_dict_get(dict, "BitsPerComponent")));
    int im = pobj_dict_get(dict, "IM") && pobj_dict_get(dict, "IM")->type == PO_BOOL && pobj_dict_get(dict, "IM")->u.b;
    PObj *cs = pobj_dict_get(dict, "CS") ? pobj_dict_get(dict, "CS") : pobj_dict_get(dict, "ColorSpace");
    int ncomp = 1;
    if (cs && cs->type == PO_NAME) ncomp = (!strcmp(cs->u.str.s, "RGB") || !strcmp(cs->u.str.s, "DeviceRGB")) ? 3 : (!strcmp(cs->u.str.s, "CMYK") || !strcmp(cs->u.str.s, "DeviceCMYK")) ? 4 : 1;
    if (im) { ncomp = 1; bpc = 1; }
    if (!ff && w > 0 && h > 0 && bpc > 0 && w < 100000 && h < 100000) {
      size_t need = (size_t)((w * ncomp * bpc + 7) / 8) * (size_t)h;
      if (need <= (size_t)(l->end - start)) {
        p = start + need;
        /* (check EI follows; if not, fall back to searching) */
        while (p < l->end && (*p == ' ' || *p == 10 || *p == 13 || *p == 9)) p++;
        if (p + 1 < l->end && p[0] == 'E' && p[1] == 'I') { len = need; l->p = p + 2; draw_image(r, NULL, res, dict, start, len); pobj_free(dict); return; }
      }
    }
  }
  for (p = start; p + 1 < l->end; p++)
    if (p[0] == 'E' && p[1] == 'I' && (p == start || p[-1] == ' ' || p[-1] == 10 || p[-1] == 13 || p[-1] == 9) &&
        (p + 2 >= l->end || p[2] == ' ' || p[2] == 10 || p[2] == 13 || p[2] == 9)) break;
  len = (size_t)(p - start);
  if (len && (start[len - 1] == 10 || start[len - 1] == ' ' || start[len - 1] == 13)) len--;
  l->p = p + 2 <= l->end ? p + 2 : l->end;
  draw_image(r, NULL, res, dict, start, len);
  pobj_free(dict);
}
static void do_xobject(Render *r, PObj *res, const char *name)
{
  PObj *xo = res ? pdf_dget(r->d, res, "XObject") : NULL, *x = xo ? pdf_dget(r->d, xo, name) : NULL, *oc;
  const char *st;
  if (!x || x->type != PO_STREAM) return;
  if ((oc = pdf_dget(r->d, x, "OC")) && ocg_hidden(r, oc)) return;
  st = pdf_dname(r->d, x, "Subtype");
  if (st && !strcmp(st, "Image")) { if (!r->o->text_only && !r->hidden) draw_image(r, x, res, NULL, NULL, 0); }
  else if (st && !strcmp(st, "Form")) run_form(r, x, res, NULL);
}
void run_form(Render *r, PObj *form, PObj *res, const Mat *extra)
{
  PObj *m = pdf_dget(r->d, form, "Matrix"), *bb = pdf_dget(r->d, form, "BBox"), *fres = pdf_dget(r->d, form, "Resources");
  size_t len;
  u8 *data;
  int i, base;
  if (r->depth >= MAXFORMDEPTH || r->stopped) return;
  if (!(data = pdf_stream_data(r->d, form, &len))) return;
  gs_push(r);
  base = r->ngs;
  if (extra) GS(r)->ctm = mat_mul(*extra, GS(r)->ctm);
  if (m && m->type == PO_ARRAY && m->u.arr.n >= 6) {
    double v[6];
    for (i = 0; i < 6; i++) v[i] = pobj_num(pdf_resolve(r->d, m->u.arr.v[i]));
    GS(r)->ctm = mat_mul(mat_make(v[0], v[1], v[2], v[3], v[4], v[5]), GS(r)->ctm);
  }
  if (bb && bb->type == PO_ARRAY && bb->u.arr.n >= 4) {
    double v[4];
    Path p, dev;
    for (i = 0; i < 4; i++) v[i] = pobj_num(pdf_resolve(r->d, bb->u.arr.v[i]));
    path_init(&p); path_init(&dev);
    path_move(&p, v[0], v[1]); path_line(&p, v[2], v[1]); path_line(&p, v[2], v[3]); path_line(&p, v[0], v[3]); path_close(&p);
    path_append(&dev, &p, &GS(r)->ctm);
    clip_to(r, &dev, 0);
    path_free(&p); path_free(&dev);
  }
  {
    Path sp = r->path;
    Mat stm = r->tm, stlm = r->tlm;
    path_init(&r->path);
    r->depth++;
    run_content(r, data, len, fres ? fres : res);
    r->depth--;
    path_free(&r->path);
    r->path = sp;
    r->tm = stm; r->tlm = stlm;
  }
  while (r->ngs >= base) gs_pop(r, 0);
  free(data);
}
void run_content(Render *r, const u8 *data, size_t len, PObj *res)
{
  Lex l;
  PObj *ops[MAXOPS];
  int n = 0, i, floor_ = r->ngs;
  u8 hidestack[64]; int nhide = 0;
  lex_init(&l, data, len, r->d);
  for (;;) {
    char *kw = NULL;
    PObj *o;
    GState *g;
    if (r->stopped) break;
    o = lex_object(&l, &kw);
    if (o) { if (n < MAXOPS) ops[n++] = o; else pobj_free(o); continue; }
    if (!kw) break;
    g = GS(r);
    if ((++r->ops & 255) == 0 && r->o->abort && r->o->abort(r->o->ud)) { r->stopped = 1; free(kw); break; }
    /* the operators, roughly most common first */
    switch (kw[0]) {
    case 'q': if (!kw[1]) gs_push(r); break;
    case 'Q': if (!kw[1]) gs_pop(r, floor_); break;
    case 'c':
      if (!strcmp(kw, "cm") && n >= 6) {
        Mat m = mat_make(num(ops, n, 0), num(ops, n, 1), num(ops, n, 2), num(ops, n, 3), num(ops, n, 4), num(ops, n, 5));
        g->ctm = mat_mul(m, g->ctm);
      } else if (!kw[1] && n >= 6) path_curve(&r->path, num(ops, n, 0), num(ops, n, 1), num(ops, n, 2), num(ops, n, 3), num(ops, n, 4), num(ops, n, 5));
      else if (!strcmp(kw, "cs") && n >= 1) set_space(r, 0, cs_load(r->d, ops[0], res, 0));
      break;
    case 'm': if (!kw[1] && n >= 2) path_move(&r->path, num(ops, n, 0), num(ops, n, 1)); break;
    case 'l': if (!kw[1] && n >= 2) path_line(&r->path, num(ops, n, 0), num(ops, n, 1)); break;
    case 'v': if (!kw[1] && n >= 4) { double x, y; if (path_current(&r->path, &x, &y)) path_curve(&r->path, x, y, num(ops, n, 0), num(ops, n, 1), num(ops, n, 2), num(ops, n, 3)); } break;
    case 'y': if (!kw[1] && n >= 4) path_curve(&r->path, num(ops, n, 0), num(ops, n, 1), num(ops, n, 2), num(ops, n, 3), num(ops, n, 2), num(ops, n, 3)); break;
    case 'h': if (!kw[1]) path_close(&r->path); break;
    case 'r':
      if (!strcmp(kw, "re") && n >= 4) {
        double x = num(ops, n, 0), y = num(ops, n, 1), w = num(ops, n, 2), h = num(ops, n, 3);
        path_move(&r->path, x, y); path_line(&r->path, x + w, y); path_line(&r->path, x + w, y + h); path_line(&r->path, x, y + h); path_close(&r->path);
      } else if (!strcmp(kw, "rg") && n >= 3) { set_space(r, 0, &dev_rgb); set_color(r, 0, ops, n); }
      else if (!strcmp(kw, "ri")) { }
      break;
    case 'R': if (!strcmp(kw, "RG") && n >= 3) { set_space(r, 1, &dev_rgb); set_color(r, 1, ops, n); } break;
    case 'S':
      if (!kw[1]) paint_path(r, 0, 1, 0);
      else if (!strcmp(kw, "SC") || !strcmp(kw, "SCN")) set_color(r, 1, ops, n);
      break;
    case 's':
      if (!kw[1]) { path_close(&r->path); paint_path(r, 0, 1, 0); }
      else if (!strcmp(kw, "sc") || !strcmp(kw, "scn")) set_color(r, 0, ops, n);
      else if (!strcmp(kw, "sh") && n >= 1 && ops[0]->type == PO_NAME && !r->hidden && !r->o->text_only) {
        PObj *shd = res ? pdf_dget(r->d, res, "Shading") : NULL, *sh = shd ? pdf_dget(r->d, shd, ops[0]->u.str.s) : NULL;
        if (sh) draw_shading(r, sh, NULL, 0, 0);
      }
      break;
    case 'f': if (!kw[1] || !strcmp(kw, "f*")) paint_path(r, 1, 0, kw[1] == '*'); break;
    case 'F': if (!kw[1]) paint_path(r, 1, 0, 0); break;
    case 'B':
      if (!kw[1] || !strcmp(kw, "B*")) paint_path(r, 1, 1, kw[1] == '*');
      else if (!strcmp(kw, "BT")) { r->tm = r->tlm = mat_identity; path_reset(&r->textclip); r->textclip_used = 0; }
      else if (!strcmp(kw, "BI")) inline_image(r, &l, res);
      else if (!strcmp(kw, "BDC") || !strcmp(kw, "BMC")) {
        int hide = 0;
        if (kw[1] == 'D' && n >= 2 && pobj_is_name(ops[0], "OC")) {
          PObj *prop = ops[1];
          if (prop->type == PO_NAME && res) { PObj *props = pdf_dget(r->d, res, "Properties"); prop = props ? pdf_dget(r->d, props, prop->u.str.s) : NULL; }
          hide = prop ? ocg_hidden(r, prop) : 0;
        }
        if (nhide < 64) hidestack[nhide++] = (u8)hide;
        if (hide) r->hidden++;
      } else if (!strcmp(kw, "BX")) { }
      break;
    case 'b': if (!kw[1] || !strcmp(kw, "b*")) { path_close(&r->path); paint_path(r, 1, 1, kw[1] == '*'); } break;
    case 'n': if (!kw[1]) paint_path(r, 0, 0, 0); break;
    case 'W': if (!kw[1]) r->clip_pending = 1; else if (!strcmp(kw, "W*")) r->clip_pending = 2; break;
    case 'w': if (!kw[1] && n >= 1) g->lw = num(ops, n, 0); break;
    case 'J': if (!kw[1] && n >= 1) g->cap = dbl_int(num(ops, n, 0)); break;
    case 'j': if (!kw[1] && n >= 1) g->join = dbl_int(num(ops, n, 0)); break;
    case 'M': if (!kw[1] && n >= 1) g->miter = num(ops, n, 0); else if (!strcmp(kw, "MP")) { } break;
    case 'd':
      if (!kw[1] && n >= 2 && ops[0]->type == PO_ARRAY) {
        g->ndash = 0;
        for (i = 0; i < ops[0]->u.arr.n && i < MAXDASH; i++) g->dash[g->ndash++] = pobj_num(ops[0]->u.arr.v[i]);
        /* (all zero: no dashes) */
        for (i = 0; i < g->ndash && g->dash[i] <= 0; i++) ;
        if (i == g->ndash) g->ndash = 0;
        g->phase = num(ops, n, 1);
      } else if (!strcmp(kw, "d1")) r->t3_uncoloured = 1;
      break;
    case 'g':
      if (!kw[1] && n >= 1) { set_space(r, 0, &dev_gray); set_color(r, 0, ops, n); }
      else if (!strcmp(kw, "gs") && n >= 1 && ops[0]->type == PO_NAME) ext_gstate(r, res, ops[0]->u.str.s);
      break;
    case 'G': if (!kw[1] && n >= 1) { set_space(r, 1, &dev_gray); set_color(r, 1, ops, n); } break;
    case 'k': if (!kw[1] && n >= 4) { set_space(r, 0, &dev_cmyk); set_color(r, 0, ops, n); } break;
    case 'K': if (!kw[1] && n >= 4) { set_space(r, 1, &dev_cmyk); set_color(r, 1, ops, n); } break;
    case 'C': if (!strcmp(kw, "CS") && n >= 1) set_space(r, 1, cs_load(r->d, ops[0], res, 0)); break;
    case 'D': if (!strcmp(kw, "Do") && n >= 1 && ops[0]->type == PO_NAME) do_xobject(r, res, ops[0]->u.str.s); else if (!strcmp(kw, "DP")) { } break;
    case 'E':
      if (!strcmp(kw, "ET")) {
        if (r->textclip_used) {
          if (r->textclip.nops) clip_to(r, &r->textclip, 0);
          else { Clip *nc = clip_new(0, 0, 0, 0); if (nc) { clip_release(g->clip); g->clip = nc; } }
          path_reset(&r->textclip); r->textclip_used = 0;
        }
      } else if (!strcmp(kw, "EMC")) { if (nhide) { if (hidestack[--nhide]) r->hidden--; } }
      break;
    case 'T':
      switch (kw[1]) {
      case 'f':
        if (n >= 2 && ops[0]->type == PO_NAME) {
          PObj *fd = res ? pdf_dget(r->d, res, "Font") : NULL;
          g->font = fd ? cached_font(r->c, r->d, pdf_dget(r->d, fd, ops[0]->u.str.s)) : NULL;
          g->size = num(ops, n, 1);
        }
        break;
      case 'j': if (n >= 1 && ops[0]->type == PO_STRING) show_string(r, (const u8 *)ops[0]->u.str.s, ops[0]->u.str.len); break;
      case 'J':
        if (n >= 1 && ops[0]->type == PO_ARRAY) {
          for (i = 0; i < ops[0]->u.arr.n; i++) {
            PObj *e = ops[0]->u.arr.v[i];
            if (e->type == PO_STRING) show_string(r, (const u8 *)e->u.str.s, e->u.str.len);
            else if (pobj_isnum(e)) {
              double v = pobj_num(e) / 1000 * g->size;
              if (g->font && g->font->vertical) r->tm = mat_mul(mat_make(1, 0, 0, 1, 0, -v), r->tm);
              else r->tm = mat_mul(mat_make(1, 0, 0, 1, -v * g->th / 100, 0), r->tm);
            }
          }
        }
        break;
      case 'd': if (n >= 2) { r->tlm = mat_mul(mat_make(1, 0, 0, 1, num(ops, n, 0), num(ops, n, 1)), r->tlm); r->tm = r->tlm; } break;
      case 'D': if (n >= 2) { g->tl = -num(ops, n, 1); r->tlm = mat_mul(mat_make(1, 0, 0, 1, num(ops, n, 0), num(ops, n, 1)), r->tlm); r->tm = r->tlm; } break;
      case 'm': if (n >= 6) { r->tlm = r->tm = mat_make(num(ops, n, 0), num(ops, n, 1), num(ops, n, 2), num(ops, n, 3), num(ops, n, 4), num(ops, n, 5)); } break;
      case '*': r->tlm = mat_mul(mat_make(1, 0, 0, 1, 0, -g->tl), r->tlm); r->tm = r->tlm; break;
      case 'c': if (n >= 1) g->cs = num(ops, n, 0); break;
      case 'w': if (n >= 1) g->ws = num(ops, n, 0); break;
      case 'z': if (n >= 1) g->th = num(ops, n, 0); break;
      case 'L': if (n >= 1) g->tl = num(ops, n, 0); break;
      case 'r': if (n >= 1) g->tmode = dbl_int(num(ops, n, 0)); break;
      case 's': if (n >= 1) g->rise = num(ops, n, 0); break;
      }
      break;
    case '\'':
      r->tlm = mat_mul(mat_make(1, 0, 0, 1, 0, -g->tl), r->tlm); r->tm = r->tlm;
      if (n >= 1 && ops[n - 1]->type == PO_STRING) show_string(r, (const u8 *)ops[n - 1]->u.str.s, ops[n - 1]->u.str.len);
      break;
    case '"':
      if (n >= 3) { g->ws = num(ops, n, 0); g->cs = num(ops, n, 1); }
      r->tlm = mat_mul(mat_make(1, 0, 0, 1, 0, -g->tl), r->tlm); r->tm = r->tlm;
      if (n >= 1 && ops[n - 1]->type == PO_STRING) show_string(r, (const u8 *)ops[n - 1]->u.str.s, ops[n - 1]->u.str.len);
      break;
    case 'i': break;
    }
    /* a colour operator naming a pattern: looked up here, where the resources are known */
    if ((!strcmp(kw, "scn") || !strcmp(kw, "SCN") || !strcmp(kw, "sc") || !strcmp(kw, "SC")) && n >= 1 && ops[n - 1]->type == PO_NAME && res) {
      PObj *pd = pdf_dget(r->d, res, "Pattern"), *pat = pd ? pdf_dget(r->d, pd, ops[n - 1]->u.str.s) : NULL;
      if (kw[0] == 's') GS(r)->fpat = pat; else GS(r)->spat = pat;
    }
    free(kw);
    for (i = 0; i < n; i++) pobj_free(ops[i]);
    n = 0;
  }
  for (i = 0; i < n; i++) pobj_free(ops[i]);
  while (nhide) if (hidestack[--nhide]) r->hidden--;
  while (r->ngs > floor_) gs_pop(r, floor_);
}

/* ------------------------------------------------------------------ a separate drawing */
Render *render_sub(Render *parent, int W, int H, Mat base, const RenderOpts *o)
{
  Render *s = (Render *)calloc(1, sizeof *s);
  GState *g;
  if (!s) return NULL;
  s->d = parent->d; s->c = parent->c; s->o = o;
  s->W = W; s->H = H; s->base = base; s->page_inv = mat_identity;
  s->depth = parent->depth + 1;
  s->ocoff = parent->ocoff;
  if (!(s->rgb = (u8 *)calloc((size_t)W * H, 3)) || !(s->alpha = (u8 *)calloc((size_t)W * H, 1))) { free(s->rgb); free(s); return NULL; }
  g = GS(s);
  g->ctm = base;
  g->fcs = g->scs = &dev_gray;
  g->lw = 1; g->miter = 10; g->fa = g->sa = 1; g->th = 100;
  if (!(g->clip = clip_new(0, 0, W, H))) { free(s->rgb); free(s->alpha); free(s); return NULL; }
  path_init(&s->path); path_init(&s->textclip);
  return s;
}
void render_sub_free(Render *s)
{
  if (!s) return;
  while (s->ngs > 0) gs_pop(s, 0);
  cs_release(GS(s)->fcs); cs_release(GS(s)->scs);
  clip_release(GS(s)->clip);
  path_free(&s->path); path_free(&s->textclip);
  free(s->rgb); free(s->alpha);
  free(s);
}

/* ------------------------------------------------------------------ annotations */
static void draw_annots(Render *r, PObj *page)
{
  PObj *annots = pdf_dget(r->d, page, "Annots");
  int i;
  if (!annots || annots->type != PO_ARRAY) return;
  for (i = r->o->annot_first > 0 ? r->o->annot_first : 0; i < annots->u.arr.n && !r->stopped; i++) {
    PObj *a = pdf_resolve(r->d, annots->u.arr.v[i]), *ap, *nrm, *rect, *bb, *mm, *oc;
    double rc[4], b[4], m[6] = { 1, 0, 0, 1, 0, 0 }, x0, y0, x1, y1, xs[4], ys[4];
    long flags;
    int k;
    const char *st;
    if (!a || a->type != PO_DICT) continue;
    flags = pdf_dint(r->d, a, "F", 0);
    if (flags & 2) continue;                          /* hidden */
    st = pdf_dname(r->d, a, "Subtype");
    if (st && !strcmp(st, "Popup")) continue;
    if ((oc = pdf_dget(r->d, a, "OC")) && ocg_hidden(r, oc)) continue;
    ap = pdf_dget(r->d, a, "AP");
    nrm = ap ? pdf_dget(r->d, ap, "N") : NULL;
    if (nrm && nrm->type == PO_DICT) {
      const char *as = pdf_dname(r->d, a, "AS");
      nrm = as ? pdf_dget(r->d, nrm, as) : NULL;
    }
    if (!nrm || nrm->type != PO_STREAM) continue;
    rect = pdf_dget(r->d, a, "Rect");
    if (!rect || rect->type != PO_ARRAY || rect->u.arr.n < 4) continue;
    for (k = 0; k < 4; k++) rc[k] = pobj_num(pdf_resolve(r->d, rect->u.arr.v[k]));
    if (rc[0] > rc[2]) { double t = rc[0]; rc[0] = rc[2]; rc[2] = t; }
    if (rc[1] > rc[3]) { double t = rc[1]; rc[1] = rc[3]; rc[3] = t; }
    bb = pdf_dget(r->d, nrm, "BBox");
    if (!bb || bb->type != PO_ARRAY || bb->u.arr.n < 4) continue;
    for (k = 0; k < 4; k++) b[k] = pobj_num(pdf_resolve(r->d, bb->u.arr.v[k]));
    mm = pdf_dget(r->d, nrm, "Matrix");
    if (mm && mm->type == PO_ARRAY && mm->u.arr.n >= 6) for (k = 0; k < 6; k++) m[k] = pobj_num(pdf_resolve(r->d, mm->u.arr.v[k]));
    /* the box transformed by the matrix, then mapped onto Rect (PDF 12.5.5) */
    {
      Mat fm = mat_make(m[0], m[1], m[2], m[3], m[4], m[5]), A;
      mat_apply(&fm, b[0], b[1], &xs[0], &ys[0]); mat_apply(&fm, b[2], b[1], &xs[1], &ys[1]);
      mat_apply(&fm, b[2], b[3], &xs[2], &ys[2]); mat_apply(&fm, b[0], b[3], &xs[3], &ys[3]);
      x0 = x1 = xs[0]; y0 = y1 = ys[0];
      for (k = 1; k < 4; k++) { if (xs[k] < x0) x0 = xs[k]; if (xs[k] > x1) x1 = xs[k]; if (ys[k] < y0) y0 = ys[k]; if (ys[k] > y1) y1 = ys[k]; }
      if (x1 - x0 < 1e-6 || y1 - y0 < 1e-6) continue;
      A = mat_make((rc[2] - rc[0]) / (x1 - x0), 0, 0, (rc[3] - rc[1]) / (y1 - y0), rc[0] - x0 * (rc[2] - rc[0]) / (x1 - x0), rc[1] - y0 * (rc[3] - rc[1]) / (y1 - y0));
      /* (run_form applies the form's own Matrix, so A goes in as the extra) */
      run_form(r, nrm, NULL, &A);
    }
  }
}

/* ------------------------------------------------------------------ the page */
Mat pdf_page_matrix(PDoc *d, int page, double s, int rotate)
{
  double b[4];
  int rot;
  pdf_page_box(d, page, b, &rot);
  rot = (rot + rotate) % 360;
  if (rot < 0) rot += 360;
  switch (rot) {
  case 90: return mat_make(0, s, s, 0, -b[0] * s, -b[1] * s);
  case 180: return mat_make(-s, 0, 0, s, b[2] * s, -b[1] * s);
  case 270: return mat_make(0, -s, -s, 0, b[3] * s, b[2] * s);
  default: return mat_make(s, 0, 0, -s, -b[0] * s, b[3] * s);
  }
}
void pdf_page_pixels(PDoc *d, int page, double s, int rotate, int *w, int *h)
{
  double b[4];
  int rot, pw, ph;
  pdf_page_box(d, page, b, &rot);
  rot = ((rot + rotate) % 360 + 360) % 360;
  pw = dbl_int(ceil((b[2] - b[0]) * s - 0.01)); ph = dbl_int(ceil((b[3] - b[1]) * s - 0.01));
  if (pw < 1) pw = 1;
  if (ph < 1) ph = 1;
  if (rot == 90 || rot == 270) { *w = ph; *h = pw; } else { *w = pw; *h = ph; }
}
int pdf_render(PDoc *d, RCache *c, int page, const RenderOpts *o, Pixmap *out)
{
  Render *r;
  PObj *pg, *contents, *res, *root, *ocp;
  int fw, fh, ok, i;
  Mat inv;
  memset(out, 0, sizeof *out);
  if (page < 0 || page >= d->npages) return 0;
  if (!(r = (Render *)calloc(1, sizeof *r))) return 0;
  pg = d->pages[page];
  r->d = d; r->c = c; r->o = o;
  pdf_page_pixels(d, page, o->scale, o->rotate, &fw, &fh);
  if (o->w > 0) { r->W = o->w; r->H = o->h; } else { r->W = fw; r->H = fh; }
  r->base = mat_mul(pdf_page_matrix(d, page, o->scale, o->rotate), mat_make(1, 0, 0, 1, o->w > 0 ? -o->x : 0, o->w > 0 ? -o->y : 0));
  if (!mat_invert(r->base, &inv)) inv = mat_identity;
  r->page_inv = inv;
  if (!o->text_only) {
    long npx = (long)r->W * r->H;
    if (r->W <= 0 || r->H <= 0 || npx > 40000000L || !(r->rgb = (u8 *)malloc((size_t)npx * 3))) { free(r); return 0; }
    if (o->onto && o->w <= 0) memcpy(r->rgb, o->onto, (size_t)npx * 3);
    else for (i = 0; i < npx; i++) { r->rgb[i * 3] = (u8)(o->paper >> 16); r->rgb[i * 3 + 1] = (u8)(o->paper >> 8); r->rgb[i * 3 + 2] = (u8)o->paper; }
  }
  /* the initial graphics state */
  {
    GState *g = GS(r);
    memset(g, 0, sizeof *g);
    g->ctm = r->base;
    g->fcs = g->scs = &dev_gray;
    g->lw = 1; g->miter = 10; g->fa = g->sa = 1; g->th = 100;
    g->clip = clip_new(0, 0, r->W, r->H);
    if (!g->clip) { free(r->rgb); free(r); return 0; }
  }
  path_init(&r->path); path_init(&r->textclip);
  root = pdf_dget(d, d->trailer, "Root");
  ocp = root ? pdf_dget(d, root, "OCProperties") : NULL;
  if (ocp) { PObj *dd = pdf_dget(d, ocp, "D"); PObj *off = dd ? pdf_dget(d, dd, "OFF") : NULL; if (off && off->type == PO_ARRAY) r->ocoff = off; }
  res = pdf_page_attr(d, pg, "Resources");
  contents = o->annots_only ? NULL : pdf_dget(d, pg, "Contents");
  if (contents && contents->type == PO_STREAM) {
    size_t len;
    u8 *data = pdf_stream_data(d, contents, &len);
    if (data) { run_content(r, data, len, res); free(data); }
  } else if (contents && contents->type == PO_ARRAY) {
    /* the pieces joined (an operator may be split across them) */
    Buf b;
    buf_init(&b);
    for (i = 0; i < contents->u.arr.n; i++) {
      PObj *s = pdf_resolve(d, contents->u.arr.v[i]);
      size_t len;
      u8 *data = s && s->type == PO_STREAM ? pdf_stream_data(d, s, &len) : NULL;
      if (data) { buf_add(&b, data, len); buf_addc(&b, '\n'); free(data); }
    }
    if (!b.oom && b.data) run_content(r, (const u8 *)b.data, b.len, res);
    buf_free(&b);
  }
  if (o->annots && !r->stopped) {
    /* (annotations start from the page's own state, whatever the content left behind) */
    GState *g;
    Clip *full = clip_new(0, 0, r->W, r->H);
    while (r->ngs > 0) gs_pop(r, 0);
    g = GS(r);
    if (full) {
      cs_release(g->fcs); cs_release(g->scs); clip_release(g->clip);
      memset(g, 0, sizeof *g);
      g->ctm = r->base;
      g->fcs = g->scs = &dev_gray;
      g->lw = 1; g->miter = 10; g->fa = g->sa = 1; g->th = 100;
      g->clip = full;
      draw_annots(r, pg);
    }
  }
  ok = !r->stopped;
  while (r->ngs > 0) gs_pop(r, 0);
  cs_release(GS(r)->fcs); cs_release(GS(r)->scs);
  clip_release(GS(r)->clip);
  path_free(&r->path); path_free(&r->textclip);
  out->w = r->W; out->h = r->H; out->rgb = r->rgb;
  free(r);
  return ok;
}
