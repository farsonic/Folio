/*
 * scantest.c - scans: CCITT Group 4 written and read back, OCR of a page of text (drawn by Folio
 * itself, then as a "scan": the page as a skewed, speckled picture), the scan straightened,
 * cleaned and stored black and white, its text read and laid over it invisibly, and the
 * background of an unevenly lit "photograph" evened out.
 *
 * The OCR's templates are made from the fonts the renderer has. The repository's own PDFs carry
 * whole Liberation and Caladea fonts: they are written out into outdir/ocrfonts first and used.
 *
 *   scantest outdir tables.pdf angles.pdf quill_tables.pdf
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pdf.h"
#include "pdfwrite.h"
#include "filters.h"
#include "render.h"
#include "font.h"
#include "pagetools.h"
#include "pagecontent.h"
#include "editpage.h"
#include "scan.h"
#include "contentedit.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static const char *T;

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
/* the whole TrueType fonts a PDF embeds, written out under the names the renderer looks for */
static void fonts_from(const char *pdf, const char *dir)
{
  size_t n;
  u8 *b = slurp(pdf, &n);
  char err[160];
  PDoc *d = b ? pdf_open(b, n, err, sizeof err) : NULL;
  int k;
  free(b);
  if (!d) return;
  for (k = 1; k < d->nxref; k++) {
    PObj *o = pdf_get(d, k), *ff;
    const char *fn;
    if (!o || o->type != PO_DICT || !pobj_is_name(pobj_dict_get(o, "Type"), "FontDescriptor") || !(fn = pdf_dname(d, o, "FontName"))) continue;
    if ((ff = pdf_dget(d, o, "FontFile2")) && ff->type == PO_STREAM) {
      static const char *const names[][2] = { { "LiberationSans", "LiberationSans-Regular" }, { "LiberationSans-Bold", "LiberationSans-Bold" },
        { "LiberationSerif", "LiberationSerif-Regular" }, { "LiberationSerif-Bold", "LiberationSerif-Bold" }, { "Caladea", "Caladea-Regular" },
        { "Caladea-Bold", "Caladea-Bold" }, { "Carlito-Bold", "Carlito-Bold" } };
      int i;
      for (i = 0; i < 7; i++) if (!strcmp(fn, names[i][0])) {
        size_t sl;
        u8 *sd = pdf_stream_data(d, ff, &sl);
        char p[600];
        FILE *f;
        snprintf(p, sizeof p, "%s/%s.ttf", dir, names[i][1]);
        if (sd && (f = fopen(p, "wb"))) { fwrite(sd, 1, sl, f); fclose(f); }
        free(sd);
      }
    }
  }
  pdf_close(d);
}
/* how alike two texts are (1 - edit distance / length), spaces and line ends taken as one space */
static void squash(const char *s, char *o, int cap)
{
  int n = 0, sp = 1;
  for (; *s && n < cap - 1; s++) {
    if (*s == ' ' || *s == '\n') { if (!sp) o[n++] = ' '; sp = 1; }
    else { o[n++] = *s; sp = 0; }
  }
  while (n && o[n - 1] == ' ') n--;
  o[n] = 0;
}
static double alike(const char *a0, const char *b0)
{
  static char a[4000], b[4000];
  static int row[4001];
  int i, j, na, nb;
  squash(a0, a, sizeof a); squash(b0, b, sizeof b);
  na = (int)strlen(a); nb = (int)strlen(b);
  for (j = 0; j <= nb; j++) row[j] = j;
  for (i = 1; i <= na; i++) {
    int prev = row[0];
    row[0] = i;
    for (j = 1; j <= nb; j++) {
      int t = row[j], c = prev + (a[i - 1] != b[j - 1]);
      if (row[j] + 1 < c) c = row[j] + 1;
      if (row[j - 1] + 1 < c) c = row[j - 1] + 1;
      row[j] = c; prev = t;
    }
  }
  return 1.0 - (double)row[nb] / (na > nb ? na : nb);
}
static const char *const text_sans[] = {
  "The quick brown fox jumps over the lazy dog.",
  "Folio reads scanned pages and finds their words.",
  "Invoice 2026-11, total 1,234.50 for May and June.",
  "Please sign and return both copies by Friday."
};
static const char *const text_serif[] = {
  "A second paragraph is set in a serif typeface,",
  "with capitals, commas; colons: and question marks?",
};
static PDoc *text_page(char *expect, int cap)
{
  PDoc *d = doc_new_blank(595, 842, 1);
  int i;
  double y = 760;
  expect[0] = 0;
  for (i = 0; i < 4; i++, y -= 20) { page_add_text(d, 0, 60, y, text_sans[i], 12, 0, 0); strncat(expect, text_sans[i], (size_t)cap - strlen(expect) - 2); strcat(expect, "\n"); }
  y -= 16;
  for (i = 0; i < 2; i++, y -= 22) { page_add_text(d, 0, 60, y, text_serif[i], 13, 0, 2); strncat(expect, text_serif[i], (size_t)cap - strlen(expect) - 2); strcat(expect, "\n"); }
  return d;
}
/* the page as grey pixels at dpi */
static u8 *grey_of(PDoc *d, int dpi, int *w, int *h)
{
  RCache *rc = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  u8 *g;
  int i;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  o.scale = dpi / 72.0; o.paper = 0xFFFFFF;
  pdf_render(d, rc, 0, &o, &pm);
  rcache_free(rc);
  *w = pm.w; *h = pm.h;
  if (!(g = (u8 *)malloc((size_t)pm.w * (size_t)pm.h))) { free(pm.rgb); return NULL; }
  for (i = 0; i < pm.w * pm.h; i++) g[i] = pm.rgb[i * 3 + 1];
  free(pm.rgb);
  return g;
}
/* a page that is only a picture (grey, w x h) */
static PDoc *picture_page(const u8 *g, int w, int h)
{
  PDoc *d = doc_new_blank(595, 842, 1);
  PObj *dict = pobj_new(PO_DICT), *res;
  size_t zl;
  u8 *z = flt_deflate(g, (size_t)w * (size_t)h, 6, &zl);
  char nm[40], ops[100];
  int num;
  pobj_dict_set(dict, "Type", pobj_name("XObject"));
  pobj_dict_set(dict, "Subtype", pobj_name("Image"));
  pobj_dict_set(dict, "Width", pobj_int(w));
  pobj_dict_set(dict, "Height", pobj_int(h));
  pobj_dict_set(dict, "BitsPerComponent", pobj_int(8));
  pobj_dict_set(dict, "ColorSpace", pobj_name("DeviceGray"));
  pobj_dict_set(dict, "Filter", pobj_name("FlateDecode"));
  num = om_stream(d, dict, z, zl);
  free(z);
  res = pc_resources(d, pc_page(d, 0));
  pc_add_resource(d, res, "XObject", "Scan", om_ref(num), nm, sizeof nm);
  snprintf(ops, sizeof ops, "q 595 0 0 842 0 0 cm /%s Do Q\n", nm);
  pc_add_content(d, 0, ops, strlen(ops), 0, NULL);
  return d;
}
static void rotate(const u8 *in, u8 *out, int w, int h, double deg)
{
  double t = deg * 3.14159265358979 / 180, c = cos(t), s = sin(t);
  int x, y;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
    double dx = x - w / 2.0, dy = y - h / 2.0;
    int sx = (int)floor(c * dx - s * dy + w / 2.0 + 0.5), sy = (int)floor(s * dx + c * dy + h / 2.0 + 0.5);
    out[(size_t)y * (size_t)w + (size_t)x] = sx >= 0 && sx < w && sy >= 0 && sy < h ? in[(size_t)sy * (size_t)w + (size_t)sx] : 255;
  }
}
static char *page_text(PDoc *d)
{
  RCache *rc = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  TextPage t;
  char *s;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  textpage_init(&t);
  o.scale = 1; o.text_only = 1; o.text = &t;
  pdf_render(d, rc, 0, &o, &pm);
  rcache_free(rc);
  free(pm.rgb);
  textpage_finish(&t);
  s = t.no ? textpage_text(&t, 0, t.no - 1) : (char *)calloc(1, 1);
  textpage_free(&t);
  return s;
}

