/*
 * pimage.c - PDF images (see render_int.h): samples of any depth in any colour space, JPEG
 * (through Quill's decoder), CCITT fax, stencil masks, colour-key masks, explicit masks and soft
 * masks (alpha), inline images. An image much bigger than it will appear is shrunk while it is
 * unpacked (averaging), so a scan drawn as a thumbnail never needs its full size in memory.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_int.h"
#include "image.h"
#include "filters.h"

typedef struct { int w, h; u8 *rgba; } Img;           /* (w * h * 4) */

/* inline images' abbreviations */
static PObj *ikey(PDoc *d, PObj *dict, const char *full, const char *abbr)
{
  PObj *v = pdf_dget(d, dict, full);
  if (!v && abbr) v = pdf_dget(d, dict, abbr);
  return v;
}
static PObj *expand_name(PObj *o)
{
  static const char *const ab[][2] = { { "G", "DeviceGray" }, { "RGB", "DeviceRGB" }, { "CMYK", "DeviceCMYK" }, { "I", "Indexed" },
    { "AHx", "ASCIIHexDecode" }, { "A85", "ASCII85Decode" }, { "LZW", "LZWDecode" }, { "Fl", "FlateDecode" }, { "RL", "RunLengthDecode" },
    { "CCF", "CCITTFaxDecode" }, { "DCT", "DCTDecode" } };
  size_t i;
  if (!o) return o;
  if (o->type == PO_NAME)
    for (i = 0; i < sizeof ab / sizeof ab[0]; i++)
      if (!strcmp(o->u.str.s, ab[i][0])) { free(o->u.str.s); o->u.str.s = str_dup(ab[i][1]); o->u.str.len = (int)strlen(ab[i][1]); break; }
  if (o->type == PO_ARRAY) for (i = 0; i < (size_t)o->u.arr.n; i++) expand_name(o->u.arr.v[i]);
  return o;
}

/* the samples' bits read from a packed row */
static u32 sample(const u8 *row, long i, int bpc)
{
  switch (bpc) {
  case 8: return row[i];
  case 1: return (row[i >> 3] >> (7 - (i & 7))) & 1;
  case 2: return (row[i >> 2] >> (6 - 2 * (i & 3))) & 3;
  case 4: return (row[i >> 1] >> (4 - 4 * (i & 1))) & 15;
  case 16: return row[i * 2];                          /* (the high byte) */
  default: return 0;
  }
}

/* an image's samples unpacked to RGBA at tw x th (box-averaged when smaller than w x h).
   stencil: an image mask (alpha only, 255 = paint). Returns 0 if out of memory */
