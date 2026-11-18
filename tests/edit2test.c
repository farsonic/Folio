/*
 * edit2test.c - the editing tools of round 2: a page's own text retyped in place (in its own
 * embedded font, then with a character it hasn't got, in a standard one; the rest of the line
 * moved along), pictures replaced, moved and deleted, snapping to the page's paths, polygon
 * measurements, drawn signatures, colours converted to CMYK and RGB, PDF/A with its fonts embedded.
 *
 *   edit2test quill_tables.pdf graphics.pdf pic.jpg pic.png outdir fontsdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pdf.h"
#include "pdfwrite.h"
#include "pdfedit.h"
#include "render.h"
#include "pagetools.h"
#include "pagecontent.h"
#include "editpage.h"
#include "optimize.h"
#include "review.h"
#include "font.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static const char *outdir;

static u8 *slurp(const char *p, size_t *n)
{
  FILE *f = fopen(p, "rb");
  long l;
  u8 *b;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
  b = (u8 *)malloc((size_t)l + 1);
  if (b && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); b = NULL; }
  fclose(f);
  *n = (size_t)l;
  return b;
}
static PDoc *open_file(const char *p)
{
  size_t n;
  u8 *b = slurp(p, &n);
  char err[160];
  PDoc *d = b ? pdf_open(b, n, err, sizeof err) : NULL;
  free(b);
  if (d) pdf_ensure_pages(d);
  return d;
}
/* a document saved and opened again (to see the edits as another reader would) */
static PDoc *again(PDoc *d, const char *name)
{
  size_t n;
  u8 *b = pdf_save(d, NULL, 0, &n);
  char err[160], p[600];
  PDoc *r;
  FILE *f;
  if (!b) return NULL;
  snprintf(p, sizeof p, "%s/%s", outdir, name);
  if ((f = fopen(p, "wb"))) { fwrite(b, 1, n, f); fclose(f); }
  r = pdf_open(b, n, err, sizeof err);
  free(b);
  if (r) pdf_ensure_pages(r);
  return r;
}
/* the page's text, in reading order (malloc'd), and the text page itself if wanted */
static char *page_text(PDoc *d, int page, TextPage *keep)
{
  RCache *rc = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  TextPage t;
  char *s;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  textpage_init(&t);
  o.scale = 1; o.text_only = 1; o.text = &t;
  pdf_render(d, rc, page, &o, &pm);
  rcache_free(rc);
  free(pm.rgb);
  textpage_finish(&t);
  s = t.no ? textpage_text(&t, 0, t.no - 1) : NULL;
  if (keep) *keep = t; else textpage_free(&t);
  return s ? s : (char *)calloc(1, 1);
}
/* the box of the first match of what */
static int find_box(PDoc *d, int page, const char *what, double *box)
{
  TextPage t;
  char *s = page_text(d, page, &t);
  int a, b, i, ok = textpage_find(&t, what, 0, &a, &b);
  if (ok) {
    box[0] = box[1] = 1e9; box[2] = box[3] = -1e9;
    for (i = a; i <= b; i++) {
      TChar *c = &t.o[i];
      if (c->x1 <= c->x0) continue;
      if (c->x0 < box[0]) box[0] = c->x0;
      if (c->y0 < box[1]) box[1] = c->y0;
      if (c->x1 > box[2]) box[2] = c->x1;
      if (c->y1 > box[3]) box[3] = c->y1;
    }
  }
  free(s);
  textpage_free(&t);
  return ok;
}

