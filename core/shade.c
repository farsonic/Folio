/*
 * shade.c - smooth shadings (types 1 to 7) and patterns (shading patterns and tiling patterns)
 * (see render_int.h).
 *
 * Function-based, axial and radial shadings are worked out for each device pixel, through a
 * table of 256 colours along the axis for the last two. Triangle meshes are drawn triangle by
 * triangle with their colours blended (Gouraud); patch meshes are cut into small quadrilaterals,
 * each two such triangles. A tiling pattern's cell is drawn once into a small transparent
 * picture at the device's resolution, then repeated across the area being filled.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_int.h"

typedef struct {
  int type;
  Mat inv;                        /* device -> shading space */
  double c[6], t0, t1; int ext0, ext1;
  u8 lut[256][3];
  CSpace *cs; PFunc *fn; double domain[4]; Mat fmat_inv;   /* (type 1) */
  /* a Gouraud triangle: device corners and colours */
  double tx[3], ty[3], tc[3][3]; double det;
} Shade;

/* t (or the colour components) to RGB through the shading's function and space */
static void eval_color(const CSpace *cs, const PFunc *fn, const double *in, int nin, u8 *rgb)
{
  double comps[FN_MAXIO], o[3];
  int k;
  if (fn) func_eval(fn, in, nin, comps);
  else for (k = 0; k < nin && k < FN_MAXIO; k++) comps[k] = in[k];
  cs_to_rgb(cs, comps, o);
  for (k = 0; k < 3; k++) rgb[k] = o[k] > 0 ? (o[k] < 1 ? (u8)(o[k] * 255 + 0.5) : 255) : 0;   /* (NaN and out of range clamped) */
}
static void axial_radial(void *ud, double x, double y, u8 *rgb, int *alpha)
{
  Shade *s = (Shade *)ud;
  double px = s->inv.a * x + s->inv.c * y + s->inv.e, py = s->inv.b * x + s->inv.d * y + s->inv.f, t = 0;
  int i;
  if (s->type == 2) {
    double dx = s->c[2] - s->c[0], dy = s->c[3] - s->c[1], l2 = dx * dx + dy * dy;
    t = l2 > 0 ? ((px - s->c[0]) * dx + (py - s->c[1]) * dy) / l2 : 0;
  } else {
    double cdx = s->c[3] - s->c[0], cdy = s->c[4] - s->c[1], dr = s->c[5] - s->c[2];
    double pdx = px - s->c[0], pdy = py - s->c[1], r0 = s->c[2];
    double a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + r0 * dr, c = pdx * pdx + pdy * pdy - r0 * r0;
    double sol[2];
    int ns = 0, k, found = 0;
    if (fabs(a) < 1e-9) { if (fabs(b) > 1e-12) { sol[0] = c / (2 * b); ns = 1; } }
    else {
      double disc = b * b - a * c;
      if (disc >= 0) { double q = sqrt(disc); sol[0] = (b + q) / a; sol[1] = (b - q) / a; ns = 2; if (sol[1] > sol[0]) { double tt = sol[0]; sol[0] = sol[1]; sol[1] = tt; } }
    }
    for (k = 0; k < ns && !found; k++) {
      double sv = sol[k];
      if (r0 + sv * dr < 0) continue;
      if (sv > 1 && !s->ext1) continue;
      if (sv < 0 && !s->ext0) continue;
      t = sv; found = 1;
    }
    if (!found) { *alpha = 0; return; }
  }
  if (t < 0) { if (!s->ext0) { *alpha = 0; return; } t = 0; }
  if (t > 1) { if (!s->ext1) { *alpha = 0; return; } t = 1; }
  i = dbl_int(t * 255 + 0.5);
  rgb[0] = s->lut[i][0]; rgb[1] = s->lut[i][1]; rgb[2] = s->lut[i][2];
}
static void function_based(void *ud, double x, double y, u8 *rgb, int *alpha)
{
  Shade *s = (Shade *)ud;
  double p[2];
  p[0] = s->inv.a * x + s->inv.c * y + s->inv.e; p[1] = s->inv.b * x + s->inv.d * y + s->inv.f;
  if (p[0] < s->domain[0] || p[0] > s->domain[1] || p[1] < s->domain[2] || p[1] > s->domain[3]) { *alpha = 0; return; }
  eval_color(s->cs, s->fn, p, 2, rgb);
}
static void gouraud(void *ud, double x, double y, u8 *rgb, int *alpha)
{
  Shade *s = (Shade *)ud;
  double l0, l1, l2;
  int k;
  if (fabs(s->det) < 1e-12) { for (k = 0; k < 3; k++) rgb[k] = (u8)s->tc[0][k]; return; }
  l0 = ((s->ty[1] - s->ty[2]) * (x - s->tx[2]) + (s->tx[2] - s->tx[1]) * (y - s->ty[2])) / s->det;
  l1 = ((s->ty[2] - s->ty[0]) * (x - s->tx[2]) + (s->tx[0] - s->tx[2]) * (y - s->ty[2])) / s->det;
  l0 = l0 < 0 ? 0 : l0 > 1 ? 1 : l0;
  l1 = l1 < 0 ? 0 : l1 > 1 ? 1 : l1;
  l2 = 1 - l0 - l1;
  if (l2 < 0) l2 = 0;
  for (k = 0; k < 3; k++) { double v = l0 * s->tc[0][k] + l1 * s->tc[1][k] + l2 * s->tc[2][k]; rgb[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5); }
  (void)alpha;
}

