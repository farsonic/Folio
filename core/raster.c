/*
 * raster.c - anti-aliased path filling and stroking (see raster.h).
 *
 * Filling: the path is flattened to straight edges in 24.8 fixed point and sorted by their top.
 * Each pixel row is sampled at four heights; at each, the edges crossing it give a sorted list of
 * crossings, walked with the winding rule into inside spans, whose exact horizontal extent is
 * added up per pixel (partial pixels at the ends, a running difference for the whole ones).
 *
 * Stroking: each piece of a flattened subpath becomes a quadrilateral, each corner a join (a
 * wedge, a miter or a round), each open end a cap, all oriented the same way, so filling the
 * union by the nonzero rule paints the stroke without gaps or double coverage.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "raster.h"

#define SUB 4                         /* sub-scanlines a pixel */
#define FIX 256                       /* fixed point: 8 fraction bits */

typedef struct { double x0, y0, x1, y1, slope; int dir; } Edge;   /* y0 < y1, device pixels */
typedef struct { double x; int dir; } Cross;

/* ------------------------------------------------------------------ flattening */
static void flat_curve(Path *o, double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, double tol)
{
  double ddx = fabs(x0 - 2 * x1 + x2), ddy = fabs(y0 - 2 * y1 + y2), e1, e2, dd;
  int n, i;
  e1 = ddx > ddy ? ddx : ddy;
  ddx = fabs(x1 - 2 * x2 + x3); ddy = fabs(y1 - 2 * y2 + y3);
  e2 = ddx > ddy ? ddx : ddy;
  dd = e1 > e2 ? e1 : e2;
  /* segments needed so the chord stays within tol of the curve */
  n = dbl_int(ceil(sqrt(dd * 0.75 / tol)));
  if (n < 1) n = 1;
  if (n > 200) n = 200;
  for (i = 1; i <= n; i++) {
    double t = (double)i / n, u = 1 - t;
    double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    path_line(o, a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3);
  }
}
void path_flatten(const Path *p, double tol, Path *o)
{
  int i, k = 0;
  double cx = 0, cy = 0;
  if (tol < 0.01) tol = 0.01;
  for (i = 0; i < p->nops; i++) {
    const double *q = p->pts + k * 2;
    switch (p->ops[i]) {
    case PATH_M: path_move(o, q[0], q[1]); cx = q[0]; cy = q[1]; k++; break;
    case PATH_L: path_line(o, q[0], q[1]); cx = q[0]; cy = q[1]; k++; break;
    case PATH_C: flat_curve(o, cx, cy, q[0], q[1], q[2], q[3], q[4], q[5], tol); cx = q[4]; cy = q[5]; k += 3; break;
    case PATH_Z: path_close(o); break;
    }
  }
}

