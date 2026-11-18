/*
 * allocfail.c - running out of memory anywhere while opening and drawing a page: each allocation
 * in turn is made to fail (one at a time, the others succeed), and nothing may crash or leak.
 * The core is built with -DFOLIO_ALLOC_TEST, so its malloc, calloc, realloc and free come here
 * (core/util.h). A PDF made here (an inline image with abbreviated names, PostScript calculator
 * functions and procedures in the content, text, a pattern) is tried at every allocation; files given are tried at up to
 * MAXRUNS allocations spread over all of them.
 *
 *   allocfail [file.pdf...]          (ALLOCFAIL_ALL=1 in the environment: every allocation of every file)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ the allocator (before the
   core's headers: the real malloc here) */
static long fa_count, fa_fail_at = -1, fa_live, fa_failed;
#define HDR 16
static void *fa_take(size_t n)
{
  if (++fa_count == fa_fail_at) { fa_failed = 1; return NULL; }
  return malloc(n + HDR);
}
void *fa_malloc(size_t n)
{
  unsigned char *p = (unsigned char *)fa_take(n);
  if (!p) return NULL;
  memcpy(p, &n, sizeof n);
  fa_live++;
  return p + HDR;
}
void *fa_calloc(size_t n, size_t m)
{
  void *p;
  if (m && n > ((size_t)-1) / 2 / m) return NULL;
  if ((p = fa_malloc(n * m))) memset(p, 0, n * m);
  return p;
}
void fa_free(void *p)
{
  if (!p) return;
  fa_live--;
  free((unsigned char *)p - HDR);
}
void *fa_realloc(void *p, size_t n)
{
  unsigned char *q;
  size_t old;
  if (!p) return fa_malloc(n);
  if (!(q = (unsigned char *)fa_take(n))) return NULL;
  memcpy(&old, (unsigned char *)p - HDR, sizeof old);
  memcpy(q + HDR, p, old < n ? old : n);
  memcpy(q, &n, sizeof n);
  free((unsigned char *)p - HDR);
  return q + HDR;
}

#include "pdf.h"
#include "render.h"
#include "font.h"

#define MAXRUNS 3000

/* opened, every page (up to 2) drawn small, its text collected, closed */
static void draw_all(const u8 *data, size_t len)
{
  char err[160];
  PDoc *d = pdf_open(data, len, err, sizeof err);
  RCache *c;
  int i;
  if (!d) return;
  if ((c = rcache_new(d))) {
    for (i = 0; i < d->npages && i < 2; i++) {
      RenderOpts o;
      Pixmap pm;
      TextPage tp;
      memset(&o, 0, sizeof o);
      o.scale = 0.3; o.paper = 0xFFFFFF; o.annots = 1;
      textpage_init(&tp);
      o.text = &tp;
      pm.rgb = NULL;
      pdf_render(d, c, i, &o, &pm);
      free(pm.rgb);
      textpage_finish(&tp);
      textpage_free(&tp);
    }
    rcache_free(c);
  }
  pdf_close(d);
}
/* each allocation in turn failed, from the first to the last a full run makes (step: every
   step-th one); the number of failures (crashes don't come back) */
static int every_allocation(const char *name, const u8 *data, size_t len, long maxruns)
{
  long total, at, step, runs = 0;
  int bad = 0;
  fa_count = 0; fa_fail_at = -1; fa_live = 0;
  draw_all(data, len);
  total = fa_count;
  if (fa_live) { printf("FAIL %s: %ld blocks left with no failure\n", name, fa_live); return 1; }
  step = total > maxruns ? (total + maxruns - 1) / maxruns : 1;
  for (at = 1; at <= total; at += step) {
    fa_count = 0; fa_fail_at = at; fa_live = 0; fa_failed = 0;
    draw_all(data, len);
    runs++;
    if (fa_live) { printf("FAIL %s: allocation %ld failing left %ld blocks\n", name, at, fa_live); bad++; if (bad > 5) break; }
  }
  fa_fail_at = -1;
  printf("%s: %ld allocations, %ld runs each with one failing, %d leaks\n", name, total, runs, bad);
  return bad;
}