static int unpack(PDoc *d, const u8 *data, size_t len, int w, int h, int bpc, CSpace *cs, const double *decode, int ndecode,
                  int stencil, const int *colorkey, int nkey, int tw, int th, Img *out)
{
  int nc = stencil ? 1 : cs->n, x, y, k, maxv = bpc == 16 ? 255 : (1 << bpc) - 1;
  long rowbytes = ((long)w * nc * bpc + 7) / 8;
  u32 *acc = NULL;
  u8 lut[256][3];
  double dmin[32], dmax[32];
  int uselut = 0, *cnt = NULL;
  (void)d;
  if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16) return 0;
  if (nc < 1 || nc > 32) return 0;
  out->w = tw; out->h = th;
  if (!(out->rgba = (u8 *)calloc((size_t)tw * th, 4))) return 0;
  for (k = 0; k < nc; k++) {
    if (decode && ndecode >= 2 * (k + 1)) { dmin[k] = decode[k * 2]; dmax[k] = decode[k * 2 + 1]; }
    else if (!stencil && cs->kind == CS_INDEXED) { dmin[k] = 0; dmax[k] = maxv; }
    else if (!stencil && cs->kind == CS_LAB) { dmin[k] = k == 0 ? 0 : cs->range[(k - 1) * 2]; dmax[k] = k == 0 ? 100 : cs->range[(k - 1) * 2 + 1]; }
    else { dmin[k] = 0; dmax[k] = 1; }
  }
  /* one component: every sample value's colour worked out once */
  if (!stencil && nc == 1 && bpc <= 8) {
    int s;
    for (s = 0; s <= maxv; s++) {
      double v = dmin[0] + s * (dmax[0] - dmin[0]) / (maxv ? maxv : 1), rgb[3];
      cs_to_rgb(cs, &v, rgb);
      for (k = 0; k < 3; k++) lut[s][k] = (u8)(rgb[k] * 255 + 0.5);
    }
    uselut = 1;
  }
  /* sums for shrinking */
  if (tw != w || th != h) {
    if (!(acc = (u32 *)calloc((size_t)tw * 4, sizeof(u32))) || !(cnt = (int *)calloc((size_t)tw, sizeof(int)))) { free(acc); free(out->rgba); out->rgba = NULL; return 0; }
  }
  for (y = 0; y < h; y++) {
    const u8 *row = data + (long)y * rowbytes;
    int oy = (int)((long)y * th / h), oy_next = (int)((long)(y + 1) * th / h);
    int haverow = (long)(y + 1) * rowbytes <= (long)len;
    for (x = 0; x < w; x++) {
      u8 px[4];
      int ox;
      px[3] = 255;
      if (!haverow) { px[0] = px[1] = px[2] = 255; px[3] = stencil ? 0 : 255; }
      else if (stencil) {
        u32 s = sample(row, x, bpc);
        double v = dmin[0] + s * (dmax[0] - dmin[0]);
        px[0] = px[1] = px[2] = 0; px[3] = v < 0.5 ? 255 : 0;
      } else {
        u32 sv[32];
        for (k = 0; k < nc; k++) sv[k] = sample(row, (long)x * nc + k, bpc);
        if (nkey) {
          int in = 1;
          for (k = 0; k < nc && in; k++) if ((int)sv[k] < colorkey[k * 2] || (int)sv[k] > colorkey[k * 2 + 1]) in = 0;
          if (in) px[3] = 0;
        }
        if (uselut) { px[0] = lut[sv[0]][0]; px[1] = lut[sv[0]][1]; px[2] = lut[sv[0]][2]; }
        else if (cs->kind == CS_RGB && bpc >= 8 && dmin[0] == 0 && dmax[0] == 1) { px[0] = (u8)sv[0]; px[1] = (u8)sv[1]; px[2] = (u8)sv[2]; }
        else if (cs->kind == CS_CMYK && bpc >= 8 && dmin[0] == 0 && dmax[0] == 1) {
          int kk = 255 - (int)sv[3];
          px[0] = (u8)((255 - (int)sv[0]) * kk / 255); px[1] = (u8)((255 - (int)sv[1]) * kk / 255); px[2] = (u8)((255 - (int)sv[2]) * kk / 255);
        } else {
          double v[32], rgb[3];
          for (k = 0; k < nc; k++) v[k] = dmin[k] + sv[k] * (dmax[k] - dmin[k]) / (maxv ? maxv : 1);
          cs_to_rgb(cs, v, rgb);
          for (k = 0; k < 3; k++) px[k] = (u8)(rgb[k] * 255 + 0.5);
        }
      }
      if (!acc) { memcpy(out->rgba + ((long)y * w + x) * 4, px, 4); continue; }
      ox = (int)((long)x * tw / w);
      /* (colour weighted by alpha, so masked-out samples don't darken the average) */
      acc[ox * 4] += px[0] * px[3]; acc[ox * 4 + 1] += px[1] * px[3]; acc[ox * 4 + 2] += px[2] * px[3]; acc[ox * 4 + 3] += px[3];
      cnt[ox]++;
    }
    if (acc && oy_next != oy) {
      u8 *o = out->rgba + (long)oy * tw * 4;
      for (x = 0; x < tw; x++) {
        if (cnt[x]) {
          u32 a = acc[x * 4 + 3];
          o[x * 4 + 3] = (u8)(a / cnt[x]);
          if (a) { o[x * 4] = (u8)(acc[x * 4] / a); o[x * 4 + 1] = (u8)(acc[x * 4 + 1] / a); o[x * 4 + 2] = (u8)(acc[x * 4 + 2] / a); }
        }
      }
      memset(acc, 0, (size_t)tw * 4 * sizeof(u32));
      memset(cnt, 0, (size_t)tw * sizeof(int));
    }
  }
  free(acc); free(cnt);
  return 1;
}

/* an image's data with its filters undone (JPEG and CCITT decoded here); *kind: 0 samples, 1 RGB
   from JPEG (out->rgba made already) */