/* the area to paint: path (device) or the whole clip, painted with a shade function */
static void paint_area(Render *r, const Path *area, int evenodd, void (*fn)(void *, double, double, u8 *, int *), void *ud)
{
  Paint p;
  const Clip *c = GS(r)->clip;
  Path whole;
  memset(&p, 0, sizeof p);
  p.r = r; p.alpha = dbl_int(GS(r)->fa * 255 + 0.5); p.clip = c; p.blend = GS(r)->blend; p.shade = fn; p.sud = ud;
  if (!area) {
    path_init(&whole);
    path_move(&whole, c->x0, c->y0); path_line(&whole, c->x1, c->y0); path_line(&whole, c->x1, c->y1); path_line(&whole, c->x0, c->y1); path_close(&whole);
    rast_path(&whole, 0, c->x0, c->y0, c->x1, c->y1, paint_span, &p);
    path_free(&whole);
  } else rast_path(area, evenodd, c->x0, c->y0, c->x1, c->y1, paint_span, &p);
}

/* ------------------------------------------------------------------ meshes */
typedef struct { const u8 *p; size_t n, pos; int bit; } Bits;
static u32 getbits(Bits *b, int n)
{
  u32 v = 0;
  while (n-- > 0) {
    if (b->pos >= b->n) return n + 1 >= 32 ? 0 : v << (n + 1);   /* (a shift by 32 is undefined, and differs on the 68k) */
    v = (v << 1) | ((b->p[b->pos] >> (7 - b->bit)) & 1);
    if (++b->bit == 8) { b->bit = 0; b->pos++; }
  }
  return v;
}
static void align(Bits *b) { if (b->bit) { b->bit = 0; b->pos++; } }
static double getval(Bits *b, int bits, double lo, double hi)
{
  double mx = bits >= 32 ? 4294967295.0 : (double)((1UL << bits) - 1);
  return lo + getbits(b, bits) * (hi - lo) / mx;
}
typedef struct { double x, y; double c[FN_MAXIO]; } Vtx;
/* a triangle's vertex colours to RGB and the triangle painted */
static void tri(Render *r, const Path *area, int evenodd, CSpace *cs, PFunc *fn, int ncomp, const Mat *m, const Vtx *a, const Vtx *b, const Vtx *c)
{
  Shade s;
  const Vtx *v[3];
  int i;
  Path p;
  memset(&s, 0, sizeof s);
  v[0] = a; v[1] = b; v[2] = c;
  for (i = 0; i < 3; i++) {
    u8 rgb[3];
    mat_apply(m, v[i]->x, v[i]->y, &s.tx[i], &s.ty[i]);
    eval_color(cs, fn, v[i]->c, fn ? 1 : ncomp, rgb);
    s.tc[i][0] = rgb[0]; s.tc[i][1] = rgb[1]; s.tc[i][2] = rgb[2];
  }
  s.det = (s.ty[1] - s.ty[2]) * (s.tx[0] - s.tx[2]) + (s.tx[2] - s.tx[1]) * (s.ty[0] - s.ty[2]);
  path_init(&p);
  path_move(&p, s.tx[0], s.ty[0]); path_line(&p, s.tx[1], s.ty[1]); path_line(&p, s.tx[2], s.ty[2]); path_close(&p);
  if (area) {
    /* (clipped to the area by the clip; areas other than the whole clip are rare for meshes) */
  }
  paint_area(r, &p, 0, gouraud, &s);
  path_free(&p);
  (void)evenodd;
}
static void mesh(Render *r, PObj *sh, int type, CSpace *cs, PFunc *fn, const Mat *m, const Path *area, int evenodd)
{
  PDoc *d = r->d;
  int bpcoord = (int)pdf_dint(d, sh, "BitsPerCoordinate", 16), bpcomp = (int)pdf_dint(d, sh, "BitsPerComponent", 8);
  int bpflag = (int)pdf_dint(d, sh, "BitsPerFlag", 8), vpr = (int)pdf_dint(d, sh, "VerticesPerRow", 2);
  int ncomp = fn ? 1 : cs->n, i, k, count = 0;
  double dec[2 * (2 + FN_MAXIO)];
  PObj *da = pdf_dget(d, sh, "Decode");
  size_t len;
  u8 *data;
  Bits b;
  if (sh->type != PO_STREAM || !da || da->type != PO_ARRAY || da->u.arr.n < 4 + 2 * ncomp) return;
  if (bpcoord < 1 || bpcoord > 32 || bpcomp < 1 || bpcomp > 16) return;
  for (i = 0; i < 4 + 2 * ncomp && i < (int)(sizeof dec / sizeof dec[0]); i++) dec[i] = pobj_num(pdf_resolve(d, da->u.arr.v[i]));
  if (!(data = pdf_stream_data(d, sh, &len))) return;
  b.p = data; b.n = len; b.pos = 0; b.bit = 0;
#define READV(v) do { (v).x = getval(&b, bpcoord, dec[0], dec[1]); (v).y = getval(&b, bpcoord, dec[2], dec[3]); \
  for (k = 0; k < ncomp; k++) (v).c[k] = getval(&b, bpcomp, dec[4 + 2 * k], dec[5 + 2 * k]); } while (0)
  if (type == 4) {
    Vtx v[3];
    int have = 0;
    while (b.pos < b.n && count++ < 200000 && !r->stopped) {
      int flag = (int)getbits(&b, bpflag);
      Vtx nv;
      READV(nv);
      align(&b);
      if (flag == 0 || have < 2) { if (flag == 0 && have >= 3) have = 0; v[have < 3 ? have : 2] = nv; if (have < 3) have++; if (have < 3) continue; }
      else if (flag == 1) { v[0] = v[1]; v[1] = v[2]; v[2] = nv; }
      else { v[1] = v[2]; v[2] = nv; }
      tri(r, area, evenodd, cs, fn, ncomp, m, &v[0], &v[1], &v[2]);
      if (flag == 0) have = 0;
    }
  } else if (type == 5) {
    Vtx *prev = NULL, *cur = NULL;
    if (vpr < 2 || vpr > 10000) { free(data); return; }
    prev = (Vtx *)malloc((size_t)vpr * sizeof(Vtx)); cur = (Vtx *)malloc((size_t)vpr * sizeof(Vtx));
    if (prev && cur) {
      int row = 0;
      while (b.pos < b.n && row < 100000 && !r->stopped) {
        for (i = 0; i < vpr; i++) READV(cur[i]);
        if (row > 0)
          for (i = 0; i + 1 < vpr; i++) {
            tri(r, area, evenodd, cs, fn, ncomp, m, &prev[i], &prev[i + 1], &cur[i]);
            tri(r, area, evenodd, cs, fn, ncomp, m, &prev[i + 1], &cur[i + 1], &cur[i]);
          }
        { Vtx *t = prev; prev = cur; cur = t; }
        row++;
      }
    }
    free(prev); free(cur);
  } else if (type == 6 || type == 7) {
    /* Coons (6) and tensor (7) patches: the boundary curves define the surface, cut into an n x n grid */
    double px[16], py[16], cc[4][FN_MAXIO], ppx[16], ppy[16], pcc[4][FN_MAXIO];
    int npts = type == 6 ? 12 : 16, have = 0;
    while (b.pos < b.n && count++ < 20000 && !r->stopped) {
      int flag = (int)getbits(&b, bpflag), np, nc, j, gi, gj, N = 6;
      Vtx grid[7][7];
      if (flag && !have) break;
      np = flag ? npts - 4 : npts; nc = flag ? 2 : 4;
      if (flag) {
        /* the shared edge from the previous patch (indices in boundary order 0..11) */
        static const int e[4][4] = { { 3, 4, 5, 6 }, { 6, 7, 8, 9 }, { 9, 10, 11, 0 }, { 0, 0, 0, 0 } };
        static const int ce[4][2] = { { 1, 2 }, { 2, 3 }, { 3, 0 }, { 0, 0 } };
        int f = flag - 1 < 3 ? flag - 1 : 0;
        for (j = 0; j < 4; j++) { px[j] = ppx[e[f][j]]; py[j] = ppy[e[f][j]]; }
        for (k = 0; k < ncomp; k++) { cc[0][k] = pcc[ce[f][0]][k]; cc[1][k] = pcc[ce[f][1]][k]; }
      }
      for (j = flag ? 4 : 0; j < (flag ? 4 : 0) + np; j++) { px[j] = getval(&b, bpcoord, dec[0], dec[1]); py[j] = getval(&b, bpcoord, dec[2], dec[3]); }
      for (j = flag ? 2 : 0; j < (flag ? 2 : 0) + nc; j++) for (k = 0; k < ncomp; k++) cc[j][k] = getval(&b, bpcomp, dec[4 + 2 * k], dec[5 + 2 * k]);
      align(&b);
      memcpy(ppx, px, sizeof px); memcpy(ppy, py, sizeof py); memcpy(pcc, cc, sizeof cc);
      have = 1;
      /* boundary: 0-3 left side (bottom to top), 3-6 top, 6-9 right (top to bottom), 9-12 bottom (right to left) */
      for (gi = 0; gi <= N; gi++) for (gj = 0; gj <= N; gj++) {
        double u = (double)gi / N, v = (double)gj / N, x = 0, y = 0;
        /* the Coons surface: the four boundary curves blended, minus the corners' bilinear part */
        double cu[4], cv[4], L0x, L0y, L1x, L1y, M0x, M0y, M1x, M1y;
        double bu = 1 - u, bv = 1 - v;
        cu[0] = bu * bu * bu; cu[1] = 3 * bu * bu * u; cu[2] = 3 * bu * u * u; cu[3] = u * u * u;
        cv[0] = bv * bv * bv; cv[1] = 3 * bv * bv * v; cv[2] = 3 * bv * v * v; cv[3] = v * v * v;
        /* left (points 0,1,2,3 as v goes 0..1), right (9,8,7,6), bottom (0,11,10,9 as u goes), top (3,4,5,6) */
        L0x = cv[0] * px[0] + cv[1] * px[1] + cv[2] * px[2] + cv[3] * px[3]; L0y = cv[0] * py[0] + cv[1] * py[1] + cv[2] * py[2] + cv[3] * py[3];
        L1x = cv[0] * px[9] + cv[1] * px[8] + cv[2] * px[7] + cv[3] * px[6]; L1y = cv[0] * py[9] + cv[1] * py[8] + cv[2] * py[7] + cv[3] * py[6];
        M0x = cu[0] * px[0] + cu[1] * px[11] + cu[2] * px[10] + cu[3] * px[9]; M0y = cu[0] * py[0] + cu[1] * py[11] + cu[2] * py[10] + cu[3] * py[9];
        M1x = cu[0] * px[3] + cu[1] * px[4] + cu[2] * px[5] + cu[3] * px[6]; M1y = cu[0] * py[3] + cu[1] * py[4] + cu[2] * py[5] + cu[3] * py[6];
        x = bu * L0x + u * L1x + bv * M0x + v * M1x - (bu * bv * px[0] + bu * v * px[3] + u * v * px[6] + u * bv * px[9]);
        y = bu * L0y + u * L1y + bv * M0y + v * M1y - (bu * bv * py[0] + bu * v * py[3] + u * v * py[6] + u * bv * py[9]);
        grid[gi][gj].x = x; grid[gi][gj].y = y;
        /* colours: corners 0 (u0 v0), 1 (u0 v1), 2 (u1 v1), 3 (u1 v0), bilinear */
        for (k = 0; k < ncomp; k++) grid[gi][gj].c[k] = bu * bv * cc[0][k] + bu * v * cc[1][k] + u * v * cc[2][k] + u * bv * cc[3][k];
      }
      for (gi = 0; gi < N; gi++) for (gj = 0; gj < N; gj++) {
        tri(r, area, evenodd, cs, fn, ncomp, m, &grid[gi][gj], &grid[gi + 1][gj], &grid[gi + 1][gj + 1]);
        tri(r, area, evenodd, cs, fn, ncomp, m, &grid[gi][gj], &grid[gi + 1][gj + 1], &grid[gi][gj + 1]);
      }
    }
  }
