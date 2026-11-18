/*
 * pki.h - the public-key parts of digital signatures, offline: DER (ASN.1) reading and writing,
 * big numbers, RSA (PKCS #1 v1.5) and ECDSA (P-256, P-384) signatures made and checked, X.509
 * certificates read, CMS SignedData (PKCS #7) checked and made, and the signer's key and
 * certificates read from a PKCS #12 (.p12/.pfx) or PEM file. Nothing goes to the network: trust
 * means "signed by a certificate you gave", no revocation lists or OCSP.
 * Portable C99 (the native tests and vbcc for the Amiga's 68040).
 */
#ifndef FOLIO_PKI_H
#define FOLIO_PKI_H
#include "util.h"
#include "crypto.h"

/* ------------------------------------------------------------------ DER */
typedef struct { const u8 *p; size_t n; } Der;
/* the next element of in (tag, contents, and the whole element with its header); 0 at the end or
   on bad data. Indefinite lengths (BER) are followed to their end-of-contents */
int der_next(Der *in, int *tag, Der *content, Der *whole);
/* the contents of in's first element if its tag is tag (else 0) */
int der_expect(Der *in, int tag, Der *content);

/* ------------------------------------------------------------------ keys */
enum { PK_NONE, PK_RSA, PK_EC };
enum { EC_NONE, EC_P256, EC_P384 };
typedef struct {
  int type;                         /* PK_ */
  int curve;                        /* EC_ */
  u8 n[520]; int nlen;              /* RSA modulus, or EC point x||y (each coordinate's length) */
  u8 e[8]; int elen;                /* RSA public exponent */
  u8 y[72]; int ylen;               /* EC: y */
} PubKey;
typedef struct {
  PubKey pub;
  u8 d[520]; int dlen;              /* RSA private exponent, EC private scalar */
  u8 p[264], q[264], dp[264], dq[264], qi[264]; int plen;   /* RSA CRT (plen 0: not given) */
} PrivKey;
/* a signature over hash (of hash_len(alg) bytes) checked: 1 good, 0 bad, -1 unsupported.
   RSA: sig is the PKCS #1 v1.5 signature; EC: sig is the DER SEQUENCE { r, s } */
int pk_verify(const PubKey *k, int alg, const u8 *hash, const u8 *sig, size_t siglen);
/* a signature made (into sig, cap bytes): its length, 0 on failure */
int pk_sign(const PrivKey *k, int alg, const u8 *hash, u8 *sig, int cap);

/* ------------------------------------------------------------------ certificates */
typedef struct {
  Der der, tbs, sigalg_der, sig;    /* (pointing into the caller's data) */
  Der serial, issuer, subject, ski;
  char subject_cn[96], issuer_cn[96], org[96], email[96];
  char not_before[16], not_after[16];   /* YYYYMMDDHHMMSS */
  int sig_hash; int sig_pk;         /* how the issuer signed it (HASH_, PK_) */
  PubKey key;
  int is_ca;
} Cert;
/* a certificate (DER) read: 1 ok */
int cert_parse(const u8 *der, size_t n, Cert *c);
/* whether issuer's key signed c: 1 yes, 0 no, -1 can't tell */
int cert_signed_by(const Cert *c, const Cert *issuer);

/* ------------------------------------------------------------------ CMS signatures */
enum { CHAIN_NONE, CHAIN_SELF, CHAIN_INCOMPLETE, CHAIN_BAD, CHAIN_TRUSTED };
typedef struct {
  int ok;                           /* the signature checks: 1 good, 0 bad, -1 couldn't be checked */
  int digest_ok;                    /* messageDigest (or the encapsulated digest) matches the data: 1, 0, -1 */
  int hash;                         /* HASH_ the signer used */
  int pk;                           /* PK_ */
  char signer[96], issuer[96], org[96];
  char signed_at[16];               /* signingTime, if given (YYYYMMDDHHMMSS) */
  char not_before[16], not_after[16];
  int time_ok;                      /* the signing time (or the time given) within the certificate's: 1, 0, -1 not known */
  int chain;                        /* CHAIN_ */
  int chain_len;
  char why[120];                    /* what couldn't be checked, if anything (English) */
} CmsResult;
/* a detached CMS SignedData (sig, siglen; trailing zeros allowed) checked against data. trusted:
   certificates (DER) the user trusts, ntrusted of them (may be 0). now: YYYYMMDDHHMMSS used when
   the signature has no signing time (NULL: not checked) */
int cms_verify(const u8 *sig, size_t siglen, const u8 *data, size_t dlen, const u8 *const *trusted, const size_t *tlen, int ntrusted,
               const char *now, CmsResult *r);
/* a detached CMS SignedData made: the signer's key and certificate chain (certs[0] the signer's),
   hash HASH_SHA256 normally, signing time (YYYYMMDDHHMMSS, may be NULL). malloc'd, *outlen; NULL on failure */
u8 *cms_sign(const PrivKey *k, const u8 *const *certs, const size_t *clen, int ncerts, int hash, const u8 *data, size_t dlen,
             const char *when, size_t *outlen);

/* ------------------------------------------------------------------ the signer's file */
typedef struct {
  PrivKey key;
  u8 *certs[8]; size_t clen[8]; int ncerts;   /* certs[0]: the one for the key (malloc'd copies) */
  char err[120];
} SignerId;
/* a PKCS #12 file (password) or PEM (a private key, unencrypted or PBES2-encrypted, and certificates)
   read: 1 ok, else 0 with err set (English) */
int signer_load(const u8 *data, size_t n, const char *password, SignerId *s);
void signer_free(SignerId *s);
#endif
