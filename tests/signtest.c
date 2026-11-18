/*
 * signtest.c - signing and checking signatures: the big-number arithmetic, RSA and ECDSA, X.509
 * and CMS (core/pki.c) against OpenSSL, the signer's key and certificates read from PKCS #12 and
 * PEM files, a PDF signed (visible and invisible, approval and certification) and checked, saved
 * again incrementally (the signature still valid, and a second one added), pages deleted in an
 * incremental update, and a certificate chain trusted.
 *
 * The keys and certificates are made here, in outdir, with the openssl command when it is installed
 * (nothing of the kind is kept in the repository); without it only the parts needing none run.
 *
 *   signtest outdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "pdfwrite.h"
#include "pdfedit.h"
#include "pagetools.h"
#include "review.h"
#include "pki.h"
#include "sign.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static const char *T;
static char cmd[2000];

static u8 *slurp(const char *name, size_t *n)
{
  char p[600];
  FILE *f;
  long l;
  u8 *b;
  snprintf(p, sizeof p, "%s/%s", T, name);
  if (!(f = fopen(p, "rb"))) return NULL;
  fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
  b = (u8 *)malloc((size_t)l + 1);
  if (b && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); b = NULL; }
  fclose(f);
  *n = (size_t)l;
  return b;
}
static void spill(const char *name, const u8 *d, size_t n)
{
  char p[600];
  FILE *f;
  snprintf(p, sizeof p, "%s/%s", T, name);
  if ((f = fopen(p, "wb"))) { fwrite(d, 1, n, f); fclose(f); }
}
static int sh(const char *fmt, ...)
{
  /* (every command run in outdir, its output thrown away) */
  return 0;
}
#define RUN(...) (snprintf(cmd, sizeof cmd, "cd '%s' && ", T), snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd), __VA_ARGS__), \
                  strncat(cmd, " >/dev/null 2>&1", sizeof cmd - strlen(cmd) - 1), system(cmd))
static PDoc *reopen(const u8 *b, size_t n)
{
  char err[160];
  PDoc *d = pdf_open(b, n, err, sizeof err);
  if (d) pdf_ensure_pages(d);
  return d;
}
static SignerId load(const char *file, const char *pw, int *ok)
{
  SignerId s;
  size_t n = 0;
  u8 *b = slurp(file, &n);
  memset(&s, 0, sizeof s);
  *ok = b && signer_load(b, n, pw, &s);
  if (b && !*ok) fprintf(stderr, "  %s: %s\n", file, s.err);
  free(b);
  return s;
}
static PDoc *a_document(void)
{
  PDoc *d = doc_new_blank(595, 842, 3), *r;
  size_t n;
  u8 *b;
  page_add_text(d, 0, 72, 760, "An agreement", 18, 0, 1);
  page_add_text(d, 0, 72, 720, "The parties agree to everything written here.", 11, 0, 0);
  page_add_text(d, 1, 72, 760, "Page two.", 11, 0, 0);
  page_add_text(d, 2, 72, 760, "Page three.", 11, 0, 0);
  b = pdf_save(d, NULL, 0, &n);
  pdf_close(d);
  r = reopen(b, n);
  free(b);
  return r;
}

