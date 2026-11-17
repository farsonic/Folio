/*
 * annotdrawtest.c - a comment added to a page drawn before: drawing just the new annotation over
 * the earlier drawing (RenderOpts onto, annots_only, annot_first) must come out the same as
 * drawing the whole page again, for each kind the drawing tools make, one after another.
 *
 *   annotdrawtest file.pdf
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "pdfedit.h"
#include "render.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static PDoc *open_file(const char *path)
{
  size_t len;
  char err[160];
  u8 *data = file_read(path, &len);
  PDoc *d;
  if (!data) { fprintf(stderr, "can't read %s\n", path); exit(1); }
  d = pdf_open(data, len, err, sizeof err);
  free(data);
  if (!d) { fprintf(stderr, "%s: %s\n", path, err); exit(1); }
  return d;
}
/* the page drawn whole (onto NULL) or just its annotations from first on over onto */
static int draw(PDoc *d, RCache *c, const u8 *onto, int first, Pixmap *pm)
{
  RenderOpts o;
  memset(&o, 0, sizeof o);
  o.scale = 0.75; o.annots = 1; o.paper = 0xFFFFFF;
  if (onto) { o.onto = onto; o.annots_only = 1; o.annot_first = first; }
  return pdf_render(d, c, 0, &o, pm);
}
/* how many pixel values differ between two drawings of the same size (-1: sizes differ) */
static long differ(const Pixmap *a, const Pixmap *b)
{
  long i, n = 0;
  if (a->w != b->w || a->h != b->h) return -1;
  for (i = 0; i < (long)a->w * a->h * 3; i++) if (a->rgb[i] != b->rgb[i]) n++;
  return n;
}
int main(int argc, char **argv)
{
  PDoc *d;
  RCache *c;
  Pixmap shown, whole, added;
  double pts[] = { 100, 400, 120, 420, 140, 400, 160, 420 };
  int npts[] = { 4 };
  double sq[4] = { 100, 100, 200, 160 }, ln[4] = { 250, 100, 400, 200 };
  double q[8] = { 72, 720, 300, 720, 72, 700, 300, 700 };
  int step;
  if (argc < 2) { fprintf(stderr, "usage: annotdrawtest file.pdf\n"); return 2; }
  d = open_file(argv[1]);
  c = rcache_new(d);
  CHECK(draw(d, c, NULL, 0, &shown) && shown.rgb);
  for (step = 0; step < 5; step++) {
    int ok, first = annot_count(d, 0);
    long n;
    switch (step) {
    case 0: ok = annot_ink(d, 0, pts, npts, 1, 2.5, 0x8000FF, "Tester"); break;
    case 1: ok = annot_shape(d, 0, AN_SQUARE, sq, 2, 0x0080FF, "Tester"); break;
    case 2: ok = annot_shape(d, 0, AN_CIRCLE, sq, 1.5, 0xFF8000, "Tester"); break;
    case 3: ok = annot_shape(d, 0, AN_LINE, ln, 3, 0x00A000, "Tester"); break;
    default: ok = annot_markup(d, 0, AN_HIGHLIGHT, q, 1, 0xFFE000, "Tester", NULL); break;
    }
    CHECK(ok);
    CHECK(annot_count(d, 0) == first + 1);
    CHECK(draw(d, c, NULL, 0, &whole) && whole.rgb);
    CHECK(draw(d, c, shown.rgb, first, &added) && added.rgb);
    n = differ(&whole, &added);
    if (n) fprintf(stderr, "step %d: %ld values differ\n", step, n);
    CHECK(n == 0);
    /* and it did change the drawing */
    CHECK(differ(&shown, &added) > 0);
    free(shown.rgb); free(whole.rgb);
    shown = added;
  }
  free(shown.rgb);
  rcache_free(c);
  pdf_close(d);
  printf("annotdrawtest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
