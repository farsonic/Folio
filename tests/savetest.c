/*
 * savetest.c - saving with options: streams compressed, objects packed into object streams,
 * password protection (AES-128 and AES-256, with permissions); each file read back by Folio
 * with and without its password, its pages and text checked against the original.
 *
 *   savetest in.pdf outdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "pdfwrite.h"
#include "render.h"
#include "textpage.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static PDoc *open_mem(const u8 *data, size_t len)
{
  char err[160];
  PDoc *d = pdf_open(data, len, err, sizeof err);
  if (!d) fprintf(stderr, "open: %s\n", err);
  return d;
}
static char *page_text(PDoc *d, int page)
{
  RCache *c = rcache_new(d);
  RenderOpts o;
  Pixmap pm;
  TextPage t;
  char *s;
  memset(&o, 0, sizeof o);
  memset(&pm, 0, sizeof pm);
  o.scale = 1; o.text_only = 1; o.text = &t;
  textpage_init(&t);
  pdf_render(d, c, page, &o, &pm);
  free(pm.rgb);
  textpage_finish(&t);
  s = t.no ? textpage_text(&t, 0, t.no - 1) : str_dup("");
  textpage_free(&t);
  rcache_free(c);
  return s;
}
static void roundtrip(PDoc *d, const char *outdir, const char *name, const SaveOpts *o, const char *pw, const char *ref)
{
  size_t len;
  u8 *out = pdf_save_opts(d, NULL, 0, o, &len);
  char path[600];
  PDoc *r;
  CHECK(out != NULL);
  if (!out) return;
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  CHECK(file_write(path, out, len));
  r = open_mem(out, len);
  CHECK(r != NULL);
  if (r) {
    CHECK(!r->repaired);
    if (pw) {
      CHECK(r->need_password);
      CHECK(!pdf_password(r, "wrong"));
      CHECK(pdf_password(r, pw));
    } else if (r->need_password) CHECK(pdf_password(r, ""));
    CHECK(pdf_ensure_pages(r) == d->npages);
    if (o && o->crypt) CHECK(r->crypt == o->crypt);
    {
      char *t = page_text(r, 0);
      CHECK(t && ref && !strcmp(t, ref));
      free(t);
    }
    {
      char *ti = pdf_info(r, "Title");
      CHECK(ti && !strcmp(ti, "Save test"));
      free(ti);
    }
    pdf_close(r);
  }
  printf("%s: %ld bytes\n", name, (long)len); fflush(stdout);
  free(out);
}
int main(int argc, char **argv)
{
  size_t len;
  u8 *data;
  PDoc *d;
  char *ref;
  SaveOpts o;
  if (argc < 3) { fprintf(stderr, "usage: savetest in.pdf outdir\n"); return 2; }
  if (!(data = file_read(argv[1], &len)) || !(d = open_mem(data, len))) return 1;
  free(data);
  pdf_ensure_pages(d);
  {
    /* (pdf_set_info lives in pdfedit.c; set the title by hand so this test needs only the writer) */
    PObj *info = pobj_new(PO_DICT);
    int n;
    pobj_dict_set(info, "Title", pobj_string("Save test", 9));
    n = pdf_new_object(d, info);
    { PObj *r = pobj_new(PO_REF); r->u.ref.num = n; pobj_dict_set(d->trailer, "Info", r); }
  }
  ref = page_text(d, 0);
  CHECK(ref && strlen(ref) > 10);
  memset(&o, 0, sizeof o);
  roundtrip(d, argv[2], "save_plain.pdf", NULL, NULL, ref);
  o.compress = 1;
  roundtrip(d, argv[2], "save_compress.pdf", &o, NULL, ref);
  o.objstm = 1;
  roundtrip(d, argv[2], "save_objstm.pdf", &o, NULL, ref);
  o.crypt = SAVE_AES256; o.user_pw = "secret"; o.owner_pw = "boss"; o.perms = PERM_PRINT | PERM_PRINTHQ;
  roundtrip(d, argv[2], "save_aes256.pdf", &o, "secret", ref);
  o.objstm = 0;
  roundtrip(d, argv[2], "save_aes256_plain.pdf", &o, "secret", ref);
  o.crypt = SAVE_AES128; o.objstm = 1;
  roundtrip(d, argv[2], "save_aes128.pdf", &o, "secret", ref);
  o.user_pw = ""; o.owner_pw = "boss"; o.perms = PERM_PRINT;
  roundtrip(d, argv[2], "save_aes128_open.pdf", &o, NULL, ref);
  o.crypt = SAVE_AES256;
  roundtrip(d, argv[2], "save_aes256_open.pdf", &o, NULL, ref);
  /* the owner password opens it too */
  {
    u8 *out;
    PDoc *r;
    o.user_pw = "u1"; o.owner_pw = "own";
    out = pdf_save_opts(d, NULL, 0, &o, &len);
    r = out ? open_mem(out, len) : NULL;
    CHECK(r && r->need_password && pdf_password(r, "own"));
    if (r) pdf_close(r);
    free(out);
  }
  free(ref);
  pdf_close(d);
  printf("savetest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
