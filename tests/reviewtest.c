/*
 * reviewtest.c - Compare Files (a changed copy compared with the original: the changes found
 * and highlighted), the accessibility checker before and after Automatically Tag PDF and the
 * reading options, alternate text, a signature made here the way signing tools lay it out (its
 * message digest over the byte ranges) found intact and then broken by a change, and Save as PDF/A.
 *
 *   reviewtest outdir
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
#include "forms.h"
#include "crypto.h"
#include "review.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static const char *outdir;

typedef struct { PDoc *d; TextPage t[8]; int done[8]; } TCache;
static TextPage *get(void *ud, int page)
{
  TCache *c = (TCache *)ud;
  RCache *rc;
  RenderOpts o;
  Pixmap pm;
  if (page >= 8) return NULL;
  if (c->done[page]) return &c->t[page];
  memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
  textpage_init(&c->t[page]);
  o.scale = 1; o.text_only = 1; o.text = &c->t[page];
  rc = rcache_new(c->d);
  pdf_render(c->d, rc, page, &o, &pm);
  rcache_free(rc);
  free(pm.rgb);
  textpage_finish(&c->t[page]);
  c->done[page] = 1;
  return &c->t[page];
}
static void tc_free(TCache *c) { int i; for (i = 0; i < 8; i++) if (c->done[i]) textpage_free(&c->t[i]); memset(c, 0, sizeof *c); }
static PDoc *make(int changed)
{
  PDoc *d = doc_new_blank(595, 842, 2);
  page_add_text(d, 0, 72, 780, "Contract terms", 18, 0, 1);
  page_add_text(d, 0, 72, 740, "The buyer pays one hundred pounds.\nDelivery is in May.\nThe seller keeps the receipt.", 11, 0, 0);
  page_add_text(d, 1, 72, 780, changed ? "Signed in London.\nA clause added later." : "Signed in Paris.", 11, 0, 0);
  if (changed) page_add_text(d, 0, 72, 640, "Payment within thirty days.", 11, 0, 0);
  return d;
}
static PDoc *reopen_mem(u8 *out, size_t len)
{
  char err[160];
  PDoc *r = pdf_open(out, len, err, sizeof err);
  CHECK(r && !r->repaired);
  return r;
}
int main(int argc, char **argv)
{
  PDoc *a, *b, *r;
  TCache ca, cb;
  Diff df;
  int n, i;
  char *rep;
  if (argc < 2) { fprintf(stderr, "usage: reviewtest outdir\n"); return 2; }
  outdir = argv[1];
  /* ---- compare */
  a = make(0); b = make(1);
  memset(&ca, 0, sizeof ca); memset(&cb, 0, sizeof cb);
  ca.d = a; cb.d = b;
  n = compare_docs(a, get, &ca, b, get, &cb, 1, &df);
  rep = diff_report(&df);
  printf("%s", rep ? rep : "");
  CHECK(n == 4);
  CHECK(rep && strstr(rep, "+ p.1: Payment within thirty days.") && strstr(rep, "- p.2: Signed in Paris.") && strstr(rep, "+ p.2: Signed in London.") && strstr(rep, "+ p.2: A clause added later."));
  free(rep);
  diff_free(&df);
  {
    PAnnot *an;
    int k = pdf_annots(b, 1, &an), hl = 0, notes = 0, j;
    for (j = 0; j < k; j++) { if (an[j].kind == AN_HIGHLIGHT) hl++; if (an[j].kind == AN_NOTE) notes++; }
    CHECK(hl == 2 && notes == 1);
    pdf_annots_free(an, k);
  }
  tc_free(&ca); tc_free(&cb);
  pdf_close(b);
  /* ---- accessibility */
  {
    A11yItem *it;
    int k, fails0 = 0, fails1 = 0, *pg;
    char **alts;
    double r1[4] = { 300, 600, 500, 620 };
    CHECK(form_add_field(a, 0, FF_TEXT, r1, "name", NULL, 0, 0));
    pdf_set_info(a, "Title", "Contract");
    memset(&ca, 0, sizeof ca); ca.d = a;
    k = a11y_check(a, get, &ca, &it);
    for (i = 0; i < k; i++) if (it[i].status == A11Y_FAIL) fails0++;
    rep = a11y_report(it, k);
    printf("before:\n%s", rep ? rep : "");
    free(rep); free(it);
    CHECK(fails0 >= 4);
    CHECK(a11y_untagged(a, &pg) == 2); free(pg);
    CHECK(a11y_autotag(a, 1) == 2);
    CHECK(a11y_untagged(a, &pg) == 0); free(pg);
    CHECK(a11y_reading_options(a, "en-GB", 1));
    tc_free(&ca); ca.d = a;
    k = a11y_check(a, get, &ca, &it);
    for (i = 0; i < k; i++) if (it[i].status == A11Y_FAIL) { fails1++; printf("still failing: %s - %s\n", it[i].rule, it[i].detail); }
    free(it);
    CHECK(fails1 == 1);                                     /* (the form field has no description) */
    rep = a11y_tags(a);
    CHECK(rep && strstr(rep, "Document\n  P\n  Form\n  P\n"));
    free(rep);
    CHECK(a11y_figures(a, &alts) == 0); free(alts);
    tc_free(&ca);
    {
      size_t len;
      u8 *out = pdf_save(a, NULL, 0, &len);
      char p[600];
      snprintf(p, sizeof p, "%s/review_tagged.pdf", outdir);
      file_write(p, out, len);
      r = reopen_mem(out, len);
      if (r) { CHECK(a11y_untagged(r, &pg) == 0); free(pg); { char *t = a11y_tags(r); CHECK(t && strstr(t, "Form")); free(t); } pdf_close(r); }
      free(out);
    }
  }
  /* ---- a signature laid out as signing tools do: ByteRange over all but Contents, a CMS blob
     holding the messageDigest attribute of those bytes */
  {
    double sr[4] = { 72, 100, 272, 150 };
    FForm f;
    PObj *v, *br, *ct;
    size_t len, k;
    u8 *out;
    char contents[400];
    long cstart = -1, cend = -1;
    SigInfo *si;
    CHECK(form_add_field(a, 1, FF_SIG, sr, "Signature1", NULL, 0, 0));
    form_load(a, &f);
    for (i = 0; i < f.n; i++) if (f.f[i].type == FF_SIG) {
      v = pobj_new(PO_DICT);
      pobj_dict_set(v, "Type", pobj_name("Sig"));
      pobj_dict_set(v, "Filter", pobj_name("Adobe.PPKLite"));
      pobj_dict_set(v, "Name", pobj_string("A. Tester", 9));
      pobj_dict_set(v, "Reason", pobj_string("Approval", 8));
      br = pobj_new(PO_ARRAY);
      for (k = 0; k < 4; k++) pobj_array_add(br, pobj_int(1000000000L));
      pobj_dict_set(v, "ByteRange", br);
      memset(contents, 0, sizeof contents);
      ct = pobj_string(contents, 200);
      pobj_dict_set(v, "Contents", ct);
      pobj_dict_set(f.f[i].dict, "V", om_ref(pdf_new_object(a, v)));
    }
    form_free(&f);
    out = pdf_save(a, NULL, 0, &len);
    /* where the Contents hex string is, and the ByteRange to fill in */
    for (k = 0; k + 402 < len; k++) if (out[k] == '<' && !memcmp(out + k + 1, "0000000000000000000000000000000000000000", 40)) { cstart = (long)k; break; }
    CHECK(cstart > 0);
    if (cstart > 0) {
      char brs[60];
      u8 h[32], *br_at = NULL;
      Buf data;
      cend = cstart + 402;
      for (k = 0; k + 44 < len; k++) if (!memcmp(out + k, "[1000000000 1000000000 1000000000 1000000000]", 45)) { br_at = out + k; break; }
      CHECK(br_at != NULL);
      if (br_at) {
        snprintf(brs, sizeof brs, "[0 %ld %ld %ld]", cstart, cend, (long)len - cend);
        memset(br_at, ' ', 45); memcpy(br_at, brs, strlen(brs));
        buf_init(&data);
        buf_add(&data, out, (size_t)cstart); buf_add(&data, out + cend, len - (size_t)cend);
        sha256((const u8 *)data.data, data.len, h);
        buf_free(&data);
        {
          /* a minimal CMS-like blob: ...SEQUENCE { OID messageDigest, SET { OCTET STRING digest } }... */
          u8 blob[200];
          int bl = 0;
          static const u8 oid[11] = { 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x04 };
          blob[bl++] = 0x30; blob[bl++] = 0x80; blob[bl++] = 0x30; blob[bl++] = 47;
          memcpy(blob + bl, oid, 11); bl += 11;
          blob[bl++] = 0x31; blob[bl++] = 34; blob[bl++] = 0x04; blob[bl++] = 32;
          memcpy(blob + bl, h, 32); bl += 32;
          for (k = 0; k < (size_t)bl; k++) { char hx[3]; snprintf(hx, sizeof hx, "%02X", blob[k]); memcpy(out + cstart + 1 + k * 2, hx, 2); }
        }
        {
          char p[600];
          snprintf(p, sizeof p, "%s/review_signed.pdf", outdir);
          file_write(p, out, len);
        }
        r = reopen_mem(out, len);
        if (r) {
          n = sig_list(r, &si);
          CHECK(n == 1 && !strcmp(si[0].name, "A. Tester") && si[0].covers_all && si[0].digest_ok == 1);
          free(si);
          pdf_close(r);
        }
        /* a byte of the signed part changed: the digest no longer matches */
        out[20] ^= 1;
        r = reopen_mem(out, len);
        if (r) { n = sig_list(r, &si); CHECK(n == 1 && si[0].digest_ok == 0); free(si); pdf_close(r); }
        out[20] ^= 1;
        /* something appended after signing */
        {
          u8 *more = (u8 *)malloc(len + 40);
          memcpy(more, out, len); memcpy(more + len, "\n% appended after signing\n", 26);
          r = reopen_mem(more, len + 26);
          if (r) { n = sig_list(r, &si); CHECK(n == 1 && !si[0].covers_all && si[0].digest_ok == 1); free(si); pdf_close(r); }
          free(more);
        }
      }
    }
    free(out);
  }
  /* ---- PDF/A */
  {
    char probs[400];
    size_t len;
    u8 *out;
    CHECK(!pdfa_convert(a, probs, sizeof probs) && strstr(probs, "aren't embedded"));   /* (the standard fonts: Folio has none to embed) */
    printf("PDF/A problems: '%s'\n", probs);
    out = pdf_save(a, NULL, 0, &len);
    r = out ? reopen_mem(out, len) : NULL;
    if (r) {
      PObj *root = pdf_dget(r, r->trailer, "Root"), *md = pdf_dget(r, root, "Metadata"), *oi = pdf_dget(r, root, "OutputIntents");
      size_t ml;
      u8 *x = md ? pdf_stream_data(r, md, &ml) : NULL;
      CHECK(x && strstr((char *)x, "<pdfaid:part>2</pdfaid:part>"));
      CHECK(oi && oi->type == PO_ARRAY && oi->u.arr.n == 1);
      free(x);
      pdf_close(r);
      { char p[600]; snprintf(p, sizeof p, "%s/review_pdfa.pdf", outdir); file_write(p, out, len); }
    }
    free(out);
  }
  pdf_close(a);
  printf("reviewtest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