/* ------------------------------------------------------------------ filling */
typedef struct { Edge *e; int n, cap; int oom; } Edges;
static void add_edge(Edges *es, double x0, double y0, double x1, double y1)
{
  Edge *e;
  if (y0 == y1 || es->oom) return;
  if (!(x0 == x0 && x1 == x1 && y0 == y0 && y1 == y1)) return;     /* (NaN) */
  if (es->n >= es->cap) {
    int nc = es->cap ? es->cap * 2 : 256;
    Edge *ne = (Edge *)realloc(es->e, (size_t)nc * sizeof(Edge));
    if (!ne) { es->oom = 1; return; }
    es->e = ne; es->cap = nc;
  }
  e = &es->e[es->n++];
  if (y0 < y1) { e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1; e->dir = 1; }
  else { e->x0 = x1; e->y0 = y1; e->x1 = x0; e->y1 = y0; e->dir = -1; }
  e->slope = (e->x1 - e->x0) / (e->y1 - e->y0);
}
static int by_y0(const void *a, const void *b)
{
  double d = ((const Edge *)a)->y0 - ((const Edge *)b)->y0;
  return d < 0 ? -1 : d > 0;
}
/* the path's edges (curves flattened to a quarter pixel), subpaths closed */
static int build_edges(const Path *p, Edges *es)
{
  int i, k = 0, started = 0;
  double sx = 0, sy = 0, cx = 0, cy = 0;
  Path flat;
  path_init(&flat);
  for (i = 0; i < p->nops; i++) {
    const double *q = p->pts + k * 2;
    switch (p->ops[i]) {
    case PATH_M:
      if (started) add_edge(es, cx, cy, sx, sy);
      sx = cx = q[0]; sy = cy = q[1]; started = 1; k++;
      break;
    case PATH_L: add_edge(es, cx, cy, q[0], q[1]); cx = q[0]; cy = q[1]; started = 1; k++; break;
    case PATH_C: {
      int j, m;
      path_reset(&flat);
      path_move(&flat, cx, cy);
      flat_curve(&flat, cx, cy, q[0], q[1], q[2], q[3], q[4], q[5], 0.2);
      for (j = 1, m = flat.npts; j < m; j++) add_edge(es, flat.pts[(j - 1) * 2], flat.pts[(j - 1) * 2 + 1], flat.pts[j * 2], flat.pts[j * 2 + 1]);
      cx = q[4]; cy = q[5]; k += 3;
      break; }
    case PATH_Z: add_edge(es, cx, cy, sx, sy); cx = sx; cy = sy; break;
    }
  }
  if (started) add_edge(es, cx, cy, sx, sy);
  path_free(&flat);
  return !es->oom;
}
static int by_x(const void *a, const void *b)
{
  double d = ((const Cross *)a)->x - ((const Cross *)b)->x;
  return d < 0 ? -1 : d > 0;
}
int rast_path(const Path *p, int evenodd, int cx0, int cy0, int cx1, int cy1, SpanFn span, void *ud)
{
  Edges es;
  int *acc = NULL, *diff = NULL, nact = 0, next = 0, y, y0, y1, i, *act = NULL, ok = 0, ncap = 0;
  u8 *cov = NULL;
  Cross *cr = NULL;
  double bx0 = 1e30, bx1 = -1e30, by0 = 1e30, by1 = -1e30;
  int W;
  if (cx1 <= cx0 || cy1 <= cy0) return 1;
  memset(&es, 0, sizeof es);
  if (!build_edges(p, &es)) goto done;
  if (!es.n) { ok = 1; goto done; }
  for (i = 0; i < es.n; i++) {
    Edge *e = &es.e[i];
    if (e->y0 < by0) by0 = e->y0;
    if (e->y1 > by1) by1 = e->y1;
    if (e->x0 < bx0) bx0 = e->x0;
    if (e->x1 < bx0) bx0 = e->x1;
    if (e->x0 > bx1) bx1 = e->x0;
    if (e->x1 > bx1) bx1 = e->x1;
  }
  y0 = by0 < cy0 ? cy0 : dbl_int(floor(by0));
  y1 = by1 > cy1 ? cy1 : dbl_int(ceil(by1));
  if (y0 >= y1 || bx1 < cx0 || bx0 > cx1) { ok = 1; goto done; }
  qsort(es.e, (size_t)es.n, sizeof(Edge), by_y0);
  W = cx1 - cx0;
  acc = (int *)calloc((size_t)W + 2, sizeof(int));
  diff = (int *)calloc((size_t)W + 2, sizeof(int));
  cov = (u8 *)malloc((size_t)W + 2);
  act = (int *)malloc((size_t)es.n * sizeof(int));
  ncap = 64;
  cr = (Cross *)malloc((size_t)ncap * sizeof(Cross));
  if (!acc || !diff || !cov || !act || !cr) goto done;
  for (y = y0; y < y1; y++) {
    int s, lo = W, hi = -1, j;
    /* edges that start above the row's bottom join; those ending above its top leave */
    while (next < es.n && es.e[next].y0 < y + 1) act[nact++] = next++;
    for (j = 0; j < nact; ) if (es.e[act[j]].y1 <= y) act[j] = act[--nact]; else j++;
    if (!nact) {
      if (next >= es.n) break;
      continue;
    }
    for (s = 0; s < SUB; s++) {
      double sy = y + (s + 0.5) / SUB;
      int nc = 0, w = 0;
      for (j = 0; j < nact; j++) {
        Edge *e = &es.e[act[j]];
        if (sy < e->y0 || sy >= e->y1) continue;
        if (nc >= ncap) {
          Cross *n2 = (Cross *)realloc(cr, (size_t)ncap * 2 * sizeof(Cross));
          if (!n2) goto done;
          cr = n2; ncap *= 2;
        }
        cr[nc].x = e->x0 + (sy - e->y0) * e->slope;
        cr[nc].dir = e->dir;
        nc++;
      }
      if (nc < 2) continue;
      if (nc > 8) qsort(cr, (size_t)nc, sizeof(Cross), by_x);
      else {                                   /* (insertion sort for the usual few) */
        int a, b;
        for (a = 1; a < nc; a++) { Cross t = cr[a]; for (b = a; b > 0 && cr[b - 1].x > t.x; b--) cr[b] = cr[b - 1]; cr[b] = t; }
      }
      for (j = 0; j + 1 < nc; j++) {
        int inside;
        w += cr[j].dir;
        inside = evenodd ? (j + 1) & 1 : w != 0;
        if (inside) {
          /* the span from crossing j to j+1, in fixed point relative to the clip's left */
          double xa = cr[j].x - cx0, xb = cr[j + 1].x - cx0;
          long fa, fb;
          int pa, pb;
          if (xb <= 0 || xa >= W) continue;
          if (xa < 0) xa = 0;
          if (xb > W) xb = W;
          fa = dbl_long(xa * FIX); fb = dbl_long(xb * FIX);
          if (fb <= fa) continue;
          pa = (int)(fa >> 8); pb = (int)(fb >> 8);
          if (pa < lo) lo = pa;
          if (pb > hi) hi = pb < W ? pb : W - 1;
          if (pa == pb) acc[pa] += (int)((fb - fa) >> 2);
          else {
            acc[pa] += (int)((FIX - (fa & 255)) >> 2);
            if (pb < W) acc[pb] += (int)((fb & 255) >> 2);
            if (pb > pa + 1) { diff[pa + 1] += FIX / SUB; diff[pb] -= FIX / SUB; }
          }
        }
      }
    }
    if (hi < lo) continue;
    {
      int run = 0, x;
      for (x = lo; x <= hi; x++) {
        int v;
        run += diff[x];
        v = acc[x] + run;
        cov[x - lo] = (u8)(v >= 255 ? 255 : v < 0 ? 0 : v);
        acc[x] = 0; diff[x] = 0;
      }
      diff[hi + 1] = 0;
      if (hi + 1 <= W) acc[hi + 1] = 0;
      span(ud, y, cx0 + lo, cx0 + hi + 1, cov);
    }
  }
  ok = 1;
done:
  free(es.e); free(acc); free(diff); free(cov); free(act); free(cr);
  return ok;
}