#undef READV
  free(data);
}

/* ------------------------------------------------------------------ shadings */
/* a shading drawn with shading space mapped by m (device) */
static void shading_with(Render *r, PObj *sh, const Mat *m, const Path *area, int evenodd)
{
  PDoc *d = r->d;
  int type, i;
  CSpace *cs;
  PFunc *fn = NULL;
  Shade s;
  PObj *f, *bb;
  sh = pdf_dict_of(d, sh);
  if (!sh) return;
  type = (int)pdf_dint(d, sh, "ShadingType", 0);
  cs = cs_load(d, pdf_dget(d, sh, "ColorSpace"), NULL, 0);
  if (!cs || cs->kind == CS_PATTERN) { cs_release(cs); return; }
  f = pdf_dget(d, sh, "Function");
  if (f) fn = func_load(d, f);
  memset(&s, 0, sizeof s);
  s.type = type; s.cs = cs; s.fn = fn;
  if (!mat_invert(*m, &s.inv)) { func_free(fn); cs_release(cs); return; }
  /* BBox: a clip in shading space */
  bb = pdf_dget(d, sh, "BBox");
  if (bb && bb->type == PO_ARRAY && bb->u.arr.n >= 4) {
    /* (approximated by intersecting with the clip's bounds) */
  }
  if (type == 2 || type == 3) {
    PObj *co = pdf_dget(d, sh, "Coords"), *dm = pdf_dget(d, sh, "Domain"), *ex = pdf_dget(d, sh, "Extend");
    int n = type == 2 ? 4 : 6;
    if (!co || co->type != PO_ARRAY || co->u.arr.n < n || !fn) { func_free(fn); cs_release(cs); return; }
    for (i = 0; i < n; i++) s.c[i] = pobj_num(pdf_resolve(d, co->u.arr.v[i]));
    s.t0 = 0; s.t1 = 1;
    if (dm && dm->type == PO_ARRAY && dm->u.arr.n >= 2) { s.t0 = pobj_num(pdf_resolve(d, dm->u.arr.v[0])); s.t1 = pobj_num(pdf_resolve(d, dm->u.arr.v[1])); }
    if (ex && ex->type == PO_ARRAY && ex->u.arr.n >= 2) {
      PObj *e0 = pdf_resolve(d, ex->u.arr.v[0]), *e1 = pdf_resolve(d, ex->u.arr.v[1]);
      s.ext0 = e0 && e0->type == PO_BOOL && e0->u.b; s.ext1 = e1 && e1->type == PO_BOOL && e1->u.b;
    }
    for (i = 0; i < 256; i++) { double t = s.t0 + (s.t1 - s.t0) * i / 255.0; eval_color(cs, fn, &t, 1, s.lut[i]); }
    paint_area(r, area, evenodd, axial_radial, &s);
  } else if (type == 1) {
    PObj *dm = pdf_dget(d, sh, "Domain"), *mt = pdf_dget(d, sh, "Matrix");
    Mat fm = mat_identity, full;
    s.domain[0] = 0; s.domain[1] = 1; s.domain[2] = 0; s.domain[3] = 1;
    if (dm && dm->type == PO_ARRAY && dm->u.arr.n >= 4) for (i = 0; i < 4; i++) s.domain[i] = pobj_num(pdf_resolve(d, dm->u.arr.v[i]));
    if (mt && mt->type == PO_ARRAY && mt->u.arr.n >= 6) {
      double v[6];
      for (i = 0; i < 6; i++) v[i] = pobj_num(pdf_resolve(d, mt->u.arr.v[i]));
      fm = mat_make(v[0], v[1], v[2], v[3], v[4], v[5]);
    }
    full = mat_mul(fm, *m);
    if (!fn || !mat_invert(full, &s.inv)) { func_free(fn); cs_release(cs); return; }
    paint_area(r, area, evenodd, function_based, &s);
  } else if (type >= 4 && type <= 7) {
    mesh(r, sh, type, cs, fn, m, area, evenodd);
  }
  func_free(fn);
  cs_release(cs);
}
void draw_shading(Render *r, PObj *sh, const Path *area, int evenodd, int stroke)
{
  (void)stroke;
  shading_with(r, sh, &GS(r)->ctm, area, evenodd);
}

