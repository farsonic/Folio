/*
 * raster.h - Folio's rasterizer: paths (already in device pixels) turned into anti-aliased coverage,
 * a row at a time, for filling, clipping and masks; and strokes turned into paths to fill.
 *
 * Coverage is worked out with four sub-scanlines a pixel and exact horizontal coverage, by the
 * nonzero winding or the even-odd rule. Pixel centres are at (x + 0.5, y + 0.5).
 */
#ifndef FOLIO_RASTER_H
#define FOLIO_RASTER_H

#include "geom.h"

/* a row's coverage: pixels x0..x1-1 of row y, cov[0] the first (0-255) */
typedef void (*SpanFn)(void *ud, int y, int x0, int x1, const u8 *cov);
/* path p (device coordinates) rasterized within the clip box cx0,cy0-cx1,cy1 (exclusive), each row
   with any coverage given to span. Returns 0 if out of memory */
int rast_path(const Path *p, int evenodd, int cx0, int cy0, int cx1, int cy1, SpanFn span, void *ud);

/* a stroke's outline as a path to fill (nonzero): p in user space, the pen's width, cap (0 butt,
   1 round, 2 square), join (0 miter, 1 round, 2 bevel), miter limit and dashes in user space, all
   then mapped by m to device space into out (appended). min_width: the thinnest the line may be
   in device pixels (hairlines) */
typedef struct {
  double width; int cap, join; double miter;
  const double *dash; int ndash; double phase;
  double min_width;
} StrokeStyle;
void stroke_path(const Path *p, const StrokeStyle *st, const Mat *m, Path *out);
/* a path flattened to lines (curves split until within tol of the curve) */
void path_flatten(const Path *p, double tol, Path *out);
#endif
