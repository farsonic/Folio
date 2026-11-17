/*
 * edittest.c - Folio's document changes, saved and read back: every kind of annotation made,
 * pages turned, moved, deleted, inserted blank and brought in from another document, the
 * information set; then the saved file opened again and checked.
 *
 *   edittest base.pdf other.pdf out.pdf
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "pdfedit.h"
#include "pdfwrite.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

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
  if (d->need_password && !(pdf_password(d, getenv("PW") ? getenv("PW") : "") && pdf_ensure_pages(d))) { fprintf(stderr, "%s: password\n", path); exit(1); }
  return d;
}
int main(int argc, char **argv)
{
  PDoc *d, *o, *r;
  double q[16] = { 72, 720, 300, 720, 72, 700, 300, 700,  72, 690, 200, 690, 72, 672, 200, 672 };
  double pts[] = { 100, 400, 120, 420, 140, 400, 160, 420,   100, 380, 160, 380 };
  int npts[] = { 4, 2 };
  double rect[4] = { 300, 500, 520, 600 }, sq[4] = { 100, 100, 200, 160 }, ln[4] = { 250, 100, 400, 200 };
  PAnnot *an;
  int n, n0, i, other_pages;
  size_t len;
  u8 *out;
  char *s;
  if (argc < 4) { fprintf(stderr, "usage: edittest base.pdf other.pdf out.pdf\n"); return 2; }
  d = open_file(argv[1]);
  o = open_file(argv[2]);
  n0 = d->npages;
  other_pages = o->npages;
  /* annotations on page 1 */
  CHECK(annot_markup(d, 0, AN_HIGHLIGHT, q, 2, 0xFFE000, "Tester", "highlighted"));
  q[1] -= 100; q[3] -= 100; q[5] -= 100; q[7] -= 100;
  CHECK(annot_markup(d, 0, AN_UNDERLINE, q, 1, 0x0000FF, "Tester", NULL));
  q[1] -= 40; q[3] -= 40; q[5] -= 40; q[7] -= 40;
  CHECK(annot_markup(d, 0, AN_STRIKE, q, 1, 0xFF0000, "Tester", NULL));
  q[1] -= 40; q[3] -= 40; q[5] -= 40; q[7] -= 40;
  CHECK(annot_markup(d, 0, AN_SQUIGGLY, q, 1, 0x00A000, "Tester", NULL));
  CHECK(annot_note(d, 0, 520, 760, 0xFFD700, "Tester", "A note: caf\xc3\xa9 \xce\xb1"));
  CHECK(annot_freetext(d, 0, rect, "Folio text box, long enough to wrap onto a second line inside the box.\nAnd a new line.", 14, 0xC00000, "Tester"));
  CHECK(annot_ink(d, 0, pts, npts, 2, 2.5, 0x8000FF, "Tester"));
  CHECK(annot_shape(d, 0, AN_SQUARE, sq, 2, 0x0080FF, "Tester"));
  CHECK(annot_shape(d, 0, AN_CIRCLE, sq, 1.5, 0xFF8000, "Tester"));
  CHECK(annot_shape(d, 0, AN_LINE, ln, 3, 0x404040, "Tester"));
  n = pdf_annots(d, 0, &an);
  CHECK(n >= 10);
  for (i = 0; i < n; i++) printf("annot %d kind %d color %06X '%s' by %s at %s\n", an[i].index, an[i].kind, an[i].color & 0xFFFFFF, an[i].contents ? an[i].contents : "", an[i].author ? an[i].author : "-", an[i].date ? an[i].date : "-");
  /* the free text box's text changed; the circle deleted */
  for (i = 0; i < n; i++) if (an[i].kind == AN_FREETEXT) CHECK(annot_set_text(d, 0, an[i].index, "Changed text"));
  for (i = n - 1; i >= 0; i--) if (an[i].kind == AN_CIRCLE) { CHECK(annot_delete(d, 0, an[i].index)); break; }
  pdf_annots_free(an, n);
  /* pages */
  pdf_rotate_page(d, 0, 1);
  CHECK(pdf_insert_blank(d, 1, 300, 400));
  CHECK(pdf_insert_pages(d, d->npages, o, 0, other_pages - 1));
  CHECK(d->npages == n0 + 1 + other_pages);
  CHECK(pdf_move_page(d, d->npages - 1, 0));       /* the other's last page first */
  if (n0 > 1) CHECK(pdf_delete_page(d, 2 + 1));    /* (base page 2, now at index 3) */
  pdf_set_info(d, "Title", "Folio edit test \xe2\x80\x93 \xc3\xa9");
  pdf_set_info(d, "Author", "Folio");
  CHECK(d->changed);
  n = d->npages;
  out = pdf_save(d, NULL, 0, &len);
  CHECK(out != NULL);
  if (!out) return 1;
  CHECK(file_write(argv[3], out, len));
  /* read back */
  {
    char err[160];
    r = pdf_open(out, len, err, sizeof err);
    CHECK(r != NULL);
    if (r) {
      int rot;
      double box[4];
      CHECK(!r->repaired);
      CHECK(r->npages == n);
      pdf_page_box(r, 1, box, &rot);
      CHECK(rot == 90);
      pdf_page_box(r, 2, box, &rot);
      CHECK((int)box[2] == 300 && (int)box[3] == 400);
      i = pdf_annots(r, 1, &an);
      CHECK(i == 9);
      {
        int k, ft = 0;
        for (k = 0; k < i; k++) if (an[k].kind == AN_FREETEXT) { ft = 1; CHECK(an[k].contents && !strcmp(an[k].contents, "Changed text")); CHECK((an[k].color & 0xFFFFFF) == 0xC00000); }
        CHECK(ft);
        for (k = 0; k < i; k++) if (an[k].kind == AN_NOTE) CHECK(an[k].contents && !strcmp(an[k].contents, "A note: caf\xc3\xa9 \xce\xb1"));
      }
      pdf_annots_free(an, i);
      s = pdf_info(r, "Title");
      CHECK(s && !strcmp(s, "Folio edit test \xe2\x80\x93 \xc3\xa9"));
      free(s);
      /* an extract: pages 2 and 1 of the saved file, saved again */
      {
        int order[2] = { 1, 0 };
        size_t l2;
        u8 *o2 = pdf_save(r, order, 2, &l2);
        CHECK(o2 != NULL);
        if (o2) {
          char p2[512];
          PDoc *r2;
          snprintf(p2, sizeof p2, "%s.extract.pdf", argv[3]);
          file_write(p2, o2, l2);
          r2 = pdf_open(o2, l2, err, sizeof err);
          CHECK(r2 && r2->npages == 2);
          if (r2) pdf_close(r2);
          free(o2);
        }
      }
      pdf_close(r);
    }
  }
  free(out);
  pdf_close(o);
  pdf_close(d);
  printf("%s: %d failures\n", argv[3], fails);
  return fails != 0;
}