/* the test PDF: an inline image whose names are abbreviated (expanded to their long forms, which
   takes memory), a shading through PostScript calculator functions (parsed into procedures),
   text in a standard font, a tiling pattern */
static u8 *made_pdf(size_t *len)
{
  static const char *const objs[] = {
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> "
      "/Shading << /S1 6 0 R >> /Pattern << /P1 8 0 R >> >> >>",
    NULL,                                   /* 4: the content (below) */
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 200 0] /Function 7 0 R >>",
    NULL,                                   /* 7: the function */
    NULL,                                   /* 8: the pattern */
  };
  static const char content[] =
    "q 20 0 0 20 10 10 cm BI /W 2 /H 2 /CS /RGB /BPC 8 /F /AHx /D [0 1 0 1 0 1] /I false ID\n"
    "FF0000 00FF00 0000FF FFFFFF> EI Q\n"
    "q 50 50 40 0 0 40 cm BI /W 4 /H 1 /CS /G /BPC 1 /IM false /F [/AHx] ID A0> EI Q\n"
    "q 0 100 200 50 re W n /S1 sh Q\n"
    "/Pattern cs /P1 scn 120 10 60 60 re f\n"
    "BT /F1 14 Tf 10 170 Td (Out of memory) Tj ET\n"
    "{ add 1 2 3 4 5 6 7 sub 9 10 11 12 13 14 15 mul } { dup } n\n";
  static const char fn[] = "{ dup 0.5 gt { pop 1 0 0 } { dup 2 mul exch 1 exch sub 0.3 } ifelse }";
  static const char pat[] = "1 0 0 rg 0 0 5 5 re f 0 0 1 rg 5 5 5 5 re f";
  Buf b;
  long off[8];
  int i;
  buf_init(&b);
  buf_adds(&b, "%PDF-1.4\n");
  for (i = 0; i < 8; i++) {
    off[i] = (long)b.len;
    buf_printf(&b, "%d 0 obj\n", i + 1);
    if (i == 3) buf_printf(&b, "<< /Length %d >>\nstream\n%s\nendstream", (int)strlen(content), content);
    else if (i == 6) buf_printf(&b, "<< /FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1] /Length %d >>\nstream\n%s\nendstream", (int)strlen(fn), fn);
    else if (i == 7) buf_printf(&b, "<< /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 10 10] /XStep 10 /YStep 10 /Resources << >> /Length %d >>\nstream\n%s\nendstream", (int)strlen(pat), pat);
    else buf_adds(&b, objs[i]);
    buf_adds(&b, "\nendobj\n");
  }
  {
    long x = (long)b.len;
    buf_printf(&b, "xref\n0 9\n0000000000 65535 f \n");
    for (i = 0; i < 8; i++) buf_printf(&b, "%010ld 00000 n \n", off[i]);
    buf_printf(&b, "trailer\n<< /Size 9 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", x);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

int main(int argc, char **argv)
{
  int i, bad = 0;
  size_t len;
  u8 *pdf = made_pdf(&len);
  long per = getenv("ALLOCFAIL_ALL") ? 1L << 30 : argc > 1 ? MAXRUNS / (argc - 1) : MAXRUNS;   /* (ALLOCFAIL_ALL: every one) */
  if (!pdf) return 2;
  bad += every_allocation("made.pdf", pdf, len, 1L << 30);
  free(pdf);
  for (i = 1; i < argc; i++) {
    u8 *d = file_read(argv[i], &len);
    if (!d) { printf("FAIL can't read %s\n", argv[i]); bad++; continue; }
    bad += every_allocation(argv[i], d, len, per);
    free(d);
  }
  font_free_substitutes();
  printf("allocfail: %d failed\n", bad);
  return bad != 0;
}
