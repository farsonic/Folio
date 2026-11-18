/*
 * exporttest.c - export: a document of headings, paragraphs and pictures exported as HTML, RTF,
 * Word (.docx), plain text and PowerPoint (.pptx), and its pictures extracted; the files checked
 * for their structure here (tools/test.sh also opens the .docx and .pptx with python-docx and
 * python-pptx where installed).
 *
 *   exporttest pic.jpg pic.png outdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "render.h"
#include "pagetools.h"
#include "export.h"
#include "zip.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static const char *outdir;
static TextPage tps[16];
static TextPage *get_text(void *ud, int page)
{
  PDoc *d = (PDoc *)ud;
  RCache *c;
  RenderOpts o;
  Pixmap pm;
  if (page >= 16) return NULL;
  if (tps[page].finished) return &tps[page];
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  textpage_init(&tps[page]);
  o.scale = 1; o.text_only = 1; o.text = &tps[page];
  c = rcache_new(d);
  pdf_render(d, c, page, &o, &pm);
  rcache_free(c);
  free(pm.rgb);
  textpage_finish(&tps[page]);
  return &tps[page];
}
static int nimg, njpg;
static int each(void *ud, const char *name, const u8 *data, size_t n)
{
  char path[600];
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  nimg++;
  if (strstr(name, ".jpg")) { njpg++; CHECK(n > 4 && data[0] == 0xFF && data[1] == 0xD8); }
  else CHECK(n > 8 && !memcmp(data, "\x89PNG", 4));
  return file_write(path, data, n);
}
static void save(const char *name, const u8 *data, size_t n) { char p[600]; snprintf(p, sizeof p, "%s/%s", outdir, name); file_write(p, data, n); }
int main(int argc, char **argv)
{
  PDoc *d;
  u8 *jpg, *png, *out;
  size_t jl, pl, n;
  int i;
  if (argc < 4) { fprintf(stderr, "usage: exporttest pic.jpg pic.png outdir\n"); return 2; }
  outdir = argv[3];
  jpg = file_read(argv[1], &jl); png = file_read(argv[2], &pl);
  d = doc_new_blank(595, 842, 2);
  CHECK(page_add_text(d, 0, 72, 780, "Report & Summary", 24, 0, 1));
  CHECK(page_add_text(d, 0, 72, 730, "Introduction", 16, 0, 1));
  CHECK(page_add_text(d, 0, 72, 700, "This is the first paragraph of the body, set in ten\npoint type over two lines <with> markup characters.", 10, 0, 0));
  CHECK(page_add_text(d, 0, 72, 640, "A second paragraph after a gap, caf\xc3\xa9.", 10, 0, 0));
  CHECK(page_add_text(d, 1, 72, 780, "Results", 16, 0, 1));
  CHECK(page_add_text(d, 1, 72, 750, "The results paragraph on page two.", 10, 0, 0));
  { double r[4] = { 72, 300, 272, 450 }; CHECK(page_add_image(d, 0, r, jpg, jl)); r[1] = 100; r[3] = 250; CHECK(page_add_image(d, 1, r, png, pl)); }
  out = export_text(d, EXP_HTML, get_text, d, &n);
  CHECK(out && strstr((char *)out, "<h1>Report &amp; Summary</h1>") && strstr((char *)out, "<h2>Introduction</h2>") && strstr((char *)out, "<h2>Results</h2>"));
  CHECK(out && strstr((char *)out, "<p>This is the first paragraph of the body, set in ten point type over two lines &lt;with&gt; markup characters.</p>"));
  CHECK(out && strstr((char *)out, "<p>A second paragraph after a gap, caf\xc3\xa9.</p>") && strstr((char *)out, "<hr>"));
  if (out) save("export.html", out, n);
  free(out);
  out = export_text(d, EXP_RTF, get_text, d, &n);
  CHECK(out && !strncmp((char *)out, "{\\rtf1", 6) && strstr((char *)out, "caf\\u233?.") && strstr((char *)out, "\\page"));
  if (out) save("export.rtf", out, n);
  free(out);
  out = export_text(d, EXP_TEXT, get_text, d, &n);
  CHECK(out && strstr((char *)out, "Introduction\n\nThis is the first paragraph"));
  free(out);
  out = export_text(d, EXP_DOCX, get_text, d, &n);
  CHECK(out != NULL);
  if (out) {
    ZipReader z;
    int k;
    save("export.docx", out, n);
    CHECK(zip_open(&z, out, n));
    k = zip_find(&z, "word/document.xml");
    CHECK(k >= 0);
    if (k >= 0) { size_t l; u8 *x = zip_read(&z, k, &l); CHECK(x && strstr((char *)x, "<w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t xml:space=\"preserve\">Report &amp; Summary</w:t>")); free(x); }
    zip_close(&z);
  }
  free(out);
  {
    RCache *rc = rcache_new(d);
    out = export_pptx(d, rc, 48, &n);
    rcache_free(rc);
    CHECK(out != NULL);
    if (out) {
      ZipReader z;
      save("export.pptx", out, n);
      CHECK(zip_open(&z, out, n) && zip_find(&z, "ppt/slides/slide2.xml") >= 0 && zip_find(&z, "ppt/media/page1.png") >= 0);
      zip_close(&z);
    }
    free(out);
  }
  CHECK(export_images(d, each, NULL) == 2);
  CHECK(nimg == 2 && njpg == 1);
  for (i = 0; i < 16; i++) textpage_free(&tps[i]);
  pdf_close(d);
  free(jpg); free(png);
  printf("exporttest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