static u8 *image_data(PDoc *d, PObj *stream, size_t *len, int *jpeg, int tw, int th, Img *jimg, int w, int h, int *bpc_override)
{
  const char *imf = NULL;
  PObj *parms = NULL;
  u8 *data = pdf_stream_data_raw(d, stream, len, &imf, &parms);
  *jpeg = 0;
  if (!data || !imf) return data;
  if (!strcmp(imf, "DCTDecode") || !strcmp(imf, "DCT")) {
    u8 *rgb = img_decode(data, (u32)*len, (u32)tw, (u32)th);
    long i, n = (long)tw * th;
    free(data);
    if (!rgb) return NULL;
    if (!(jimg->rgba = (u8 *)malloc((size_t)n * 4))) { free(rgb); return NULL; }
    for (i = 0; i < n; i++) { jimg->rgba[i * 4] = rgb[i * 3]; jimg->rgba[i * 4 + 1] = rgb[i * 3 + 1]; jimg->rgba[i * 4 + 2] = rgb[i * 3 + 2]; jimg->rgba[i * 4 + 3] = 255; }
    jimg->w = tw; jimg->h = th;
    free(rgb);
    *jpeg = 1;
    return (u8 *)1;                                  /* (non-NULL: success) */
  }
  if (!strcmp(imf, "CCITTFaxDecode") || !strcmp(imf, "CCF")) {
    size_t n;
    PObj *bi = pdf_dget(d, parms, "BlackIs1"), *ba = pdf_dget(d, parms, "EncodedByteAlign"), *eol = pdf_dget(d, parms, "EndOfLine"), *eob = pdf_dget(d, parms, "EndOfBlock");
    u8 *o = flt_ccitt(data, *len, (int)pdf_dint(d, parms, "K", 0), (int)pdf_dint(d, parms, "Columns", w), (int)pdf_dint(d, parms, "Rows", h),
                      bi && bi->type == PO_BOOL && bi->u.b, ba && ba->type == PO_BOOL && ba->u.b, eol && eol->type == PO_BOOL && eol->u.b,
                      !(eob && eob->type == PO_BOOL && !eob->u.b), &n);
    free(data);
    *len = n;
    *bpc_override = 1;
    return o;
  }
  /* JPX and JBIG2: not decoded */
  free(data);
  return NULL;
}

/* a mask image (explicit /Mask stream or /SMask) unpacked to its own size; alpha in .rgba[3] */
static int load_mask(Render *r, PObj *m, int soft, int tw, int th, Img *out)
{
  PDoc *d = r->d;
  int w = (int)pdf_dint(d, m, "Width", 0), h = (int)pdf_dint(d, m, "Height", 0), bpc = (int)pdf_dint(d, m, "BitsPerComponent", soft ? 8 : 1), jpeg = 0, ok;
  size_t len;
  u8 *data;
  double dec[2] = { 0, 1 };
  PObj *da = pdf_dget(d, m, "Decode");
  Img j;
  memset(&j, 0, sizeof j);
  if (w < 1 || h < 1 || w > 30000 || h > 30000) return 0;
  if (tw > w) tw = w;
  if (th > h) th = h;
  if (da && da->type == PO_ARRAY && da->u.arr.n >= 2) { dec[0] = pobj_num(pdf_resolve(d, da->u.arr.v[0])); dec[1] = pobj_num(pdf_resolve(d, da->u.arr.v[1])); }
  data = image_data(d, m, &len, &jpeg, tw, th, &j, w, h, &bpc);
  if (!data) return 0;
  if (jpeg) {
    long i, n = (long)j.w * j.h;
    for (i = 0; i < n; i++) j.rgba[i * 4 + 3] = j.rgba[i * 4];       /* (a JPEG soft mask: its grey is the alpha) */
    *out = j;
    return 1;
  }
  if (soft) {
    /* grey samples become alpha */
    Img g;
    ok = unpack(d, data, len, w, h, bpc, cs_device(CS_GRAY), dec, 2, 0, NULL, 0, tw, th, &g);
    if (ok) { long i, n = (long)g.w * g.h; for (i = 0; i < n; i++) g.rgba[i * 4 + 3] = g.rgba[i * 4]; *out = g; }
  } else {
    /* an explicit mask: like a stencil, 0 samples painted */
    ok = unpack(d, data, len, w, h, 1, NULL, dec, 2, 1, NULL, 0, tw, th, out);
  }
  free(data);
  return ok;
}

/* the RGBA image drawn through the CTM (unit square -> page), the clip and the fill alpha;
   stencil: drawn in the fill colour where its alpha says */
