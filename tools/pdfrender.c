/*
 * pdfrender.c - (native, for testing) a page drawn to a PPM file, and its text printed:
 *   pdfrender file.pdf page dpi out.ppm [fontdir]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/render.h"
#include "core/font.h"

int main(int argc, char **argv)
{
  size_t len;
  u8 *data;
  char err[200];
  PDoc *d;
  RCache *c;
  RenderOpts o;
  Pixmap pm;
  TextPage tp;
  int page, ok;
  FILE *f;
  if (argc < 5) { fprintf(stderr, "usage: pdfrender file.pdf page dpi out.ppm [fontdir]\n"); return 2; }
  font_set_dir(argc > 5 ? argv[5] : "/tmp/claude-0/-home-user/3a066da9-8230-5cea-817a-b3782336c61e/scratchpad/lpk1/Ledger/TrueType");
  if (!(data = file_read(argv[1], &len))) { fprintf(stderr, "can't read\n"); return 1; }
  d = pdf_open(data, len, err, sizeof err);
  free(data);
  if (!d) { fprintf(stderr, "open failed: %s\n", err); return 1; }
  if (d->need_password && !(getenv("PW") && pdf_password(d, getenv("PW")) && pdf_ensure_pages(d))) { fprintf(stderr, "needs a password\n"); return 1; }
  page = atoi(argv[2]);
  fprintf(stderr, "%d pages%s, crypt %d\n", d->npages, d->repaired ? " (repaired)" : "", d->crypt);
  c = rcache_new(d);
  memset(&o, 0, sizeof o);
  o.scale = atof(argv[3]) / 72.0; o.paper = 0xFFFFFF; o.annots = 1;
  textpage_init(&tp);
  o.text = &tp;
  ok = pdf_render(d, c, page, &o, &pm);
  if (!ok) { fprintf(stderr, "render failed\n"); return 1; }
  if ((f = fopen(argv[4], "wb"))) { fprintf(f, "P6\n%d %d\n255\n", pm.w, pm.h); fwrite(pm.rgb, 3, (size_t)pm.w * pm.h, f); fclose(f); }
  textpage_finish(&tp);
  { char *t = textpage_text(&tp, 0, tp.no - 1); printf("%s\n", t); free(t); }
  textpage_free(&tp);
  free(pm.rgb);
  rcache_free(c);
  pdf_close(d);
  font_free_substitutes();
  return 0;
}