int main(int argc, char **argv)
{
  char dir[600], expect[2000];
  if (argc < 5) { fprintf(stderr, "scantest outdir tables.pdf angles.pdf quill_tables.pdf\n"); return 2; }
  T = argv[1];
  /* ------------------------------------------------------------ Group 4 both ways */
  {
    int w = 300, h = 120, x, y, i, ok = 1;
    u8 *px = (u8 *)calloc((size_t)(w * h), 1), *z, *back;
    size_t zl, bl;
    unsigned s = 12345;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
      s = s * 1103515245u + 12345u;
      px[y * w + x] = ((x / 7 + y / 5) % 3 == 0) ^ ((s >> 16) % 97 == 0) ^ (x > 250 && y > 100) ^ (x == 0) ^ (x == w - 1);
    }
    z = flt_ccitt_g4_encode(px, w, h, &zl);
    CHECK(z && zl < (size_t)(w * h) / 8);
    back = z ? flt_ccitt(z, zl, -1, w, h, 0, 0, 0, 1, &bl) : NULL;
    CHECK(back && bl == (size_t)((w + 7) / 8 * h));
    if (back) for (y = 0; y < h && ok; y++) for (x = 0; x < w; x++) {
      int white = (back[y * ((w + 7) / 8) + x / 8] >> (7 - x % 8)) & 1;
      if (white == px[y * w + x]) { ok = 0; fprintf(stderr, "  G4 differs at %d,%d\n", x, y); break; }
    }
    CHECK(ok);
    for (i = 0; i < w * h; i++) px[i] = 0;
    free(z); free(back);
    z = flt_ccitt_g4_encode(px, w, h, &zl);
    back = z ? flt_ccitt(z, zl, -1, w, h, 0, 0, 0, 1, &bl) : NULL;
    CHECK(back && back[0] == 0xFF && back[bl - 1] == 0xFF);
    free(z); free(back); free(px);
  }
  /* ------------------------------------------------------------ the fonts, then OCR of drawn text */
  snprintf(dir, sizeof dir, "%s/ocrfonts", T);
  {
    char cmd[700];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    if (system(cmd)) return 2;
  }
  fonts_from(argv[2], dir); fonts_from(argv[3], dir); fonts_from(argv[4], dir);
  font_set_dir(dir);
  {
    PDoc *d = text_page(expect, sizeof expect);
    RCache *rc = rcache_new(d);
    OcrResult r;
    char *got;
    double a;
    CHECK(ocr_page(d, rc, 0, 200, NULL, NULL, &r) && r.n > 30);
    got = ocr_text(&r);
    a = got ? alike(got, expect) : 0;
    printf("  OCR of drawn text: %.1f%% alike\n", a * 100);
    if (a < 0.995 && getenv("OCRSHOW")) fprintf(stderr, "  got:\n%s\n", got ? got : "");
    CHECK(a >= 0.97);
    free(got);
    ocr_free(&r);
    rcache_free(rc);
    /* ------------------------------------------------------------ the same page as a crooked, speckled scan */
    {
      int w, h, i;
      u8 *g = grey_of(d, 200, &w, &h), *t;
      PDoc *s;
      ScanOpts so;
      double ang = 0;
      CHECK(g != NULL);
      t = (u8 *)malloc((size_t)w * (size_t)h);
      rotate(g, t, w, h, 1.8);
      {
        unsigned q = 99;
        for (i = 0; i < 1500; i++) { q = q * 1103515245u + 12345u; t[(q >> 8) % (unsigned)(w * h)] = 0; }
      }
      s = picture_page(t, w, h);
      free(g); free(t);
      memset(&so, 0, sizeof so);
      so.deskew = 1; so.despeckle = 1; so.bitonal = 1;
      CHECK(scan_clean_page(s, 0, &so, &ang));
      printf("  skew found: %.2f degrees (made: 1.8)\n", ang);
      CHECK(fabs(fabs(ang) - 1.8) < 0.25);
      {
        PageImage *im;
        int n = page_images(s, 0, &im);
        PObj *o = n == 1 ? pdf_get(s, im[0].num) : NULL;
        CHECK(n == 1 && o && pobj_is_name(pdf_dget(s, o, "Filter"), "CCITTFaxDecode"));
        free(im);
      }
      rc = rcache_new(s);
      CHECK(ocr_page(s, rc, 0, 200, NULL, NULL, &r) && r.n > 30);
      rcache_free(rc);
      got = ocr_text(&r);
      a = got ? alike(got, expect) : 0;
      printf("  OCR of the scan: %.1f%% alike\n", a * 100);
      if (a < 0.995 && getenv("OCRSHOW")) fprintf(stderr, "  got:\n%s\n", got ? got : "");
      CHECK(a >= 0.9);
      free(got);
      CHECK(ocr_add_text_layer(s, 0, &r));
      ocr_free(&r);
      {
        size_t n;
        u8 *b = pdf_save(s, NULL, 0, &n);
        char err[160], p[700], *txt;
        PDoc *r2 = b ? pdf_open(b, n, err, sizeof err) : NULL;
        FILE *f;
        snprintf(p, sizeof p, "%s/scan_ocr.pdf", T);
        if (b && (f = fopen(p, "wb"))) { fwrite(b, 1, n, f); fclose(f); }
        free(b);
        if (r2) pdf_ensure_pages(r2);
        txt = r2 ? page_text(r2) : NULL;
        CHECK(txt && strstr(txt, "brown") && strstr(txt, "Friday"));
        free(txt);
        if (r2) pdf_close(r2);
      }
      pdf_close(s);
    }
    pdf_close(d);
  }
  /* ------------------------------------------------------------ an unevenly lit photograph made even */
  {
    int w = 400, h = 300, x, y;
    u8 *g = (u8 *)malloc((size_t)(w * h));
    PDoc *d;
    ScanOpts so;
    PageImage *im;
    long sum = 0, ink = 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
      int light = 120 + x * 110 / w;
      int dark = (y % 30 < 3 && x % 50 < 35);
      g[y * w + x] = (u8)(dark ? light / 5 : light);
    }
    d = picture_page(g, w, h);
    free(g);
    memset(&so, 0, sizeof so);
    so.camera = 1;
    CHECK(scan_clean_page(d, 0, &so, NULL));
    if (page_images(d, 0, &im) == 1) {
      int ww, hh, comps, bpc, rgb;
      u8 *s = ce_image_samples(d, pdf_get(d, im[0].num), &ww, &hh, &comps, &bpc, &rgb);
      CHECK(s && ww == w && comps == 1);
      if (s) {
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) if (y % 30 >= 6) sum += s[y * w + x]; else if (y % 30 < 3 && x % 50 < 35 && x % 50 > 2) ink += s[y * w + x] < 100;
        CHECK(sum / (w * h * 24 / 30) > 235);
        CHECK(ink > (long)(w * 30 / 50) * (h / 30) * 2);
      }
      free(s);
      free(im);
    }
    pdf_close(d);
  }
  printf("scantest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
