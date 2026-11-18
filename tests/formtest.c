/*
 * formtest.c - forms: the fields of a generated form read, filled in (text, a box of several lines,
 * a check box, a radio button, a drop-down and a list box), saved and read back; the appearances
 * drawn (the page renders differently once filled); exported as XFDF and FDF and imported into a
 * fresh copy; cleared; flattened; fields added (every kind, a table), renamed and put in tab order;
 * blanks on a page without fields found by Detect.
 *
 *   formtest forms.pdf outdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "pdfwrite.h"
#include "render.h"
#include "forms.h"
#include "font.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static PDoc *open_mem(const u8 *data, size_t len)
{
  char err[160];
  PDoc *d = pdf_open(data, len, err, sizeof err);
  if (!d) fprintf(stderr, "open: %s\n", err);
  return d;
}
static PDoc *reopen(PDoc *d, const char *path)
{
  size_t len;
  u8 *out = pdf_save(d, NULL, 0, &len);
  PDoc *r;
  CHECK(out != NULL);
  if (!out) return NULL;
  if (path) file_write(path, out, len);
  r = open_mem(out, len);
  free(out);
  CHECK(r && !r->repaired);
  return r;
}
static Pixmap draw(PDoc *d, int page)
{
  /* (twice the size, so text has dark pixels) */
  RCache *c = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  o.scale = 2; o.annots = 1; o.paper = 0xFFFFFF;
  pdf_render(d, c, page, &o, &pm);
  rcache_free(c);
  return pm;
}
static long dark(Pixmap pm)
{
  long n = 0, i;
  for (i = 0; pm.rgb && i < (long)pm.w * pm.h; i++) if (pm.rgb[i * 3] + pm.rgb[i * 3 + 1] + pm.rgb[i * 3 + 2] < 300) n++;
  return n;
}
static int have_fonts;
/* the normal appearance's content of a field's first widget contains text */
static int ap_has(PDoc *d, FField *f, const char *text)
{
  PObj *ap = pdf_dget(d, f->w[0].dict, "AP"), *n = ap ? pdf_dget(d, ap, "N") : NULL;
  size_t len;
  u8 *s;
  int r = 0;
  if (n && n->type == PO_DICT) n = pdf_resolve(d, n->u.dict.v[0]);
  if (!n || n->type != PO_STREAM || !(s = pdf_stream_data(d, n, &len))) return 0;
  {
    size_t i, tl = strlen(text);
    for (i = 0; i + tl <= len; i++) if (!memcmp(s + i, text, tl)) r = 1;
  }
  free(s);
  return r;
}
static FField *field(FForm *f, const char *name)
{
  int i;
  for (i = 0; i < f->n; i++) if (!strcmp(f->f[i].name, name)) return &f->f[i];
  return NULL;
}
static void check_values(PDoc *d, const char *what)
{
  FForm f;
  FField *x;
  form_load(d, &f);
  fprintf(stderr, "(%s)\n", what);
  CHECK((x = field(&f, "name")) && x->value && !strcmp(x->value, "J\xc3\xbcrgen Smith"));
  CHECK((x = field(&f, "notes")) && x->value && !strcmp(x->value, "First line\nsecond line of the notes, long enough to wrap in the box"));
  CHECK((x = field(&f, "agree")) && x->value && strcmp(x->value, "Off"));
  CHECK((x = field(&f, "colour")) && x->value && !strcmp(x->value, "green"));
  CHECK((x = field(&f, "country")) && x->value && !strcmp(x->value, "Italy"));
  CHECK((x = field(&f, "fruit")) && x->value && !strcmp(x->value, "Cherry"));
  form_free(&f);
}
int main(int argc, char **argv)
{
  size_t len;
  u8 *data;
  PDoc *d, *r;
  FForm f;
  FField *x;
  char path[600], *xfdf, *fdf;
  size_t xl, fl;
  long before, after;
  int i, k;
  if (argc < 3) { fprintf(stderr, "usage: formtest forms.pdf outdir\n"); return 2; }
  {
    const char *fd = getenv("FOLIO_FONTS") ? getenv("FOLIO_FONTS") : "/usr/share/fonts/truetype/liberation";
    char fp[600];
    u8 *t;
    snprintf(fp, sizeof fp, "%s/LiberationSans-Regular.ttf", fd);
    if ((t = file_read(fp, &len))) { have_fonts = 1; free(t); font_set_dir(fd); }
  }
  if (!(data = file_read(argv[1], &len)) || !(d = open_mem(data, len))) return 1;
  CHECK(form_load(d, &f) == 6);
  for (i = 0; i < f.n; i++) printf("field %s type %d widgets %d value '%s' opts %d page %d\n", f.f[i].name, f.f[i].type, f.f[i].nw, f.f[i].value ? f.f[i].value : "", f.f[i].nopts, f.f[i].nw ? f.f[i].w[0].page : -1);
  CHECK((x = field(&f, "name")) && x->type == FF_TEXT && x->nw == 1 && x->w[0].page == 0);
  CHECK((x = field(&f, "notes")) && (x->flags & FFL_MULTILINE));
  CHECK((x = field(&f, "agree")) && x->type == FF_CHECK);
  CHECK((x = field(&f, "colour")) && x->type == FF_RADIO && x->nw == 3);
  CHECK((x = field(&f, "country")) && x->type == FF_COMBO && x->nopts == 3);
  CHECK((x = field(&f, "fruit")) && x->type == FF_LIST && x->nopts == 3);
  /* the field under a point */
  x = field(&f, "name");
  k = -1;
  CHECK(x && form_field_at(&f, 0, (x->w[0].rect[0] + x->w[0].rect[2]) / 2, (x->w[0].rect[1] + x->w[0].rect[3]) / 2, &k) == (int)(x - f.f) && k == 0);
  { Pixmap pm = draw(d, 0); before = dark(pm); free(pm.rgb); }
  /* filling in */
  CHECK(form_set_text(d, field(&f, "name"), "J\xc3\xbcrgen Smith"));
  CHECK(form_set_text(d, field(&f, "notes"), "First line\nsecond line of the notes, long enough to wrap in the box"));
  CHECK(form_set_check(d, field(&f, "agree"), 0, 1));
  x = field(&f, "colour");
  for (k = 0; k < x->nw; k++) if (!strcmp(x->w[k].on, "green")) CHECK(form_set_check(d, x, k, 1));
  x = field(&f, "country");
  CHECK(form_set_choice(d, x, 2));
  x = field(&f, "fruit");
  CHECK(form_set_text(d, x, "Cherry"));
  CHECK(!form_set_text(d, field(&f, "agree"), "x"));      /* not a text field */
  CHECK(ap_has(d, field(&f, "name"), "(J\\374rgen Smith) Tj"));
  CHECK(ap_has(d, field(&f, "notes"), "(First line) Tj"));
  CHECK(ap_has(d, field(&f, "country"), "(Italy) Tj"));
  CHECK(ap_has(d, field(&f, "fruit"), "(Cherry) Tj"));
  form_free(&f);
  { Pixmap pm = draw(d, 0); after = dark(pm); free(pm.rgb); }
  printf("dark pixels before %ld after %ld\n", before, after);
  if (have_fonts) CHECK(after > before + 300);             /* the values are drawn */
  check_values(d, "in memory");
  snprintf(path, sizeof path, "%s/form_filled.pdf", argv[2]);
  r = reopen(d, path);
  if (r) { check_values(r, "saved"); { Pixmap pm = draw(r, 0); CHECK(dark(pm) == after); free(pm.rgb); } pdf_close(r); }
  /* export, then import into a fresh copy */
  form_load(d, &f);
  xfdf = form_export(d, &f, 0, "forms.pdf", &xl);
  fdf = form_export(d, &f, 1, "forms.pdf", &fl);
  CHECK(xfdf && strstr(xfdf, "<field name=\"name\"><value>J\xc3\xbcrgen Smith</value></field>"));
  CHECK(fdf && strstr(fdf, "/T (country) /V (Italy)"));
  snprintf(path, sizeof path, "%s/form.xfdf", argv[2]); file_write(path, xfdf, xl);
  snprintf(path, sizeof path, "%s/form.fdf", argv[2]); file_write(path, fdf, fl);
  form_free(&f);
  for (i = 0; i < 2; i++) {
    PDoc *e = open_mem(data, len);
    FForm g;
    form_load(e, &g);
    CHECK(form_import(e, &g, (const u8 *)(i ? fdf : xfdf), i ? fl : xl) == 6);
    form_free(&g);
    check_values(e, i ? "FDF" : "XFDF");
    pdf_close(e);
  }
  CHECK(form_import(d, &f, (const u8 *)"hello", 5) == -1);
  free(xfdf); free(fdf);
  /* clearing */
  {
    PDoc *e = open_mem(data, len);
    FForm g;
    form_load(e, &g);
    form_set_text(e, field(&g, "name"), "x");
    CHECK(form_reset(e, &g) >= 5);
    CHECK(field(&g, "name")->value && !*field(&g, "name")->value);
    CHECK(!strcmp(field(&g, "agree")->value, "Off"));
    form_free(&g);
    pdf_close(e);
  }
  /* flattening: the page looks the same, the form is gone */
  form_load(d, &f);
  CHECK(form_flatten(d, &f) >= 6);
  form_free(&f);
  { Pixmap pm = draw(d, 0); long dk = dark(pm); printf("flattened: %ld\n", dk); CHECK(dk > after - 50 && dk < after + 50); free(pm.rgb); }
  snprintf(path, sizeof path, "%s/form_flat.pdf", argv[2]);
  r = reopen(d, path);
  if (r) { CHECK(form_load(r, &f) == 0); form_free(&f); { Pixmap pm = draw(r, 0); CHECK(dark(pm) > after - 50); free(pm.rgb); } pdf_close(r); }
  pdf_close(d);
  /* preparing a form: every kind added to page 2, a table, a rename, tab order, detection */
  d = open_mem(data, len);
  {
    double r1[4] = { 300, 700, 500, 720 }, r2[4] = { 300, 670, 316, 686 }, r3[4] = { 330, 670, 346, 686 }, r4[4] = { 350, 670, 366, 686 };
    double r5[4] = { 300, 630, 450, 650 }, r6[4] = { 300, 560, 450, 620 }, r7[4] = { 300, 520, 380, 545 }, r8[4] = { 300, 460, 500, 500 };
    double r9[4] = { 300, 300, 540, 400 };
    const char *opts[] = { "One", "Two", "Three" };
    TextPage t;
    RCache *c;
    RenderOpts o;
    Pixmap pm;
    CHECK(form_add_field(d, 1, FF_TEXT, r1, "email", NULL, 0, 0));
    CHECK(form_add_field(d, 1, FF_CHECK, r2, "subscribe", NULL, 0, 0));
    CHECK(form_add_field(d, 1, FF_RADIO, r3, "size", NULL, 0, 0));
    CHECK(form_add_field(d, 1, FF_RADIO, r4, "size", NULL, 0, 0));
    CHECK(form_add_field(d, 1, FF_COMBO, r5, "pick", opts, 3, 0));
    CHECK(form_add_field(d, 1, FF_LIST, r6, "list", opts, 3, 0));
    CHECK(form_add_field(d, 1, FF_PUSH, r7, "Go", NULL, 0, 0));
    CHECK(form_add_field(d, 1, FF_SIG, r8, NULL, NULL, 0, 0));
    CHECK(form_add_field(d, 1, FF_TEXT, r1, "email", NULL, 0, 0));      /* the same name: made unique */
    CHECK(form_add_table(d, 1, r9, 3, 2, "grid"));
    memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
    textpage_init(&t);
    o.scale = 1; o.text_only = 1; o.text = &t;
    c = rcache_new(d);
    pdf_render(d, c, 1, &o, &pm);
    rcache_free(c);
    free(pm.rgb);
    textpage_finish(&t);
    CHECK(form_detect(d, 1, &t) == 2);
    textpage_free(&t);
    form_load(d, &f);
    printf("%d fields after adding\n", f.n);
    CHECK(f.n == 6 + 8 + 6 + 2);
    CHECK((x = field(&f, "size")) && x->type == FF_RADIO && x->nw == 2);
    CHECK(field(&f, "email 2") != NULL);
    CHECK(field(&f, "grid.2.1") != NULL);
    CHECK((x = field(&f, "pick")) && x->type == FF_COMBO && x->nopts == 3);
    CHECK((x = field(&f, "email")) && form_set_props(d, x, "mail", "Your e-mail address", FFL_REQUIRED, 0, 40, 1, 11));
    CHECK(form_set_text(d, field(&f, "mail"), "someone@example.org"));
    CHECK(form_set_choice(d, field(&f, "pick"), 1));
    x = field(&f, "size");
    CHECK(form_set_check(d, x, 1, 1));
    CHECK(form_tab_order(d, 0) > 0);
    { int co[2] = { 0, 1 }; CHECK(form_calc_order(d, &f, co, 2)); }
    CHECK(form_delete_field(d, field(&f, "Go")));
    form_free(&f);
    snprintf(path, sizeof path, "%s/form_made.pdf", argv[2]);
    r = reopen(d, path);
    if (r) {
      form_load(r, &f);
      CHECK(f.n == 6 + 8 + 6 + 2 - 1);
      CHECK((x = field(&f, "mail")) && x->value && !strcmp(x->value, "someone@example.org") && x->maxlen == 40 && (x->flags & FFL_REQUIRED) && x->tooltip);
      CHECK((x = field(&f, "pick")) && x->value && !strcmp(x->value, "Two"));
      CHECK((x = field(&f, "size")) && x->value && !strcmp(x->value, x->w[1].on));
      form_free(&f);
      pdf_close(r);
    }
  }
  pdf_close(d);
  free(data);
  printf("formtest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