/* ------------------------------------------------------------------ stroking */
typedef struct { Path *out; const Mat *m; double hw; int round_n; } SCtx;
/* a polygon of n points added to the output, oriented positively (in device space) */
static void poly(SCtx *c, const double *xy, int n)
{
  double area = 0, x, y;
  int i;
  if (n < 3) return;
  for (i = 0; i < n; i++) {
    int j = (i + 1) % n;
    double ax, ay, bx, by;
    mat_apply(c->m, xy[i * 2], xy[i * 2 + 1], &ax, &ay);
    mat_apply(c->m, xy[j * 2], xy[j * 2 + 1], &bx, &by);
    area += ax * by - bx * ay;
  }
  if (fabs(area) < 1e-9) return;
  for (i = 0; i < n; i++) {
    int k = area > 0 ? i : n - 1 - i;
    mat_apply(c->m, xy[k * 2], xy[k * 2 + 1], &x, &y);
    if (!i) path_move(c->out, x, y); else path_line(c->out, x, y);
  }
  path_close(c->out);
}
static void disc(SCtx *c, double cx, double cy)
{
  double xy[2 * 64];
  int i, n = c->round_n;
  for (i = 0; i < n; i++) {
    double a = 2 * 3.14159265358979 * i / n;
    xy[i * 2] = cx + c->hw * cos(a); xy[i * 2 + 1] = cy + c->hw * sin(a);
  }
  poly(c, xy, n);
}
/* the corner at (x, y) between a piece arriving with direction (ux0, uy0) and one leaving with (ux1, uy1) */
static void join(SCtx *c, const StrokeStyle *st, double x, double y, double ux0, double uy0, double ux1, double uy1)
{
  double cross = ux0 * uy1 - uy0 * ux1, dot = ux0 * ux1 + uy0 * uy1, hw = c->hw, xy[8];
  /* the outer side's normals */
  double s = cross > 0 ? -1 : 1;
  double n0x = -uy0 * hw * s, n0y = ux0 * hw * s, n1x = -uy1 * hw * s, n1y = ux1 * hw * s;
  if (fabs(cross) < 1e-9 && dot > 0) return;           /* straight on */
  if (st->join == 1) { disc(c, x, y); return; }
  xy[0] = x; xy[1] = y;
  xy[2] = x + n0x; xy[3] = y + n0y;
  if (st->join == 0) {
    double cosh = sqrt((1 + dot) / 2);                   /* cos of half the angle between the pieces' normals */
    if (cosh > 1e-6 && 1 / cosh <= st->miter) {
      /* the miter's tip: along the bisector of the normals, hw / cos(half angle) out */
      double bx = n0x + n1x, by = n0y + n1y, bl = sqrt(bx * bx + by * by);
      if (bl > 1e-12) {
        xy[4] = x + bx / bl * hw / cosh; xy[5] = y + by / bl * hw / cosh;
        xy[6] = x + n1x; xy[7] = y + n1y;
        poly(c, xy, 4);
        return;
      }
    }
  }
  xy[4] = x + n1x; xy[5] = y + n1y;
  poly(c, xy, 3);
}
/* one open or closed polyline stroked (pts: n points) */
static void stroke_poly(SCtx *c, const StrokeStyle *st, const double *pts, int n, int closed)
{
  int i;
  double hw = c->hw;
  /* drop repeated points */
  double *q = (double *)malloc((size_t)(n + 1) * 2 * sizeof(double));
  int m = 0;
  if (!q) return;
  for (i = 0; i < n; i++) {
    if (m && fabs(pts[i * 2] - q[(m - 1) * 2]) < 1e-9 && fabs(pts[i * 2 + 1] - q[(m - 1) * 2 + 1]) < 1e-9) continue;
    q[m * 2] = pts[i * 2]; q[m * 2 + 1] = pts[i * 2 + 1]; m++;
  }
  if (closed && m > 1 && fabs(q[0] - q[(m - 1) * 2]) < 1e-9 && fabs(q[1] - q[(m - 1) * 2 + 1]) < 1e-9) m--;
  if (m == 1) {
    /* a dot: round caps make a disc, square ones a square */
    if (st->cap == 1) disc(c, q[0], q[1]);
    else if (st->cap == 2) { double xy[8] = { q[0] - hw, q[1] - hw, q[0] + hw, q[1] - hw, q[0] + hw, q[1] + hw, q[0] - hw, q[1] + hw }; poly(c, xy, 4); }
    free(q);
    return;
  }
  if (m < 2) { free(q); return; }
  for (i = 0; i < (closed ? m : m - 1); i++) {
    int j = (i + 1) % m;
    double dx = q[j * 2] - q[i * 2], dy = q[j * 2 + 1] - q[i * 2 + 1], l = sqrt(dx * dx + dy * dy), ux, uy, nx, ny;
    double ax = q[i * 2], ay = q[i * 2 + 1], bx = q[j * 2], by = q[j * 2 + 1], xy[8];
    if (l < 1e-12) continue;
    ux = dx / l; uy = dy / l; nx = -uy * hw; ny = ux * hw;
    if (!closed && st->cap == 2) {
      if (i == 0) { ax -= ux * hw; ay -= uy * hw; }
      if (i == m - 2) { bx += ux * hw; by += uy * hw; }
    }
    xy[0] = ax + nx; xy[1] = ay + ny; xy[2] = bx + nx; xy[3] = by + ny;
    xy[4] = bx - nx; xy[5] = by - ny; xy[6] = ax - nx; xy[7] = ay - ny;
    poly(c, xy, 4);
    /* the join at the far end */
    if (closed || j < m - 1) {
      int k = (j + 1) % m;
      double ex = q[k * 2] - q[j * 2], ey = q[k * 2 + 1] - q[j * 2 + 1], el = sqrt(ex * ex + ey * ey);
      if (el > 1e-12) join(c, st, q[j * 2], q[j * 2 + 1], ux, uy, ex / el, ey / el);
    }
  }
  if (!closed && st->cap == 1) { disc(c, q[0], q[1]); disc(c, q[(m - 1) * 2], q[(m - 1) * 2 + 1]); }
  free(q);
}
/* a polyline cut into its dashes, each stroked open */
static void dashed(SCtx *c, const StrokeStyle *st, const double *pts, int n, int closed)
{
  double total = 0, pos, *seg;
  int idx = 0, on, i, cap = 64, ns = 0;
  int bad = 0;
  for (i = 0; i < st->ndash; i++) { if (!(st->dash[i] >= 0)) bad = 1; total += st->dash[i]; }
  if (bad || !(total > 1e-9)) { stroke_poly(c, st, pts, n, closed); return; }
  {
    /* a negative dash would never end, and dashes far finer than the line is
       long would take for ever: such lines are drawn solid */
    double len = 0;
    for (i = 0; i < (closed ? n : n - 1); i++) {
      int j = (i + 1) % n;
      len += sqrt((pts[j * 2] - pts[i * 2]) * (pts[j * 2] - pts[i * 2]) + (pts[j * 2 + 1] - pts[i * 2 + 1]) * (pts[j * 2 + 1] - pts[i * 2 + 1]));
    }
    if (!(len / total * st->ndash < 100000.0)) { stroke_poly(c, st, pts, n, closed); return; }
  }
  pos = fmod(st->phase, total);
  if (pos < 0) pos += total;
  /* where in the pattern the line starts */
  on = 1;
  while (pos >= st->dash[idx]) { pos -= st->dash[idx]; idx = (idx + 1) % st->ndash; on = !on; }
  if (!(seg = (double *)malloc((size_t)cap * 2 * sizeof(double)))) return;
  if (on) { seg[0] = pts[0]; seg[1] = pts[1]; ns = 1; }
  for (i = 0; i < (closed ? n : n - 1); i++) {
    int j = (i + 1) % n;
    double x0 = pts[i * 2], y0 = pts[i * 2 + 1], x1 = pts[j * 2], y1 = pts[j * 2 + 1];
    double l = sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)), t = 0;
    while (l - t > 1e-9) {
      double left = st->dash[idx] - pos, step = l - t < left ? l - t : left;
      double nx, ny;
      t += step; pos += step;
      nx = x0 + (x1 - x0) * t / l; ny = y0 + (y1 - y0) * t / l;
      if (on) {
        if (ns >= cap) { double *n2 = (double *)realloc(seg, (size_t)cap * 4 * sizeof(double)); if (!n2) { free(seg); return; } seg = n2; cap *= 2; }
        seg[ns * 2] = nx; seg[ns * 2 + 1] = ny; ns++;
      }
      if (pos >= st->dash[idx] - 1e-9) {
        if (on && ns) stroke_poly(c, st, seg, ns, 0);
        ns = 0;
        idx = (idx + 1) % st->ndash; pos = 0; on = !on;
        if (on) { seg[0] = nx; seg[1] = ny; ns = 1; }
      }
    }
  }
  if (on && ns > 1) stroke_poly(c, st, seg, ns, 0);
  free(seg);
}
void stroke_path(const Path *p, const StrokeStyle *st0, const Mat *m, Path *out)
{
  Path flat;
  SCtx c;
  StrokeStyle st = *st0;
  double exp = mat_expansion(m), tol, dev;
  int i, k, start = 0;
  if (exp < 1e-9) return;
  /* the thinnest a line may be on the device */
  dev = st.width * exp;
  if (dev < st.min_width) st.width = st.min_width / exp;
  c.out = out; c.m = m; c.hw = st.width / 2;
  dev = st.width * exp;
  c.round_n = dev < 3 ? 8 : dev < 10 ? 16 : dev < 40 ? 32 : 64;
  tol = 0.2 / exp;
  path_init(&flat);
  path_flatten(p, tol, &flat);
  /* each subpath */
  for (i = 0, k = 0; i <= flat.nops; i++) {
    int end = i == flat.nops || flat.ops[i] == PATH_M;
    if (end && i > 0) {
      int closed = flat.ops[i - 1] == PATH_Z, npts = k - start;
      if (npts >= 1) {
        if (st.ndash > 0) dashed(&c, &st, flat.pts + start * 2, npts, closed);
        else stroke_poly(&c, &st, flat.pts + start * 2, npts, closed);
      }
      start = k;
    }
    if (i < flat.nops && flat.ops[i] != PATH_Z) k++;
  }
  path_free(&flat);
}
