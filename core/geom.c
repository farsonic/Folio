/*
 * geom.c - matrices and paths (see geom.h).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "geom.h"

const Mat mat_identity = { 1, 0, 0, 1, 0, 0 };
Mat mat_make(double a, double b, double c, double d, double e, double f)
{
  Mat m;
  m.a = a; m.b = b; m.c = c; m.d = d; m.e = e; m.f = f;
  return m;
}
Mat mat_mul(Mat m, Mat n)
{
  Mat r;
  r.a = m.a * n.a + m.b * n.c; r.b = m.a * n.b + m.b * n.d;
  r.c = m.c * n.a + m.d * n.c; r.d = m.c * n.b + m.d * n.d;
  r.e = m.e * n.a + m.f * n.c + n.e; r.f = m.e * n.b + m.f * n.d + n.f;
  return r;
}
int mat_invert(Mat m, Mat *o)
{
  double det = m.a * m.d - m.b * m.c;
  if (fabs(det) < 1e-12) return 0;
  o->a = m.d / det; o->b = -m.b / det; o->c = -m.c / det; o->d = m.a / det;
  o->e = -(m.e * o->a + m.f * o->c); o->f = -(m.e * o->b + m.f * o->d);
  return 1;
}
void mat_apply(const Mat *m, double x, double y, double *ox, double *oy)
{
  *ox = m->a * x + m->c * y + m->e;
  *oy = m->b * x + m->d * y + m->f;
}
double mat_expansion(const Mat *m) { return sqrt(fabs(m->a * m->d - m->b * m->c)); }

void path_init(Path *p) { memset(p, 0, sizeof *p); }
void path_free(Path *p) { free(p->ops); free(p->pts); memset(p, 0, sizeof *p); }
void path_reset(Path *p) { p->nops = 0; p->npts = 0; }
/* room for one more operation and n more points */
static int room(Path *p, int n)
{
  if (p->oom) return 0;
  if (p->nops + 1 > p->capops) {
    int nc = p->capops ? p->capops * 2 : 32;
    u8 *o = (u8 *)realloc(p->ops, (size_t)nc);
    if (!o) { p->oom = 1; return 0; }
    p->ops = o; p->capops = nc;
  }
  if (p->npts + n > p->cappts) {
    int nc = p->cappts ? p->cappts * 2 : 64;
    double *q;
    while (nc < p->npts + n) nc *= 2;
    if (!(q = (double *)realloc(p->pts, (size_t)nc * 2 * sizeof(double)))) { p->oom = 1; return 0; }
    p->pts = q; p->cappts = nc;
  }
  return 1;
}
static void pt(Path *p, double x, double y) { p->pts[p->npts * 2] = x; p->pts[p->npts * 2 + 1] = y; p->npts++; }
void path_move(Path *p, double x, double y)
{
  /* a move straight after a move replaces it */
  if (p->nops && p->ops[p->nops - 1] == PATH_M) { p->pts[(p->npts - 1) * 2] = x; p->pts[(p->npts - 1) * 2 + 1] = y; return; }
  if (!room(p, 1)) return;
  p->ops[p->nops++] = PATH_M; pt(p, x, y);
}
void path_line(Path *p, double x, double y)
{
  if (!p->nops) { path_move(p, x, y); return; }
  if (!room(p, 1)) return;
  p->ops[p->nops++] = PATH_L; pt(p, x, y);
}
void path_curve(Path *p, double x1, double y1, double x2, double y2, double x3, double y3)
{
  if (!p->nops) path_move(p, x1, y1);
  if (!room(p, 3)) return;
  p->ops[p->nops++] = PATH_C; pt(p, x1, y1); pt(p, x2, y2); pt(p, x3, y3);
}
void path_close(Path *p)
{
  if (!p->nops || p->ops[p->nops - 1] == PATH_Z) return;
  if (!room(p, 0)) return;
  p->ops[p->nops++] = PATH_Z;
}
int path_current(const Path *p, double *x, double *y)
{
  int i, start = -1, k = 0;
  if (!p->nops || !p->npts) return 0;
  if (p->ops[p->nops - 1] != PATH_Z) { *x = p->pts[(p->npts - 1) * 2]; *y = p->pts[(p->npts - 1) * 2 + 1]; return 1; }
  /* after a close: the start of the last subpath */
  for (i = 0; i < p->nops; i++) {
    if (p->ops[i] == PATH_M) start = k;
    k += p->ops[i] == PATH_C ? 3 : p->ops[i] == PATH_Z ? 0 : 1;
  }
  if (start < 0) return 0;
  *x = p->pts[start * 2]; *y = p->pts[start * 2 + 1];
  return 1;
}
void path_transform(Path *p, const Mat *m)
{
  int i;
  for (i = 0; i < p->npts; i++) {
    double x = p->pts[i * 2], y = p->pts[i * 2 + 1];
    p->pts[i * 2] = m->a * x + m->c * y + m->e;
    p->pts[i * 2 + 1] = m->b * x + m->d * y + m->f;
  }
}
void path_append(Path *p, const Path *q, const Mat *m)
{
  int i, k = 0;
  for (i = 0; i < q->nops; i++) {
    double x[3], y[3];
    int j, n = q->ops[i] == PATH_C ? 3 : q->ops[i] == PATH_Z ? 0 : 1;
    for (j = 0; j < n; j++) {
      x[j] = q->pts[(k + j) * 2]; y[j] = q->pts[(k + j) * 2 + 1];
      if (m) mat_apply(m, x[j], y[j], &x[j], &y[j]);
    }
    k += n;
    switch (q->ops[i]) {
    case PATH_M: path_move(p, x[0], y[0]); break;
    case PATH_L: path_line(p, x[0], y[0]); break;
    case PATH_C: path_curve(p, x[0], y[0], x[1], y[1], x[2], y[2]); break;
    case PATH_Z: path_close(p); break;
    }
  }
}
int path_bounds(const Path *p, double *x0, double *y0, double *x1, double *y1)
{
  int i;
  if (!p->npts) return 0;
  *x0 = *x1 = p->pts[0]; *y0 = *y1 = p->pts[1];
  for (i = 1; i < p->npts; i++) {
    double x = p->pts[i * 2], y = p->pts[i * 2 + 1];
    if (x < *x0) *x0 = x;
    if (x > *x1) *x1 = x;
    if (y < *y0) *y0 = y;
    if (y > *y1) *y1 = y;
  }
  return 1;
}
