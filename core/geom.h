/*
 * geom.h - matrices and paths, shared by Folio's PDF interpreter, its fonts and its rasterizer.
 * Portable C99 (the native tests and vbcc for the Amiga, which has the 68040's FPU).
 */
#ifndef FOLIO_GEOM_H
#define FOLIO_GEOM_H

#include "util.h"

/* an affine matrix as PDF writes it: [a b c d e f], x' = a x + c y + e, y' = b x + d y + f */
typedef struct { double a, b, c, d, e, f; } Mat;
extern const Mat mat_identity;
Mat  mat_make(double a, double b, double c, double d, double e, double f);
Mat  mat_mul(Mat m, Mat n);                  /* m then n (PDF's "m x n": points go through m first) */
int  mat_invert(Mat m, Mat *out);            /* 0 if singular */
void mat_apply(const Mat *m, double x, double y, double *ox, double *oy);
double mat_expansion(const Mat *m);          /* sqrt(|det|): how much it scales lengths on average */

/* a path: operations M (move), L (line), C (curve: three points), Z (close), with their points */
enum { PATH_M = 'M', PATH_L = 'L', PATH_C = 'C', PATH_Z = 'Z' };
typedef struct {
  u8 *ops; int nops, capops;
  double *pts; int npts, cappts;             /* npts: number of points (x, y pairs) */
  int oom;
} Path;
void path_init(Path *p);
void path_free(Path *p);
void path_reset(Path *p);                    /* emptied, its memory kept */
void path_move(Path *p, double x, double y);
void path_line(Path *p, double x, double y);
void path_curve(Path *p, double x1, double y1, double x2, double y2, double x3, double y3);
void path_close(Path *p);
int  path_current(const Path *p, double *x, double *y);   /* the current point; 0 if none */
void path_transform(Path *p, const Mat *m);
void path_append(Path *p, const Path *q, const Mat *m);   /* q's operations added, through m (NULL: as they are) */
/* the bounding box of the points (control points included); 0 if empty */
int  path_bounds(const Path *p, double *x0, double *y0, double *x1, double *y1);
#endif
