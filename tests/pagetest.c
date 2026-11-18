/*
 * pagetest.c - the page and document tools: boxes and cropping, page labels, duplicating,
 * reversing, replacing and splitting; headers and footers, Bates numbers, watermarks and
 * backgrounds added and removed; text, pictures and links added; attachments; bookmarks written
 * and made from text styles; stamps, arrows, clouds and measurements, then flattened; new
 * documents from nothing, pictures and text; XMP metadata. Each saved, read back and checked.
 *
 *   pagetest graphics.pdf fonts.pdf pic.jpg pic.png outdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "pdfwrite.h"
#include "pdfedit.h"
#include "render.h"
#include "pagetools.h"

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
static PDoc *reopen(PDoc *d, const char *name)
{
  size_t len;
  u8 *out = pdf_save(d, NULL, 0, &len);
  char err[160], path[600];
  PDoc *r;
  CHECK(out != NULL);
  if (!out) return NULL;
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  file_write(path, out, len);
  r = pdf_open(out, len, err, sizeof err);
  free(out);
  CHECK(r && !r->repaired);
  return r;
}
static TextPage tps[64];
static PDoc *tpdoc;
static TextPage *get_text(void *ud, int page)
{
  PDoc *d = (PDoc *)ud;
  RCache *c;
  RenderOpts o;
  Pixmap pm;
  if (page >= 64) return NULL;
  if (tpdoc != d) { int i; for (i = 0; i < 64; i++) { textpage_free(&tps[i]); textpage_init(&tps[i]); } tpdoc = d; }
  if (tps[page].finished) return &tps[page];
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  o.scale = 1; o.text_only = 1; o.text = &tps[page];
  c = rcache_new(d);
  pdf_render(d, c, page, &o, &pm);
  rcache_free(c);
  free(pm.rgb);
  textpage_finish(&tps[page]);
  return &tps[page];
}
static char *text(PDoc *d, int page)
{
  TextPage t;
  RCache *c = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  char *s;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  textpage_init(&t);
  o.scale = 1; o.text_only = 1; o.text = &t;
  pdf_render(d, c, page, &o, &pm);
  rcache_free(c);
  free(pm.rgb);
  textpage_finish(&t);
  s = t.no ? textpage_text(&t, 0, t.no - 1) : str_dup("");
  textpage_free(&t);
  return s;
}
static int has_text(PDoc *d, int page, const char *what)
{
  char *s = text(d, page);
  int r = s && strstr(s, what) != NULL;
  if (!r) fprintf(stderr, "  page %d text: %.300s\n", page, s ? s : "(none)");
  free(s);
  return r;
}
static u32 pixel(PDoc *d, int page, int x, int y)
{
  RCache *c = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  u32 v = 0;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  o.scale = 0.5; o.annots = 1; o.paper = 0xFFFFFF;
  if (pdf_render(d, c, page, &o, &pm) && x < pm.w && y < pm.h) { u8 *p = pm.rgb + (y * pm.w + x) * 3; v = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2]; }
  free(pm.rgb);
  rcache_free(c);
  return v;
}
int main(int argc, char **argv)
{
  PDoc *d, *o, *r;
  u8 *jpg, *png;
  size_t jl, pl;
  double box[4];
  int rot, n, i;
  if (argc < 6) { fprintf(stderr, "usage: pagetest graphics.pdf fonts.pdf pic.jpg pic.png outdir\n"); return 2; }
  outdir = argv[5];
  jpg = file_read(argv[3], &jl); png = file_read(argv[4], &pl);
  CHECK(jpg && png);
  d = open_file(argv[1]);
  o = open_file(argv[2]);
  n = d->npages;
  /* boxes */
  CHECK(page_crop(d, 0, 0, 10, 20, 30, 40) == 1);
  pdf_page_box(d, 0, box, &rot);
  CHECK((int)box[0] == 10 && (int)box[1] == 20 && (int)box[2] == 595 - 30 && (int)box[3] == 842 - 40);
  { double t[4] = { 5, 5, 590, 837 }, g[4]; CHECK(page_set_box(d, 1, BOX_TRIM, t)); CHECK(page_get_box(d, 1, BOX_TRIM, g) && (int)g[2] == 590); CHECK(!page_get_box(d, 1, BOX_BLEED, g) && (int)g[2] == 595); }
  /* labels */
  {
    PLabel l[2];
    char s[40];
    memset(l, 0, sizeof l);
    l[0].start = 0; l[0].style = 'r'; l[0].first = 1;
    l[1].start = 1; l[1].style = 'D'; l[1].first = 5; strcpy(l[1].prefix, "A-");
    CHECK(page_labels_set(d, l, 2));
    page_label(d, 0, s, sizeof s); CHECK(!strcmp(s, "i"));
    page_label(d, 1, s, sizeof s); CHECK(!strcmp(s, "A-5"));
  }
  /* headers, footers, Bates, watermark, background */
  {
    HFOpts h;
    WMOpts w;
    memset(&h, 0, sizeof h);
    strcpy(h.text[1], "Folio header");
    strcpy(h.text[5], "Page <<page>> of <<pages>> (<<label>>)");
    h.color = 0x000080; h.last = -1;
    CHECK(hf_apply(d, &h, "header") == n);
    CHECK(marks_present(d, "header") == n);
    CHECK(has_text(d, 0, "Folio header"));
    CHECK(has_text(d, 1, "Page 2 of 2 (A-5)"));
    memset(&h, 0, sizeof h);
    strcpy(h.text[2], "<<bates>>"); strcpy(h.bates_prefix, "ABC"); h.bates_digits = 6; h.bates_start = 41; h.last = -1;
    CHECK(hf_apply(d, &h, "bates") == n);
    CHECK(has_text(d, 1, "ABC000042"));
    memset(&w, 0, sizeof w);
    strcpy(w.text, "CONFIDENTIAL"); w.angle = 0; w.color = 0xFF0000; w.last = -1;
    CHECK(watermark_apply(d, &w) == n);
    CHECK(has_text(d, 0, "CONFIDENTIAL"));
    memset(&w, 0, sizeof w);
    w.image = png; w.image_len = pl; w.last = 0; w.behind = 1;
    CHECK(watermark_apply(d, &w) == 1);
    CHECK(background_apply(d, 0xFFFF80, NULL, 0, 0, -1) == n);
    CHECK(pixel(d, 0, 10, 20) == 0xFFFF80);
    CHECK(marks_remove(d, "header") == n);
    CHECK(!marks_present(d, "header"));
    { char *s = text(d, 0); CHECK(s && !strstr(s, "Folio header") && strstr(s, "CONFIDENTIAL")); free(s); }
  }
  /* text, pictures, links */
  CHECK(page_add_text(d, 1, 72, 300, "Added text\nsecond line", 14, 0x008000, 0));
  CHECK(has_text(d, 1, "Added text"));
  { double rr[4] = { 300, 100, 420, 180 }; CHECK(page_add_image(d, 1, rr, jpg, jl)); rr[1] = 200; rr[3] = 280; CHECK(page_add_image(d, 1, rr, png, pl)); }
  {
    double rr[4] = { 72, 72, 200, 100 };
    PLink *lk;
    int k;
    CHECK(page_add_link(d, 1, rr, 0, -1, NULL));
    rr[1] = 120; rr[3] = 140;
    CHECK(page_add_link(d, 1, rr, -1, -1, "https://example.org/"));
    k = pdf_links(d, 1, &lk);
    CHECK(k == 2 && lk[0].page == 0 && lk[1].uri && !strcmp(lk[1].uri, "https://example.org/"));
    pdf_links_free(lk, k);
  }
  /* attachments */
  {
    PAttach *a;
    size_t k;
    u8 *data;
    CHECK(attach_add(d, "notes.txt", (const u8 *)"Some notes\n", 11, "The notes"));
    CHECK(attach_add(d, "b\xc3\xa9ta.dat", (const u8 *)"\x01\x02\x03", 3, NULL));
    CHECK(attach_add(d, "notes.txt", (const u8 *)"Newer notes\n", 12, "The notes"));
    i = attach_list(d, &a);
    CHECK(i == 2 && !strcmp(a[1].name, "notes.txt") && a[1].size == 12 && a[1].desc && !strcmp(a[1].desc, "The notes"));
    attach_free(a, i);
    data = attach_data(d, "notes.txt", &k);
    CHECK(data && k == 12 && !memcmp(data, "Newer notes\n", 12));
    free(data);
    CHECK(attach_delete(d, "b\xc3\xa9ta.dat"));
    i = attach_list(d, &a); CHECK(i == 1); attach_free(a, i);
  }
  /* bookmarks */
  {
    POutline *t = outline_item("Start", 0, -1), *c = outline_item("Child \xc3\xa9", 1, 400);
    t->child = c; t->open = 1;
    t->next = outline_item("Second", 1, -1);
    CHECK(outline_write(d, t));
    pdf_outline_free(t);
  }
  /* comments of the new kinds, then flattened */
  {
    double ar[4] = { 100, 500, 250, 600 }, cl[4] = { 300, 500, 450, 600 }, ms[4] = { 100, 450, 244, 450 };
    double poly[8] = { 100, 300, 172, 300, 172, 372, 100, 372 };
    PAnnot *an;
    CHECK(annot_stamp(d, 0, 300, 700, "APPROVED", 0x008000, "Tester"));
    CHECK(annot_arrow(d, 0, ar, 2, 0xFF0000, "Tester"));
    CHECK(annot_cloud(d, 0, cl, 1.5, 0x0000FF, "Tester"));
    CHECK(annot_measure(d, 0, ms, 72, "in", 0xFF00FF, "Tester"));
    CHECK(annot_measure_area(d, 0, poly, 4, 1, 72, "in", 0xFF00FF, "Tester"));
    CHECK(annot_measure_area(d, 0, poly, 4, 0, 72, "in", 0xFF00FF, "Tester"));
    i = pdf_annots(d, 0, &an);
    CHECK(i == 6);
    {
      int k, mi = 0;
      for (k = 0; k < i; k++) if (an[k].contents && (!strcmp(an[k].contents, "2.00 in") || !strcmp(an[k].contents, "1.00 sq in") || !strcmp(an[k].contents, "4.00 in"))) mi++;
      CHECK(mi == 3);
    }
    pdf_annots_free(an, i);
    r = reopen(d, "page_comments.pdf");
    if (r) { i = pdf_annots(r, 0, &an); CHECK(i == 6); pdf_annots_free(an, i); pdf_close(r); }
    CHECK(annots_flatten(d, 0) == 6);
    i = pdf_annots(d, 0, &an); CHECK(i == 0); pdf_annots_free(an, i);
    CHECK(has_text(d, 0, "APPROVED"));
  }
  pdf_set_info(d, "Title", "Page tools \xe2\x80\x93 test");
  CHECK(xmp_update(d));
  /* saved and read back */
  r = reopen(d, "page_tools.pdf");
  if (r) {
    PLabel *l;
    POutline *ol;
    PAttach *a;
    char s[40];
    CHECK(r->npages == n);
    CHECK(page_labels_get(r, &l) == 2); free(l);
    page_label(r, 1, s, sizeof s); CHECK(!strcmp(s, "A-5"));
    pdf_page_box(r, 0, box, &rot); CHECK((int)box[0] == 10);
    CHECK(has_text(r, 1, "ABC000042") && has_text(r, 1, "Added text") && has_text(r, 0, "APPROVED"));
    CHECK(marks_present(r, "watermark") == n && marks_present(r, "bates") == n && !marks_present(r, "header"));
    ol = pdf_outline(r);
    CHECK(ol && !strcmp(ol->title, "Start") && ol->child && !strcmp(ol->child->title, "Child \xc3\xa9") && ol->child->page == 1 && ol->next && ol->next->page == 1);
    pdf_outline_free(ol);
    i = attach_list(r, &a); CHECK(i == 1 && !strcmp(a[0].name, "notes.txt")); attach_free(a, i);
    {
      PObj *root = pdf_dget(r, r->trailer, "Root"), *md = pdf_dget(r, root, "Metadata");
      size_t k;
      u8 *x = md ? pdf_stream_data(r, md, &k) : NULL;
      CHECK(x && strstr((char *)x, "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Page tools \xe2\x80\x93 test</rdf:li>"));
      free(x);
    }
    pdf_close(r);
  }
  /* pages: duplicate, reverse, replace, split */
  CHECK(page_duplicate(d, 0, 2));
  CHECK(d->npages == n + 2);
  CHECK(has_text(d, 1, "APPROVED") && has_text(d, 2, "APPROVED"));
  CHECK(page_reverse(d, 0, -1));
  CHECK(has_text(d, 0, "Added text"));
  CHECK(page_replace(d, 0, 1, o, 0, 0));
  CHECK(d->npages == n + 2 && has_text(d, 0, "Helvetica"));
  { int *st; int k = split_points(d, 0, 2, &st); CHECK(k == 2 && st[1] == 2); free(st); k = split_points(d, 2, 3, &st); CHECK(k == 3); free(st); }
  r = reopen(d, "page_moved.pdf");
  if (r) { CHECK(r->npages == n + 2); pdf_close(r); }
  pdf_close(d);
  /* new documents */
  {
    const u8 *files[2];
    size_t lens[2];
    PDoc *b = doc_new_blank(300, 400, 3), *im, *tx;
    POutline *ol;
    char *big;
    CHECK(b && b->npages == 3);
    files[0] = jpg; files[1] = png; lens[0] = jl; lens[1] = pl;
    im = doc_from_images(files, lens, 2, 0, 0);
    CHECK(im && im->npages == 2);
    if (im) { pdf_page_box(im, 0, box, &rot); CHECK((int)box[2] == 120 && (int)box[3] == 80); CHECK(pixel(im, 0, 30, 20) != 0xFFFFFF); r = reopen(im, "page_images.pdf"); if (r) pdf_close(r); pdf_close(im); }
    big = (char *)malloc(20000);
    big[0] = 0;
    for (i = 0; i < 150; i++) sprintf(big + strlen(big), "Line %d of the text file, caf\xc3\xa9.\n", i + 1);
    tx = doc_from_text(big, strlen(big), 595, 842, 10, 3);
    CHECK(tx && tx->npages == 3);
    if (tx) { CHECK(has_text(tx, 0, "Line 1 of the text file, caf\xc3\xa9.")); CHECK(has_text(tx, 2, "Line 150")); r = reopen(tx, "page_text.pdf"); if (r) pdf_close(r); pdf_close(tx); }
    free(big);
    /* bookmarks from styles */
    CHECK(page_add_text(b, 0, 40, 380, "Chapter One", 24, 0, 1));
    CHECK(page_add_text(b, 0, 40, 340, "Section A", 16, 0, 1));
    CHECK(page_add_text(b, 0, 40, 300, "Body text body text body text\nmore body text here and there\nand more", 10, 0, 0));
    CHECK(page_add_text(b, 1, 40, 380, "Section B", 16, 0, 1));
    CHECK(page_add_text(b, 1, 40, 340, "Body text again and again\nand again", 10, 0, 0));
    CHECK(page_add_text(b, 2, 40, 380, "Chapter Two", 24, 0, 1));
    ol = outline_from_styles(b, get_text, b);
    CHECK(ol && !strcmp(ol->title, "Chapter One") && ol->child && !strcmp(ol->child->title, "Section A") && ol->child->next && !strcmp(ol->child->next->title, "Section B") && ol->child->next->page == 1);
    CHECK(ol && ol->next && !strcmp(ol->next->title, "Chapter Two") && ol->next->page == 2);
    pdf_outline_free(ol);
    pdf_close(b);
    for (i = 0; i < 64; i++) textpage_free(&tps[i]);
  }
  pdf_close(o);
  free(jpg); free(png);
  printf("pagetest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