int main(int argc, char **argv)
{
  int have_ssl, ok;
  (void)sh;
  if (argc < 2) { fprintf(stderr, "signtest outdir\n"); return 2; }
  T = argv[1];
  /* ------------------------------------------------------------ hashes */
  {
    u8 h[64];
    static const char *const hex = "a9993e364706816aba3e25717850c26c9cd0d89d";
    char s[48];
    int i;
    sha1((const u8 *)"abc", 3, h);
    for (i = 0; i < 20; i++) sprintf(s + 2 * i, "%02x", h[i]);
    CHECK(!strcmp(s, hex));
    /* RFC 4231 test 2 */
    hmac(HASH_SHA256, (const u8 *)"Jefe", 4, (const u8 *)"what do ya want for nothing?", 28, h);
    for (i = 0; i < 4; i++) sprintf(s + 2 * i, "%02x", h[i]);
    CHECK(!memcmp(s, "5bdcc146", 8));
    /* RFC 6070 (PBKDF2-HMAC-SHA1, 2 iterations) */
    pbkdf2(HASH_SHA1, (const u8 *)"password", 8, (const u8 *)"salt", 4, 2, h, 20);
    for (i = 0; i < 4; i++) sprintf(s + 2 * i, "%02x", h[i]);
    CHECK(!memcmp(s, "ea6c014d", 8));
  }
  have_ssl = RUN("openssl version") == 0;
  if (!have_ssl) { printf("signtest: %d checks, %d failed (no openssl here: keys not made, signing skipped)\n", checks, fails); return fails != 0; }
  /* ------------------------------------------------------------ keys and certificates, made now */
  CHECK(RUN("openssl req -x509 -newkey rsa:2048 -nodes -keyout rsa_key.pem -out rsa_cert.pem -subj '/CN=Folio Test RSA/O=Folio Tests' -days 30") == 0);
  CHECK(RUN("openssl pkcs12 -export -inkey rsa_key.pem -in rsa_cert.pem -out rsa.p12 -passout pass:secret") == 0);
  CHECK(RUN("openssl pkcs8 -topk8 -in rsa_key.pem -out rsa_enc.pem -v2 aes-256-cbc -passout pass:secret") == 0);
  CHECK(RUN("(cat rsa_enc.pem rsa_cert.pem > rsa_both.pem)") == 0);
  CHECK(RUN("openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout ca_key.pem -out ca_cert.pem -subj '/CN=Folio Test CA' -days 30 -addext basicConstraints=critical,CA:TRUE") == 0);
  CHECK(RUN("openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout leaf_key.pem -out leaf.csr -subj '/CN=Folio Test Signer'") == 0);
  CHECK(RUN("openssl x509 -req -in leaf.csr -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out leaf_cert.pem -days 10") == 0);
  CHECK(RUN("openssl pkcs12 -export -inkey leaf_key.pem -in leaf_cert.pem -certfile ca_cert.pem -out leaf.p12 -passout pass:pw") == 0);
  CHECK(RUN("openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-384 -nodes -keyout p384_key.pem -out p384_cert.pem -subj '/CN=Folio Test P-384' -days 30") == 0);
  CHECK(RUN("(cat p384_key.pem p384_cert.pem > p384_both.pem)") == 0);
  CHECK(RUN("openssl x509 -in ca_cert.pem -outform DER -out ca_cert.der") == 0);
  {
    SignerId rsa, rsa2, leaf, p384, bad;
    int ok2, ok3, ok4, ok5;
    const char *msg = "Folio signs these bytes.";
    rsa = load("rsa.p12", "secret", &ok);
    CHECK(ok && rsa.key.pub.type == PK_RSA && rsa.key.plen > 0 && rsa.ncerts == 1);
    bad = load("rsa.p12", "wrong", &ok2);
    CHECK(!ok2);
    signer_free(&bad);
    rsa2 = load("rsa_both.pem", "secret", &ok3);
    CHECK(ok3 && rsa2.key.pub.type == PK_RSA && rsa2.ncerts == 1 && rsa2.key.pub.nlen == rsa.key.pub.nlen && !memcmp(rsa2.key.pub.n, rsa.key.pub.n, (size_t)rsa.key.pub.nlen));
    leaf = load("leaf.p12", "pw", &ok4);
    CHECK(ok4 && leaf.key.pub.type == PK_EC && leaf.key.pub.curve == EC_P256 && leaf.ncerts == 2);
    p384 = load("p384_both.pem", "", &ok5);
    CHECK(ok5 && p384.key.pub.curve == EC_P384);
    /* ------------------------------------------------------------ CMS made here, checked here and by openssl */
    {
      SignerId *ids[3];
      const char *names[3] = { "rsa", "leaf", "p384" };
      int i;
      ids[0] = &rsa; ids[1] = &leaf; ids[2] = &p384;
      for (i = 0; i < 3; i++) {
        size_t n;
        u8 *s;
        CmsResult r;
        char f[40];
        if (!ids[i]->ncerts) continue;
        s = cms_sign(&ids[i]->key, (const u8 *const *)ids[i]->certs, ids[i]->clen, ids[i]->ncerts, i == 2 ? HASH_SHA384 : HASH_SHA256, (const u8 *)msg, strlen(msg), "20261118120000", &n);
        CHECK(s != NULL);
        if (!s) continue;
        CHECK(cms_verify(s, n, (const u8 *)msg, strlen(msg), NULL, NULL, 0, NULL, &r) && r.ok == 1 && r.digest_ok == 1);
        CHECK(!strcmp(r.signed_at, "20261118120000") && r.time_ok == 1);
        CHECK(i == 1 ? r.chain == CHAIN_INCOMPLETE || r.chain == CHAIN_SELF || r.chain_len == 2 : r.chain == CHAIN_SELF);
        CHECK(cms_verify(s, n, (const u8 *)"Folio signs those bytes.", strlen(msg), NULL, NULL, 0, NULL, &r) && r.digest_ok == 0);
        snprintf(f, sizeof f, "%s.cms", names[i]); spill(f, s, n);
        spill("msg.bin", (const u8 *)msg, strlen(msg));
        CHECK(RUN("openssl cms -verify -binary -inform DER -in %s.cms -content msg.bin -noverify -out /dev/null", names[i]) == 0);
        free(s);
      }
      /* the leaf's chain trusted through the CA */
      {
        size_t n, cl;
        u8 *s = cms_sign(&leaf.key, (const u8 *const *)leaf.certs, leaf.clen, leaf.ncerts, HASH_SHA256, (const u8 *)msg, strlen(msg), NULL, &n), *ca = slurp("ca_cert.der", &cl);
        CmsResult r;
        const u8 *tc[1];
        tc[0] = ca;
        CHECK(s && ca && cms_verify(s, n, (const u8 *)msg, strlen(msg), tc, &cl, 1, NULL, &r) && r.ok == 1 && r.chain == CHAIN_TRUSTED && r.chain_len == 2);
        CHECK(!strcmp(r.signer, "Folio Test Signer") && !strcmp(r.issuer, "Folio Test CA"));
        /* only the signer's certificate in the signature: the CA found among those trusted */
        free(s);
        s = cms_sign(&leaf.key, (const u8 *const *)leaf.certs, leaf.clen, 1, HASH_SHA256, (const u8 *)msg, strlen(msg), NULL, &n);
        CHECK(s && cms_verify(s, n, (const u8 *)msg, strlen(msg), tc, &cl, 1, NULL, &r) && r.ok == 1 && r.chain == CHAIN_TRUSTED);
        CHECK(s && cms_verify(s, n, (const u8 *)msg, strlen(msg), NULL, NULL, 0, NULL, &r) && r.ok == 1 && r.chain == CHAIN_INCOMPLETE);
        free(s); free(ca);
      }
    }
    /* signatures made by openssl, checked here */
    CHECK(RUN("openssl cms -sign -binary -nodetach -in msg.bin -signer rsa_cert.pem -inkey rsa_key.pem -outform DER -out o_rsa_att.cms") == 0);
    CHECK(RUN("openssl cms -sign -binary -in msg.bin -signer rsa_cert.pem -inkey rsa_key.pem -outform DER -out o_rsa.cms -md sha512") == 0);
    CHECK(RUN("openssl cms -sign -binary -in msg.bin -signer leaf_cert.pem -inkey leaf_key.pem -certfile ca_cert.pem -outform DER -out o_leaf.cms") == 0);
    CHECK(RUN("openssl cms -sign -binary -in msg.bin -signer p384_cert.pem -inkey p384_key.pem -outform DER -out o_p384.cms -md sha384") == 0);
    {
      static const char *const fs[3] = { "o_rsa.cms", "o_leaf.cms", "o_p384.cms" };
      int i;
      for (i = 0; i < 3; i++) {
        size_t n;
        u8 *s = slurp(fs[i], &n);
        CmsResult r;
        CHECK(s && cms_verify(s, n, (const u8 *)msg, strlen(msg), NULL, NULL, 0, NULL, &r) && r.ok == 1 && r.digest_ok == 1);
        if (i == 1) CHECK(r.chain == CHAIN_SELF && r.chain_len == 2);
        free(s);
      }
    }
    /* ------------------------------------------------------------ a PDF signed, saved again, signed again */
    {
      PDoc *d = a_document(), *r;
      SignOpts o;
      SigInfo *si;
      size_t n = 0, n2 = 0, n3 = 0;
      u8 *b, *b2 = NULL, *b3 = NULL;
      char err[160];
      int ns;
      memset(&o, 0, sizeof o);
      o.page = 0; o.rect[0] = 72; o.rect[1] = 600; o.rect[2] = 272; o.rect[3] = 660;
      o.reason = "I agree"; o.location = "Amiga"; o.certify = 2; o.when = "20261118100000";
      b = pdf_sign(d, &rsa, &o, &n, err, sizeof err);
      if (!b) fprintf(stderr, "  sign: %s\n", err);
      CHECK(b != NULL);
      pdf_close(d);
      if (b) {
        spill("signed1.pdf", b, n);
        r = reopen(b, n);
        CHECK(r && !r->repaired && r->npages == 3);
        ns = r ? sig_list(r, &si) : 0;
        CHECK(ns == 1);
        if (ns == 1) {
          CHECK(si[0].digest_ok == 1 && si[0].sig_ok == 1 && si[0].covers_all && si[0].certify && si[0].chain == CHAIN_SELF);
          CHECK(!strcmp(si[0].signer, "Folio Test RSA") && !strcmp(si[0].reason, "I agree") && si[0].cert && si[0].time_ok == 1);
          sig_free(si, ns);
        }
        /* a comment added and the file saved incrementally: the signature still holds */
        if (r) {
          long at = 0;
          double q[8] = { 72, 730, 300, 730, 72, 715, 300, 715 };
          annot_markup(r, 0, AN_HIGHLIGHT, q, 1, 0xFFFF00, "Tester", NULL);
          r->changed = 1;
          b2 = pdf_save_incremental(r, &n2, &at);
          CHECK(b2 && n2 > n && at >= (long)n && !memcmp(b2, b, n));
          pdf_close(r);
        }
        if (b2) {
          spill("signed2.pdf", b2, n2);
          r = reopen(b2, n2);
          CHECK(r && !r->repaired && annot_count(r, 0) == 2);
          ns = r ? sig_list(r, &si) : 0;
          CHECK(ns == 1 && si[0].digest_ok == 1 && si[0].sig_ok == 1 && !si[0].covers_all);
          if (ns) sig_free(si, ns);
          /* a second signature (invisible, with the EC key and its CA) */
          if (r) {
            memset(&o, 0, sizeof o);
            o.page = 1;
            b3 = pdf_sign(r, &leaf, &o, &n3, err, sizeof err);
            if (!b3) fprintf(stderr, "  sign 2: %s\n", err);
            CHECK(b3 != NULL && n3 > n2 && !memcmp(b3, b2, n2));
            pdf_close(r);
          }
        }
        if (b3) {
          spill("signed3.pdf", b3, n3);
          r = reopen(b3, n3);
          ns = r ? sig_list(r, &si) : 0;
          CHECK(ns == 2);
          if (ns == 2) {
            CHECK(si[0].digest_ok == 1 && si[0].sig_ok == 1 && !si[0].covers_all);
            CHECK(si[1].digest_ok == 1 && si[1].sig_ok == 1 && si[1].covers_all && si[1].chain == CHAIN_SELF);
            sig_free(si, ns);
          }
          /* trusting the CA */
          {
            size_t cl;
            u8 *ca = slurp("ca_cert.der", &cl);
            const u8 *tc[1];
            tc[0] = ca;
            sig_trust(tc, &cl, 1);
            ns = r ? sig_list(r, &si) : 0;
            CHECK(ns == 2 && si[1].chain == CHAIN_TRUSTED && si[0].chain == CHAIN_SELF);
            if (ns) sig_free(si, ns);
            sig_trust(NULL, NULL, 0);
            free(ca);
          }
          /* changing a byte the first signature covers breaks it (and only it) */
          if (r) pdf_close(r);
          {
            u8 *t = (u8 *)malloc(n3);
            size_t k;
            memcpy(t, b3, n3);
            for (k = 0; k + 12 < n; k++) if (!memcmp(t + k, "parties agree", 13)) { t[k] = 'P'; break; }
            r = reopen(t, n3);
            ns = r ? sig_list(r, &si) : 0;
            CHECK(ns == 2 && si[0].digest_ok == 0 && si[1].digest_ok == 0);
            if (ns) sig_free(si, ns);
            if (r) pdf_close(r);
            free(t);
          }
        }
        if (RUN("command -v qpdf") == 0) {
          CHECK(RUN("qpdf --check signed1.pdf") == 0);
          CHECK(RUN("qpdf --check signed2.pdf") == 0);
          CHECK(RUN("qpdf --check signed3.pdf") == 0);
        }
        if (RUN("command -v pdfsig") == 0) {
          CHECK(RUN("pdfsig signed3.pdf | grep -c 'Signature is Valid' | grep -q 2") == 0);
        }
      }
      free(b); free(b2); free(b3);
    }
    /* ------------------------------------------------------------ incremental updates of pages */
    {
      PDoc *d = a_document(), *r;
      size_t n;
      u8 *b;
      CHECK(d && pdf_can_save_incremental(d));
      if (d) {
        pdf_delete_page(d, 1);
        d->changed = 1;
        b = pdf_save_incremental(d, &n, NULL);
        CHECK(b != NULL);
        r = b ? reopen(b, n) : NULL;
        CHECK(r && !r->repaired && r->npages == 2);
        if (r) pdf_close(r);
        if (b) { spill("incr_pages.pdf", b, n); if (RUN("command -v qpdf") == 0) CHECK(RUN("qpdf --check incr_pages.pdf") == 0); }
        free(b);
        pdf_close(d);
      }
    }
    signer_free(&rsa); signer_free(&rsa2); signer_free(&leaf); signer_free(&p384);
  }
  printf("signtest: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