static void blit(Render *r, const Img *im, const Img *mask, int stencil, int smooth)
{
  GState *g = GS(r);
  Mat inv;
  double xs[4], ys[4], bx0, by0, bx1, by1;
  int x0, y0, x1, y1, x, y, k, ga = (int)(g->fa * 255 + 0.5);
  const Clip *c = g->clip;
  const u8 *mrow;
  if (!mat_invert(g->ctm, &inv)) return;
  mat_apply(&g->ctm, 0, 0, &xs[0], &ys[0]); mat_apply(&g->ctm, 1, 0, &xs[1], &ys[1]);
  mat_apply(&g->ctm, 1, 1, &xs[2], &ys[2]); mat_apply(&g->ctm, 0, 1, &xs[3], &ys[3]);
  bx0 = bx1 = xs[0]; by0 = by1 = ys[0];
  for (k = 1; k < 4; k++) { if (xs[k] < bx0) bx0 = xs[k]; if (xs[k] > bx1) bx1 = xs[k]; if (ys[k] < by0) by0 = ys[k]; if (ys[k] > by1) by1 = ys[k]; }
  x0 = (int)floor(bx0); y0 = (int)floor(by0); x1 = (int)ceil(bx1); y1 = (int)ceil(by1);
  if (x0 < c->x0) x0 = c->x0;
  if (y0 < c->y0) y0 = c->y0;
  if (x1 > c->x1) x1 = c->x1;
  if (y1 > c->y1) y1 = c->y1;
  for (y = y0; y < y1; y++) {
    /* image space along the row: u, v at the first pixel's centre and their steps */
    double u = inv.a * (x0 + 0.5) + inv.c * (y + 0.5) + inv.e, v = inv.b * (x0 + 0.5) + inv.d * (y + 0.5) + inv.f;
    mrow = c->mask ? ((const u8 *)((const Clip *)c)->mask ? NULL : NULL) : NULL;
    for (x = x0; x < x1; x++, u += inv.a, v += inv.b) {
      double fx, fy;
      int ix, iy, a;
      u8 rgb[3];
      if (u < 0 || u >= 1 || v <= 0 || v > 1) continue;
      fx = u * im->w; fy = (1 - v) * im->h;
      ix = (int)fx; iy = (int)fy;
      if (ix >= im->w) ix = im->w - 1;
      if (iy >= im->h) iy = im->h - 1;
      if (smooth && im->w > 1 && im->h > 1) {
        /* bilinear between the four samples around the point */
        double ax = fx - 0.5, ay = fy - 0.5, wx, wy, acc[4] = { 0, 0, 0, 0 };
        int jx = (int)floor(ax), jy = (int)floor(ay), dx, dy;
        wx = ax - jx; wy = ay - jy;
        for (dy = 0; dy < 2; dy++) for (dx = 0; dx < 2; dx++) {
          int sx = jx + dx, sy = jy + dy;
          double wgt = (dx ? wx : 1 - wx) * (dy ? wy : 1 - wy);
          const u8 *p;
          if (sx < 0) sx = 0;
          if (sy < 0) sy = 0;
          if (sx >= im->w) sx = im->w - 1;
          if (sy >= im->h) sy = im->h - 1;
          p = im->rgba + ((long)sy * im->w + sx) * 4;
          acc[0] += p[0] * wgt * p[3]; acc[1] += p[1] * wgt * p[3]; acc[2] += p[2] * wgt * p[3]; acc[3] += p[3] * wgt;
        }
        a = (int)(acc[3] + 0.5);
        if (acc[3] > 0) for (k = 0; k < 3; k++) rgb[k] = (u8)(acc[k] / acc[3] + 0.5);
        else rgb[0] = rgb[1] = rgb[2] = 0;
      } else {
        const u8 *p = im->rgba + ((long)iy * im->w + ix) * 4;
        rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2]; a = p[3];
      }
      if (mask) {
        int mx = (int)(u * mask->w), my = (int)((1 - v) * mask->h);
        if (mx >= mask->w) mx = mask->w - 1;
        if (my >= mask->h) my = mask->h - 1;
        a = a * mask->rgba[((long)my * mask->w + mx) * 4 + 3] / 255;
      }
      if (!a) continue;
      if (c->mask) a = a * clip_at(c, r->W, x, y) / 255;
      if (ga < 255) a = a * ga / 255;
      put_pixel(r, x, y, stencil ? g->frgb : rgb, a, g->blend);
    }
  }
  (void)mrow;
}