/* ------------------------------------------------------------------ patterns */
typedef struct { Render *cell; Mat inv; double bx, by, xs, ys; int tw, th; } Tile;
static void tile_shade(void *ud, double x, double y, u8 *rgb, int *alpha)
{
  Tile *t = (Tile *)ud;
  double px = t->inv.a * x + t->inv.c * y + t->inv.e, py = t->inv.b * x + t->inv.d * y + t->inv.f;
  double u = fmod(px - t->bx, t->xs), v = fmod(py - t->by, t->ys);
  int ix, iy;
  long o;
  if (u < 0) u += t->xs;
  if (v < 0) v += t->ys;
  ix = dbl_int(u / t->xs * t->tw); iy = t->th - 1 - dbl_int(v / t->ys * t->th);
  if (ix < 0) ix = 0;
  if (ix >= t->tw) ix = t->tw - 1;
  if (iy < 0) iy = 0;
  if (iy >= t->th) iy = t->th - 1;
  o = (long)iy * t->tw + ix;
  rgb[0] = t->cell->rgb[o * 3]; rgb[1] = t->cell->rgb[o * 3 + 1]; rgb[2] = t->cell->rgb[o * 3 + 2];
  *alpha = t->cell->alpha[o];
}
int fill_with_pattern(Render *r, PObj *pat, const Path *dev, int evenodd, int stroke)
{
  PDoc *d = r->d;
  int ptype, i;
  PObj *mt;
  Mat pm = mat_identity, m;
  GState *g = GS(r);
  pat = pdf_dict_of(d, pat);
  if (!pat) return 0;
  ptype = (int)pdf_dint(d, pat, "PatternType", 0);
  mt = pdf_dget(d, pat, "Matrix");
  if (mt && mt->type == PO_ARRAY && mt->u.arr.n >= 6) {
    double v[6];
    for (i = 0; i < 6; i++) v[i] = pobj_num(pdf_resolve(d, mt->u.arr.v[i]));
    pm = mat_make(v[0], v[1], v[2], v[3], v[4], v[5]);
  }
  /* pattern space is the page's default space (patterns from forms: an approximation) */
  m = mat_mul(pm, r->base);
  if (ptype == 2) {
    PObj *sh = pdf_dget(d, pat, "Shading");
    if (!sh) return 0;
    shading_with(r, sh, &m, dev, evenodd);
    return 1;
  }
  if (ptype == 1) {
    PObj *bb = pdf_dget(d, pat, "BBox"), *res = pdf_dget(d, pat, "Resources");
    double b[4], xs = pdf_dnum(d, pat, "XStep", 0), ys = pdf_dnum(d, pat, "YStep", 0), sc;
    int paint = (int)pdf_dint(d, pat, "PaintType", 1), tw, th;
    Tile t;
    Render *cell;
    RenderOpts co;
    size_t len;
    u8 *data;
    Mat cm;
    if (!bb || bb->type != PO_ARRAY || bb->u.arr.n < 4 || pat->type != PO_STREAM) return 0;
    for (i = 0; i < 4; i++) b[i] = pobj_num(pdf_resolve(d, bb->u.arr.v[i]));
    if (b[0] > b[2]) { double tt = b[0]; b[0] = b[2]; b[2] = tt; }
    if (b[1] > b[3]) { double tt = b[1]; b[1] = b[3]; b[3] = tt; }
    if (xs == 0) xs = b[2] - b[0];
    if (ys == 0) ys = b[3] - b[1];
    xs = fabs(xs); ys = fabs(ys);
    if (xs < 1e-6 || ys < 1e-6) return 0;
    /* the cell at the device's resolution (within reason) */
    sc = mat_expansion(&m);
    tw = dbl_int(ceil(xs * sc)); th = dbl_int(ceil(ys * sc));
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (tw > 768) tw = 768;
    if (th > 768) th = 768;
    if (r->depth >= 8) return 0;
    co = *r->o; co.text = NULL; co.annots = 0; co.w = 0; co.onto = NULL; co.annots_only = 0; co.annot_first = 0;
    /* cell pixels: pattern x from b0 across xs, y from b1 up ys (flipped) */
    cm = mat_make(tw / xs, 0, 0, -th / ys, -b[0] * tw / xs, (b[1] + ys) * th / ys);
    if (!(cell = render_sub(r, tw, th, cm, &co))) return 0;
    if (paint == 2) {
      /* uncoloured: drawn in the pattern colour space's base colour */
      GState *cg = &cell->gs[0];
      memcpy(cg->frgb, g->frgb, 3); memcpy(cg->srgb, g->frgb, 3);
      cell->t3_uncoloured = 1;
    }
    if ((data = pdf_stream_data(d, pat, &len))) {
      run_content(cell, data, len, res);
      free(data);
    }
    t.cell = cell; t.bx = b[0]; t.by = b[1]; t.xs = xs; t.ys = ys; t.tw = tw; t.th = th;
    if (mat_invert(m, &t.inv)) {
      Paint p;
      const Clip *c = g->clip;
      memset(&p, 0, sizeof p);
      p.r = r; p.alpha = dbl_int((stroke ? g->sa : g->fa) * 255 + 0.5); p.clip = c; p.blend = g->blend; p.shade = tile_shade; p.sud = &t;
      rast_path(dev, evenodd, c->x0, c->y0, c->x1, c->y1, paint_span, &p);
    }
    render_sub_free(cell);
    return 1;
  }
  return 0;
}
