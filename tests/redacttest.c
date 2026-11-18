/*
 * redacttest.c - redaction (an area, found text, a whole page): the text under the marks is no
 * longer in the content, the pixels under them are cleared, annotations there go; Remove Hidden
 * Information and Sanitize; the optimizer (audit, pictures at a lower resolution, identical streams),
 * Fix Hairlines, Convert to Grey and printer's marks. Each result saved, read back and checked.
 *
 *   redacttest fonts.pdf pic.jpg pic.png outdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "pdfwrite.h"
#include "pdfedit.h"
#include "render.h"
#include "pagetools.h"
#include "pagecontent.h"
#include "redact.h"
#include "optimize.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static const char *outdir;

static PDoc *open_file(const char *path)
{
  size_t len;
  char err[160];
  u8 *data = file_read(path, &len);
  PDoc *d = data ? pdf_open(data, len, err, sizeof err) : NULL;
  free(data);
  if (!d) { fprintf(stderr, "can't open %s\n", path); exit(1); }
  return d;
}
static PDoc *reopen(PDoc *d, const char *name, const SaveOpts *o, size_t *size)
{
  size_t len;
  u8 *out = pdf_save_opts(d, NULL, 0, o, &len);
  char err[160], path[600];
  PDoc *r;
  CHECK(out != NULL);
  if (!out) return NULL;
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  file_write(path, out, len);
  if (size) *size = len;
  r = pdf_open(out, len, err, sizeof err);
  free(out);
  CHECK(r && !r->repaired);
  return r;
}
static void page_text(PDoc *d, int page, TextPage *t)
{
  RCache *c = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  textpage_init(t);
  o.scale = 1; o.text_only = 1; o.text = t;
  pdf_render(d, c, page, &o, &pm);
  rcache_free(c);
  free(pm.rgb);
  textpage_finish(t);
}
static char *text(PDoc *d, int page)
{
  TextPage t;
  char *s;
  page_text(d, page, &t);
  s = t.no ? textpage_text(&t, 0, t.no - 1) : str_dup("");
  textpage_free(&t);
  return s;
}
static int has(PDoc *d, int page, const char *w) { char *s = text(d, page); int r = s && strstr(s, w); free(s); return r; }
static Pixmap draw(PDoc *d, int page, double scale)
{
  RCache *c = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  o.scale = scale; o.paper = 0xFFFFFF; o.annots = 1;
  pdf_render(d, c, page, &o, &pm);
  rcache_free(c);
  return pm;
}
static u32 px(Pixmap pm, int x, int y) { u8 *p = pm.rgb + ((long)y * pm.w + x) * 3; return ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2]; }
/* the content stream of every page and form joined: the raw bytes contain s? */
static int content_has(PDoc *d, int page, const char *s)
{
  size_t n, i, l = strlen(s);
  u8 *c = pc_content(d, d->pages[page], &n);
  int r = 0;
  for (i = 0; c && i + l <= n; i++) if (!memcmp(c + i, s, l)) r = 1;
  free(c);
  return r;
}
int main(int argc, char **argv)
{
  PDoc *d, *r;
  u8 *jpg, *png;
  size_t jl, pl;
  RedactProps rp;
  int n, i;
  if (argc < 5) { fprintf(stderr, "usage: redacttest fonts.pdf pic.jpg pic.png outdir\n"); return 2; }
  outdir = argv[4];
  jpg = file_read(argv[2], &jl); png = file_read(argv[3], &pl);
  redact_props_default(&rp);
  /* ---- redaction on a page of lines like the fonts page's (set in Folio's own fonts, which carry
     their widths, so this doesn't depend on stand-in fonts): a word found, an area, a picture half covered */
  d = doc_new_blank(595, 842, 1);
  {
    static const char *const names[] = { "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Times-Roman", "Times-Bold", "Times-Italic", "Courier", "Courier-Bold" };
    for (i = 0; i < 8; i++) {
      char line[200];
      snprintf(line, sizeof line, "%s: The quick brown fox jumps over the lazy dog 0123456789", names[i]);
      CHECK(page_add_text(d, 0, 40, 812 - i * 30, line, 16, 0, i / 3 == 1 ? 2 : i / 3 == 2 ? 3 : 0));
    }
  }
  {
    TextPage t;
    double area[4] = { 30, 735, 300, 760 }, ir[4] = { 300, 100, 420, 180 };
    double half[4] = { 300, 100, 360, 180 };
    PAnnot *an;
    CHECK(has(d, 0, "Times-Roman: The quick brown fox"));
    CHECK(page_add_image(d, 0, ir, png, pl));
    CHECK(annot_note(d, 0, 100, 745, 0xFFFF00, "T", "a note over the area"));
    page_text(d, 0, &t);
    strcpy(rp.overlay, "Redacted");
    n = redact_mark_text(d, 0, &t, "lazy dog", &rp); printf("lazy dog marked %d times\n", n); CHECK(n == 8);
    textpage_free(&t);
    rp.overlay[0] = 0;
    CHECK(redact_mark_area(d, 0, area, &rp));
    CHECK(redact_mark_area(d, 0, half, &rp));
    CHECK(redact_count(d, -1) == 10);
    r = reopen(d, "redact_marked.pdf", NULL, NULL);
    if (r) { CHECK(redact_count(r, 0) == 10); CHECK(has(r, 0, "lazy dog")); pdf_close(r); }
    n = redact_apply(d, -1); printf("areas applied %d\n", n); CHECK(n >= 10);
    CHECK(redact_count(d, -1) == 0);
    {
      char *s = text(d, 0);
      printf("after redaction: %.400s\n", s);
      CHECK(s && !strstr(s, "lazy dog") && !strstr(s, "lazy"));
      CHECK(s && strstr(s, "The quick brown fox jumps over the"));          /* the rest of each line stays */
      CHECK(s && !strstr(s, "Helvetica-Oblique: The quick") && strstr(s, "Helvetica-Bold: The quick") && strstr(s, "Courier"));   /* (the area covered the third line) */
      free(s);
    }
    n = pdf_annots(d, 0, &an); CHECK(n == 0); pdf_annots_free(an, n);
    {
      /* the picture's covered half is cleared in its own pixels: drawn without the box, it shows black there */
      Pixmap pm = draw(d, 0, 1);
      CHECK(pm.rgb && px(pm, 330, 842 - 140) == 0x000000);           /* under the box */
      CHECK(pm.rgb && px(pm, 390, 842 - 140) != 0x000000 && px(pm, 390, 842 - 140) != 0xFFFFFF);   /* the uncovered half */
      free(pm.rgb);
    }
    r = reopen(d, "redact_applied.pdf", NULL, NULL);
    if (r) { CHECK(!has(r, 0, "lazy")); CHECK(has(r, 0, "quick brown fox")); pdf_close(r); }
    CHECK(redact_mark_page(d, 0, &rp));
    CHECK(redact_apply(d, 0) == 1);
    { char *s = text(d, 0); CHECK(s && !strstr(s, "quick")); free(s); }
  }
  pdf_close(d);
  /* ---- hidden information */
  d = open_file(argv[1]);
  {
    double q[8] = { 72, 720, 300, 720, 72, 700, 300, 700 };
    int what;
    pdf_set_info(d, "Author", "Secret Author");
    CHECK(xmp_update(d));
    CHECK(attach_add(d, "hidden.txt", (const u8 *)"x", 1, NULL));
    CHECK(annot_markup(d, 0, AN_HIGHLIGHT, q, 1, 0xFFFF00, "T", "comment"));
    {
      double lr[4] = { 72, 72, 200, 100 };
      CHECK(page_add_link(d, 0, lr, -1, -1, "https://example.org/"));
    }
    {
      /* a script run on opening, and a thumbnail */
      PObj *root = pdf_dget(d, d->trailer, "Root"), *js = pobj_new(PO_DICT);
      pobj_dict_set(js, "S", pobj_name("JavaScript"));
      pobj_dict_set(js, "JS", pobj_string("app.alert(1)", 12));
      pobj_dict_set(root, "OpenAction", js);
      pobj_dict_set(d->pages[0], "Thumb", pobj_int(0));
      pobj_dict_set(d->pages[0], "PieceInfo", pobj_new(PO_DICT));
    }
    {
      /* invisible text (as an OCR layer draws) */
      PObj *res = pc_resources(d, d->pages[0]);
      char nm[32];
      pc_add_resource(d, res, "Font", "Inv", om_std_font("Helvetica"), nm, sizeof nm);
      { char ops[200]; snprintf(ops, sizeof ops, "BT 3 Tr /%s 12 Tf 72 100 Td (InvisibleWords) Tj ET", nm); CHECK(pc_add_content(d, 0, ops, strlen(ops), 0, NULL)); }
    }
    CHECK(has(d, 0, "InvisibleWords"));
    what = hidden_survey(d);
    CHECK((what & (HID_METADATA | HID_SCRIPTS | HID_ATTACH | HID_COMMENTS | HID_THUMBS | HID_PRIVATE | HID_LINKS)) == (HID_METADATA | HID_SCRIPTS | HID_ATTACH | HID_COMMENTS | HID_THUMBS | HID_PRIVATE | HID_LINKS));
    CHECK(hidden_remove(d, HID_SANITIZE) > 6);
    CHECK((hidden_survey(d) & ~HID_BOOKMARKS) == 0);
    CHECK(!has(d, 0, "InvisibleWords") && has(d, 0, "quick brown fox"));
    r = reopen(d, "redact_sanitized.pdf", NULL, NULL);
    if (r) { char *a = pdf_info(r, "Author"); CHECK(!a); free(a); CHECK((hidden_survey(r) & ~HID_BOOKMARKS) == 0); pdf_close(r); }
  }
  pdf_close(d);
  /* ---- optimizing: a big picture shown small, stored twice */
  {
    const u8 *files[1];
    size_t lens[1], before, after;
    PDoc *b;
    SpaceAudit a;
    int *alias, dup;
    SaveOpts so;
    u8 *big;
    size_t bl;
    /* (a large photograph-like PNG made here: smooth colours) */
    {
      extern u8 *png_write_rgb(const u8 *rgb, int w, int h, int dpi, size_t *len);
      int W = 1200, H = 900, x, y;
      u8 *rgb = (u8 *)malloc((size_t)W * H * 3);
      for (y = 0; y < H; y++) for (x = 0; x < W; x++) { u8 *p = rgb + ((size_t)y * W + x) * 3; p[0] = (u8)(x * 255 / W); p[1] = (u8)(y * 255 / H); p[2] = (u8)((x + y) & 255); }
      big = png_write_rgb(rgb, W, H, 0, &bl);
      free(rgb);
    }
    files[0] = big; lens[0] = bl;
    b = doc_new_blank(595, 842, 1);
    { double rr[4] = { 72, 400, 272, 550 }; CHECK(page_add_image(b, 0, rr, big, bl)); rr[1] = 100; rr[3] = 250; CHECK(page_add_image(b, 0, rr, big, bl)); }
    optimize_audit(b, &a);
    printf("audit: images %ld (%d) content %ld fonts %ld objects %ld total %ld\n", a.images, a.nimages, a.content, a.fonts, a.objects, a.total);
    CHECK(a.nimages == 2 && a.images > 100000);
    r = reopen(b, "opt_before.pdf", NULL, &before);
    if (r) pdf_close(r);
    alias = optimize_dedupe(b, &dup);
    CHECK(alias && dup == 1);
    CHECK(optimize_images(b, 150, 70) >= 1);
    free(alias);
    alias = optimize_dedupe(b, &dup);
    memset(&so, 0, sizeof so);
    so.compress = 1; so.objstm = 1; so.alias = alias; so.nalias = b->nxref + 1;
    r = reopen(b, "opt_after.pdf", &so, &after);
    printf("optimized: %ld -> %ld bytes\n", (long)before, (long)after);
    CHECK(after * 4 < before);
    if (r) {
      Pixmap pm = draw(r, 0, 1);
      u32 c = px(pm, 172, 842 - 475);
      CHECK(pm.rgb && c != 0xFFFFFF);
      free(pm.rgb);
      pdf_close(r);
    }
    free(alias);
    (void)files; (void)lens;
    /* print production on the same document */
    {
      PObj *res = pc_resources(b, b->pages[0]);
      const char *ops = "0.1 w 1 0 0 RG 72 700 m 500 700 l S 0 0 1 rg 72 600 100 50 re f";
      (void)res;
      CHECK(pc_add_content(b, 0, ops, strlen(ops), 0, NULL));
      CHECK(fix_hairlines(b, 0.5, 0.5) == 1);
      CHECK(content_has(b, 0, "0.5 w"));
      CHECK(convert_to_gray(b) == 1);
      CHECK(!content_has(b, 0, " RG") && content_has(b, 0, " G"));
      {
        Pixmap pm = draw(b, 0, 1);
        u32 c = px(pm, 172, 842 - 475), c2 = px(pm, 100, 842 - 620);
        CHECK(((c >> 16) & 255) == ((c >> 8) & 255) && ((c >> 8) & 255) == (c & 255));   /* the picture grey */
        CHECK(((c2 >> 16) & 255) == (c2 & 255) && c2 != 0xFFFFFF);                          /* the blue box grey */
        free(pm.rgb);
      }
      CHECK(printer_marks(b, 9) == 1);
      {
        double box[4];
        int rot;
        pdf_page_box(b, 0, box, &rot);
        CHECK((int)box[0] == -27 && (int)box[2] == 622);
        CHECK(page_get_box(b, 0, BOX_TRIM, box) && (int)box[2] == 595);
      }
      r = reopen(b, "opt_print.pdf", NULL, NULL);
      if (r) pdf_close(r);
    }
    pdf_close(b);
    free(big);
  }
  free(jpg); free(png);
  (void)i;
  printf("redacttest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