void draw_image(Render *r, PObj *img, PObj *res, PObj *idict, const u8 *idata, size_t ilen)
{
  PDoc *d = r->d;
  PObj *dict = img ? img : idict, *csobj, *mask, *smask, *dec;
  PObj tmp;
  int w, h, bpc, stencil, jpeg = 0, tw, th, ndecode = 0, nkey = 0, colorkey[64], smooth, ok, i;
  double decode[64], sw, sh;
  CSpace *cs = NULL;
  size_t len = 0;
  u8 *data = NULL;
  Img im, mk;
  GState *g = GS(r);
  memset(&im, 0, sizeof im); memset(&mk, 0, sizeof mk);
  if (!dict) return;
  w = (int)pobj_num(ikey(d, dict, "Width", idict ? "W" : NULL));
  h = (int)pobj_num(ikey(d, dict, "Height", idict ? "H" : NULL));
  {
    PObj *im_ = ikey(d, dict, "ImageMask", idict ? "IM" : NULL);
    stencil = im_ && im_->type == PO_BOOL && im_->u.b;
  }
  bpc = stencil ? 1 : (int)pobj_num(ikey(d, dict, "BitsPerComponent", idict ? "BPC" : NULL));
  if (w < 1 || h < 1 || w > 30000 || h > 30000) return;
  if (!bpc) bpc = 8;
  /* how big it will be on the device: sizes to unpack it at */
  sw = sqrt(g->ctm.a * g->ctm.a + g->ctm.b * g->ctm.b);
  sh = sqrt(g->ctm.c * g->ctm.c + g->ctm.d * g->ctm.d);
  tw = sw * 1.25 < w ? (int)ceil(sw * 1.25) : w;
  th = sh * 1.25 < h ? (int)ceil(sh * 1.25) : h;
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  if ((long)tw * th > 12000000L) { tw = w > 3000 ? 3000 : w; th = (int)((long)h * tw / w); if (th < 1) th = 1; }
  smooth = !stencil && bpc > 1 && !(sw > w * 4 && sh > h * 4 && w * h < 4096);   /* (tiny images blown up big: kept sharp, as icons and pixel art) */
  {
    PObj *ip = ikey(d, dict, "Interpolate", idict ? "I" : NULL);
    if (ip && ip->type == PO_BOOL && ip->u.b && !stencil) smooth = sw > w || sh > h;
  }
  /* colour space */
  if (!stencil) {
    csobj = ikey(d, dict, "ColorSpace", idict ? "CS" : NULL);
    if (idict && csobj) expand_name(csobj);
    if (csobj) cs = cs_load(d, csobj, res, 0);
    if (!cs) cs = cs_device(CS_GRAY);
    if (cs->kind == CS_PATTERN) { cs_release(cs); return; }
  }
  /* the data */
  if (img) data = image_data(d, img, &len, &jpeg, tw, th, &im, w, h, &bpc);
  else {
    /* an inline image: a stream made from its bytes and dictionary */
    memset(&tmp, 0, sizeof tmp);
    tmp.type = PO_STREAM;
    {
      PObj *f = ikey(d, idict, "Filter", "F"), *dp = ikey(d, idict, "DecodeParms", "DP");
      PObj *fake = pobj_new(PO_STREAM);
      if (fake) {
        if (f) pobj_dict_set(fake, "Filter", expand_name(pobj_copy(f)));
        if (dp) pobj_dict_set(fake, "DecodeParms", pobj_copy(dp));
        if ((fake->sdata = (u8 *)malloc(ilen ? ilen : 1))) { memcpy(fake->sdata, idata, ilen); fake->slen = (long)ilen; fake->onum = -1; }
        data = image_data(d, fake, &len, &jpeg, tw, th, &im, w, h, &bpc);
        pobj_free(fake);
      }
    }
  }
  if (!data) { cs_release(cs); return; }
  if (!jpeg) {
    dec = ikey(d, dict, "Decode", idict ? "D" : NULL);
    if (dec && dec->type == PO_ARRAY) for (i = 0; i < dec->u.arr.n && i < 64; i++) decode[ndecode++] = pobj_num(pdf_resolve(d, dec->u.arr.v[i]));
    mask = img ? pdf_dget(d, dict, "Mask") : NULL;
    if (mask && mask->type == PO_ARRAY) for (i = 0; i < mask->u.arr.n && i < 64; i++) colorkey[nkey++] = (int)pobj_num(pdf_resolve(d, mask->u.arr.v[i]));
    nkey /= 2;
    ok = unpack(d, data, len, w, h, bpc, cs, ndecode ? decode : NULL, ndecode, stencil, nkey ? colorkey : NULL, nkey, tw, th, &im);
    free(data);
    if (!ok) { cs_release(cs); return; }
  }
  /* masks */
  smask = img ? pdf_dget(d, dict, "SMask") : NULL;
  mask = img ? pdf_dget(d, dict, "Mask") : NULL;
  if (smask && smask->type == PO_STREAM) load_mask(r, smask, 1, tw, th, &mk);
  else if (mask && mask->type == PO_STREAM) load_mask(r, mask, 0, tw, th, &mk);
  if (stencil && g->fcs->kind == CS_PATTERN && g->fpat) {
    /* a stencil filled with a pattern: drawn in the pattern's base colour (an approximation) */
  }
  blit(r, &im, mk.rgba ? &mk : NULL, stencil, smooth);
  free(im.rgba); free(mk.rgba);
  cs_release(cs);
}