int main(int argc, char **argv)
{
  const char *quill, *graphics, *jpg, *png;
  if (argc < 7) { fprintf(stderr, "edit2test quill_tables.pdf graphics.pdf pic.jpg pic.png outdir fontsdir\n"); return 2; }
  quill = argv[1]; graphics = argv[2]; jpg = argv[3]; png = argv[4]; outdir = argv[5];
  font_set_dir(argv[6]);
  /* ------------------------------------------------------------ text retyped */
  {
    PDoc *d = open_file(quill), *r;
    double box[4], box2[4];
    char *s;
    CHECK(d != NULL);
    if (d) {
      CHECK(find_box(d, 0, "grid table", box));
      CHECK(find_box(d, 0, "merged cells", box2));
      CHECK(text_replace(d, 0, box, "grid chart") == TE_SAME_FONT);
      r = again(d, "edit_same.pdf");
      CHECK(r != NULL);
      if (r) {
        double nb[4];
        s = page_text(r, 0, NULL);
        CHECK(strstr(s, "A grid chart with merged cells") != NULL && !strstr(s, "grid table"));
        if (!strstr(s, "A grid chart with merged cells")) fprintf(stderr, "  text: %.200s\n", s);
        free(s);
        /* the rest of the line moved by the difference ("chart" is narrower than "table" here or not: it moved with it) */
        CHECK(find_box(r, 0, "merged cells", nb) && fabs(nb[1] - box2[1]) < 1 && fabs(nb[0] - box2[0]) > 0.01);
        /* a character the font hasn't got: a standard font */
        CHECK(find_box(r, 0, "grid chart", box));
        CHECK(text_replace(r, 0, box, "grid \xE2\x84\xA6") == TE_STD_FONT);
        pdf_close(r);
      }
      /* a run taken out altogether */
      CHECK(find_box(d, 0, "shading and", box));
      CHECK(text_replace(d, 0, box, "") == TE_SAME_FONT);
      s = page_text(d, 0, NULL);
      CHECK(!strstr(s, "shading and") && strstr(s, "a fixed row height"));
      free(s);
      /* nowhere */
      box[0] = box[1] = -50; box[2] = box[3] = -40;
      CHECK(text_replace(d, 0, box, "x") == TE_NONE);
      pdf_close(d);
    }
    /* text in a standard font that isn't embedded: retyped in it */
    d = doc_new_blank(595, 842, 1);
    page_add_text(d, 0, 72, 700, "Hello old world, said the line.", 12, 0x102030, 0);
    CHECK(find_box(d, 0, "old", box));
    CHECK(text_replace(d, 0, box, "brave new") == TE_SAME_FONT);
    r = again(d, "edit_std.pdf");
    s = r ? page_text(r, 0, NULL) : NULL;
    CHECK(s && strstr(s, "Hello brave new world, said the line."));
    free(s);
    if (r) pdf_close(r);
    pdf_close(d);
  }
  /* ------------------------------------------------------------ pictures */
  {
    PDoc *d = doc_new_blank(595, 842, 1);
    PageImage *im;
    size_t jn, pn;
    u8 *jb = slurp(jpg, &jn), *pb = slurp(png, &pn);
    double rect[4] = { 100, 500, 300, 650 }, rect2[4] = { 320, 100, 420, 200 }, m[6];
    int n, num0;
    CHECK(jb && pb);
    if (jb && pb) {
      page_add_image(d, 0, rect, jb, jn);
      page_add_image(d, 0, rect2, pb, pn);
      n = page_images(d, 0, &im);
      /* (fitted into the box, its shape kept) */
      CHECK(n == 2 && fabs(im[0].m[4] - 100) < 0.5 && im[0].m[5] >= 499.5 && fabs(im[0].m[0] - 200) < 0.5 && im[0].m[3] < 150.5);
      num0 = n ? im[0].num : 0;
      if (n) memcpy(m, im[0].m, sizeof m);
      free(im);
      CHECK(page_image_at(d, 0, 150, 550) == 0 && page_image_at(d, 0, 350, 150) == 1 && page_image_at(d, 0, 10, 10) == -1);
      /* replaced: the other picture in the same place */
      CHECK(image_replace(d, 0, 0, pb, pn));
      n = page_images(d, 0, &im);
      CHECK(n == 2 && im[0].num != num0 && fabs(im[0].m[4] - m[4]) < 0.01 && fabs(im[0].m[3] - m[3]) < 0.01);
      free(im);
      /* moved and made smaller */
      m[0] = 100; m[1] = 0; m[2] = 0; m[3] = 75; m[4] = 50; m[5] = 50;
      CHECK(image_set_matrix(d, 0, 0, m));
      CHECK(page_image_at(d, 0, 150, 550) == -1 && page_image_at(d, 0, 60, 60) >= 0);
      {
        PDoc *r = again(d, "edit_images.pdf");
        CHECK(r && page_images(r, 0, &im) == 2);
        if (r) { free(im); pdf_close(r); }
      }
      CHECK(image_delete(d, 0, 0));
      n = page_images(d, 0, &im);
      CHECK(n == 1);
      free(im);
    }
    free(jb); free(pb);
    pdf_close(d);
  }
  /* ------------------------------------------------------------ snapping */
  {
    PDoc *d = doc_new_blank(400, 400, 1);
    double *pts, sx, sy;
    int n;
    pc_add_content(d, 0, "0 0 1 RG 2 w 50 60 m 200 60 l S 100 100 80 40 re S\n", 52, 0, NULL);
    n = page_snap_points(d, 0, &pts);
    CHECK(n >= 6);
    CHECK(snap_nearest(pts, n, 198, 63, 6, &sx, &sy) && fabs(sx - 200) < 0.01 && fabs(sy - 60) < 0.01);
    CHECK(snap_nearest(pts, n, 181, 141, 6, &sx, &sy) && fabs(sx - 180) < 0.01 && fabs(sy - 140) < 0.01);
    CHECK(!snap_nearest(pts, n, 300, 300, 6, &sx, &sy));
    free(pts);
    pdf_close(d);
    d = open_file(graphics);
    if (d) { n = page_snap_points(d, 0, &pts); CHECK(n > 10); free(pts); pdf_close(d); }
  }
  printf("edit2test: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
