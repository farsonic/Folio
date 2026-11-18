/*
 * pki.c - DER, big numbers, RSA and ECDSA, X.509, CMS and the signer's key file (see pki.h).
 *
 * Big numbers are little-endian arrays of 32-bit limbs, multiplied the Montgomery way (CIOS); a
 * 2048-bit RSA signature takes a few seconds on a 68040 with the Chinese remainder theorem.
 * Elliptic curve points are kept in Jacobian coordinates in Montgomery form.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pki.h"
#include "pdfwrite.h"

typedef unsigned long long u64;

/* ================================================================== DER */
static int der_next_d(Der *in, int *tag, Der *content, Der *whole, int depth)
{
  const u8 *p = in->p;
  size_t n = in->n, len, hl;
  if (n < 2 || depth > 40) return 0;
  *tag = p[0];
  if ((p[0] & 0x1F) == 0x1F) return 0;               /* (high tag numbers: not in what Folio reads) */
  if (p[1] < 0x80) { len = p[1]; hl = 2; }
  else if (p[1] == 0x80) {
    /* indefinite: the contents run to an end-of-contents at this level */
    Der rest, c, w;
    int t;
    if (!(p[0] & 0x20)) return 0;
    rest.p = p + 2; rest.n = n - 2;
    for (;;) {
      if (rest.n >= 2 && rest.p[0] == 0 && rest.p[1] == 0) break;
      if (!der_next_d(&rest, &t, &c, &w, depth + 1)) return 0;
    }
    content->p = p + 2; content->n = (size_t)(rest.p - (p + 2));
    whole->p = p; whole->n = (size_t)(rest.p + 2 - p);
    in->p = rest.p + 2; in->n = n - whole->n;
    return 1;
  } else {
    int k = p[1] & 0x7F, i;
    if (k > 4 || n < (size_t)(2 + k)) return 0;
    for (len = 0, i = 0; i < k; i++) len = (len << 8) | p[2 + i];
    hl = (size_t)(2 + k);
  }
  if (len > n - hl) return 0;
  content->p = p + hl; content->n = len;
  whole->p = p; whole->n = hl + len;
  in->p = p + hl + len; in->n = n - hl - len;
  return 1;
}
int der_next(Der *in, int *tag, Der *content, Der *whole) { return der_next_d(in, tag, content, whole, 0); }
int der_expect(Der *in, int tag, Der *content)
{
  Der save = *in, w;
  int t;
  if (!der_next(in, &t, content, &w) || t != tag) { *in = save; return 0; }
  return 1;
}
static int der_peek(const Der *in) { return in->n ? in->p[0] : -1; }
/* skip one element */
static int der_skip(Der *in) { Der c, w; int t; return der_next(in, &t, &c, &w); }
static int oid_is(const Der *o, const char *hex)
{
  size_t n = strlen(hex) / 2, i;
  if (o->n != n) return 0;
  for (i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(hex + 2 * i, "%2x", &v) != 1 || o->p[i] != (u8)v) return 0;
  }
  return 1;
}
/* an INTEGER's magnitude (leading zeros dropped) */
static void int_bytes(Der *c)
{
  while (c->n > 1 && c->p[0] == 0) { c->p++; c->n--; }
}
/* an OCTET STRING's bytes: primitive, or constructed (BER chunks joined into buf) */
static int octets(int tag, const Der *c, Buf *buf, Der *out)
{
  if ((tag & 0x20) == 0) { *out = *c; return 1; }
  {
    Der in = *c, cc, w;
    int t;
    while (in.n) {
      if (!der_next(&in, &t, &cc, &w)) return 0;
      if (t & 0x20) { Der o2; if (!octets(t, &cc, buf, &o2)) return 0; }
      else buf_add(buf, cc.p, cc.n);
    }
    if (buf->oom) return 0;
    out->p = (const u8 *)buf->data; out->n = buf->len;
    return 1;
  }
}

/* DER writing */
static void dput_hdr(Buf *b, int tag, size_t n)
{
  buf_addc(b, tag);
  if (n < 0x80) buf_addc(b, (int)n);
  else if (n < 0x100) { buf_addc(b, 0x81); buf_addc(b, (int)n); }
  else if (n < 0x10000) { buf_addc(b, 0x82); buf_addc(b, (int)(n >> 8)); buf_addc(b, (int)(n & 255)); }
  else { buf_addc(b, 0x83); buf_addc(b, (int)(n >> 16)); buf_addc(b, (int)((n >> 8) & 255)); buf_addc(b, (int)(n & 255)); }
}
static void dput(Buf *b, int tag, const void *p, size_t n) { dput_hdr(b, tag, n); if (n) buf_add(b, p, n); }
static void dwrap(Buf *b, int tag, Buf *inner) { dput(b, tag, inner->data, inner->len); if (inner->oom) b->oom = 1; buf_free(inner); buf_init(inner); }
static void dput_oid(Buf *b, const char *hex)
{
  u8 t[32];
  size_t n = strlen(hex) / 2, i;
  for (i = 0; i < n && i < sizeof t; i++) { unsigned v = 0; sscanf(hex + 2 * i, "%2x", &v); t[i] = (u8)v; }
  dput(b, 0x06, t, n);
}
static void dput_uint(Buf *b, const u8 *p, size_t n)
{
  while (n > 1 && p[0] == 0) { p++; n--; }
  if (n && (p[0] & 0x80)) { dput_hdr(b, 0x02, n + 1); buf_addc(b, 0); buf_add(b, p, n); }
  else dput(b, 0x02, p, n);
}

/* ================================================================== big numbers */
#define BN_LIMBS 140
typedef struct { u32 v[BN_LIMBS]; } BN;
typedef struct { u32 m[BN_LIMBS]; u32 rr[BN_LIMBS]; u32 one[BN_LIMBS]; u32 minv; int n; } Mont;

static void bn_from(u32 *r, int n, const u8 *p, size_t len)
{
  int i;
  memset(r, 0, (size_t)n * sizeof(u32));
  for (i = 0; (size_t)i < len && i < n * 4; i++) r[i / 4] |= (u32)p[len - 1 - (size_t)i] << (8 * (i % 4));
}
static void bn_to(const u32 *a, int n, u8 *p, size_t len)
{
  size_t i;
  for (i = 0; i < len; i++) { size_t k = len - 1 - i; p[k] = (int)(i / 4) < n ? (u8)(a[i / 4] >> (8 * (i % 4))) : 0; }
}
static int bn_cmp(const u32 *a, const u32 *b, int n)
{
  int i;
  for (i = n - 1; i >= 0; i--) if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
  return 0;
}
static int bn_zero(const u32 *a, int n) { int i; for (i = 0; i < n; i++) if (a[i]) return 0; return 1; }
static u32 bn_sub(u32 *r, const u32 *a, const u32 *b, int n)
{
  u64 br = 0;
  int i;
  for (i = 0; i < n; i++) { u64 t = (u64)a[i] - b[i] - br; r[i] = (u32)t; br = (t >> 32) & 1; }
  return (u32)br;
}
static u32 bn_add(u32 *r, const u32 *a, const u32 *b, int n)
{
  u64 c = 0;
  int i;
  for (i = 0; i < n; i++) { c += (u64)a[i] + b[i]; r[i] = (u32)c; c >>= 32; }
  return (u32)c;
}
/* r = bytes p (any length) mod m (n limbs), bit by bit */
static void bn_mod_bytes(u32 *r, const u8 *p, size_t len, const u32 *m, int n)
{
  size_t i;
  int b, k;
  memset(r, 0, (size_t)n * sizeof(u32));
  for (i = 0; i < len; i++) for (b = 7; b >= 0; b--) {
    u32 top = r[n - 1] >> 31;
    for (k = n - 1; k > 0; k--) r[k] = (r[k] << 1) | (r[k - 1] >> 31);
    r[0] = (r[0] << 1) | ((p[i] >> b) & 1);
    if (top || bn_cmp(r, m, n) >= 0) bn_sub(r, r, m, n);
  }
}
static void mont_mul(const Mont *M, u32 *r, const u32 *a, const u32 *b)
{
  int n = M->n, i, j;
  u32 t[BN_LIMBS + 2];
  memset(t, 0, sizeof(u32) * (size_t)(n + 2));
  for (i = 0; i < n; i++) {
    u64 c = 0;
    u32 m;
    for (j = 0; j < n; j++) { c = (u64)t[j] + (u64)a[j] * b[i] + c; t[j] = (u32)c; c >>= 32; }
    c = (u64)t[n] + c; t[n] = (u32)c; t[n + 1] = (u32)(c >> 32);
    m = t[0] * M->minv;
    c = ((u64)t[0] + (u64)m * M->m[0]) >> 32;
    for (j = 1; j < n; j++) { c = (u64)t[j] + (u64)m * M->m[j] + c; t[j - 1] = (u32)c; c >>= 32; }
    c = (u64)t[n] + c; t[n - 1] = (u32)c; t[n] = t[n + 1] + (u32)(c >> 32);
  }
  if (t[n] || bn_cmp(t, M->m, n) >= 0) bn_sub(t, t, M->m, n);
  memcpy(r, t, sizeof(u32) * (size_t)n);
}
/* the modulus m (big-endian bytes, odd) set up; 0 if it is even or too big */
static int mont_init(Mont *M, const u8 *m, size_t len)
{
  int n, i, k;
  u32 x;
  while (len > 1 && m[0] == 0) { m++; len--; }
  n = (int)((len + 3) / 4);
  if (n < 1 || n > BN_LIMBS - 2 || !(m[len - 1] & 1)) return 0;
  memset(M, 0, sizeof *M);
  M->n = n;
  bn_from(M->m, n, m, len);
  x = M->m[0];
  for (i = 0; i < 5; i++) x *= 2 - M->m[0] * x;
  M->minv = (u32)0 - x;
  /* R mod m, then R^2 mod m by doubling */
  memset(M->rr, 0, sizeof M->rr);
  M->rr[0] = 1;
  for (i = 0; i < 64 * n; i++) {
    u32 top = M->rr[n - 1] >> 31;
    for (k = n - 1; k > 0; k--) M->rr[k] = (M->rr[k] << 1) | (M->rr[k - 1] >> 31);
    M->rr[0] <<= 1;
    if (top || bn_cmp(M->rr, M->m, n) >= 0) bn_sub(M->rr, M->rr, M->m, n);
    if (i == 32 * n - 1) memcpy(M->one, M->rr, sizeof M->one);   /* R mod m: one, in Montgomery form */
  }
  return 1;
}
static void to_mont(const Mont *M, u32 *r, const u32 *a) { mont_mul(M, r, a, M->rr); }
static void from_mont(const Mont *M, u32 *r, const u32 *a) { u32 one[BN_LIMBS]; memset(one, 0, sizeof one); one[0] = 1; mont_mul(M, r, a, one); }
/* r = base^e mod m, all outside Montgomery form; base < m. A 4-bit window */
static void mod_exp(const Mont *M, u32 *r, const u32 *base, const u8 *e, size_t elen)
{
  static u32 tab[16][BN_LIMBS];
  u32 acc[BN_LIMBS];
  int i, n = M->n;
  size_t k;
  memcpy(tab[0], M->one, sizeof(u32) * (size_t)n);
  to_mont(M, tab[1], base);
  for (i = 2; i < 16; i++) mont_mul(M, tab[i], tab[i - 1], tab[1]);
  memcpy(acc, M->one, sizeof(u32) * (size_t)n);
  for (k = 0; k < elen; k++) {
    int h;
    for (h = 0; h < 2; h++) {
      int nib = h ? e[k] & 15 : e[k] >> 4, s;
      for (s = 0; s < 4; s++) mont_mul(M, acc, acc, acc);
      if (nib) mont_mul(M, acc, acc, tab[nib]);
    }
  }
  from_mont(M, r, acc);
}
/* r = a * b mod m (outside Montgomery form) */
static void mod_mul(const Mont *M, u32 *r, const u32 *a, const u32 *b) { u32 t[BN_LIMBS]; mont_mul(M, t, a, b); mont_mul(M, r, t, M->rr); }
/* r = a^-1 mod m for a prime m (Fermat) */
static void mod_inv_prime(const Mont *M, u32 *r, const u32 *a)
{
  u32 e[BN_LIMBS], two[BN_LIMBS];
  u8 eb[BN_LIMBS * 4];
  memset(two, 0, sizeof two); two[0] = 2;
  bn_sub(e, M->m, two, M->n);
  bn_to(e, M->n, eb, (size_t)M->n * 4);
  mod_exp(M, r, a, eb, (size_t)M->n * 4);
}

/* ================================================================== RSA */
static const char *const digest_info[6] = { NULL, "3020300c06082a864886f70d020505000410", "3021300906052b0e03021a05000414",
  "3031300d060960864801650304020105000420", "3041300d060960864801650304020205000430", "3051300d060960864801650304020305000440" };
/* EMSA-PKCS1-v1_5 of hash into em (k bytes): 0 if it doesn't fit */
static int emsa_pkcs1(int alg, const u8 *hash, u8 *em, int k)
{
  const char *di = alg > 0 && alg < 6 ? digest_info[alg] : NULL;
  int dl, hl = hash_len(alg), i;
  if (!di) return 0;
  dl = (int)strlen(di) / 2;
  if (k < dl + hl + 11) return 0;
  em[0] = 0; em[1] = 1;
  memset(em + 2, 0xFF, (size_t)(k - dl - hl - 3));
  em[k - dl - hl - 1] = 0;
  for (i = 0; i < dl; i++) { unsigned v; sscanf(di + 2 * i, "%2x", &v); em[k - dl - hl + i] = (u8)v; }
  memcpy(em + k - hl, hash, (size_t)hl);
  return 1;
}
static int rsa_verify(const PubKey *k, int alg, const u8 *hash, const u8 *sig, size_t siglen)
{
  static Mont M;
  u32 s[BN_LIMBS], m[BN_LIMBS];
  u8 em[520], want[520];
  int klen = k->nlen;
  if (klen < 64 || klen > 512 || siglen > (size_t)klen + 1 || !mont_init(&M, k->n, (size_t)klen)) return -1;
  bn_from(s, M.n, sig, siglen);
  if (bn_cmp(s, M.m, M.n) >= 0) return 0;
  mod_exp(&M, m, s, k->e, (size_t)k->elen);
  bn_to(m, M.n, em, (size_t)klen);
  if (!emsa_pkcs1(alg, hash, want, klen)) return -1;
  if (!memcmp(em, want, (size_t)klen)) return 1;
  /* (some signers leave out the NULL parameters of the digest algorithm) */
  {
    int hl = hash_len(alg), i = 2;
    while (i < klen && em[i] == 0xFF) i++;
    if (em[0] == 0 && em[1] == 1 && i < klen && em[i] == 0 && klen - hl >= 0 && !memcmp(em + klen - hl, hash, (size_t)hl) &&
        klen - hl - (i + 1) == (int)strlen(digest_info[alg]) / 2 - 2) return 1;
  }
  return 0;
}
static int rsa_sign(const PrivKey *k, int alg, const u8 *hash, u8 *sig, int cap)
{
  static Mont M, P, Q;
  static u32 c[BN_LIMBS], s[BN_LIMBS], m1[BN_LIMBS], m2[BN_LIMBS], h[BN_LIMBS], t[BN_LIMBS], qi[BN_LIMBS];
  u8 em[520];
  int klen = k->pub.nlen;
  if (klen > cap || klen > 512 || !emsa_pkcs1(alg, hash, em, klen) || !mont_init(&M, k->pub.n, (size_t)klen)) return 0;
  bn_from(c, M.n, em, (size_t)klen);
  if (k->plen > 0 && mont_init(&P, k->p, (size_t)k->plen) && mont_init(&Q, k->q, (size_t)k->plen)) {
    int i, pn = P.n, qn = Q.n, j;
    u64 carry;
    /* m1 = c^dp mod p, m2 = c^dq mod q, h = qi (m1 - m2) mod p, s = m2 + h q */
    {
      u8 cb[520];
      bn_to(c, M.n, cb, (size_t)klen);
      bn_mod_bytes(t, cb, (size_t)klen, P.m, pn); mod_exp(&P, m1, t, k->dp, (size_t)k->plen);
      bn_mod_bytes(t, cb, (size_t)klen, Q.m, qn); mod_exp(&Q, m2, t, k->dq, (size_t)k->plen);
      bn_to(m2, qn, cb, (size_t)qn * 4);
      bn_mod_bytes(t, cb, (size_t)qn * 4, P.m, pn);
    }
    if (bn_sub(h, m1, t, pn)) bn_add(h, h, P.m, pn);
    bn_from(qi, pn, k->qi, (size_t)k->plen);
    mod_mul(&P, h, h, qi);
    memset(s, 0, sizeof s);
    for (i = 0; i < pn; i++) {
      carry = 0;
      for (j = 0; j < qn; j++) { carry = (u64)s[i + j] + (u64)h[i] * Q.m[j] + carry; s[i + j] = (u32)carry; carry >>= 32; }
      for (j = i + qn; carry && j < BN_LIMBS; j++) { carry += s[j]; s[j] = (u32)carry; carry >>= 32; }
    }
    memset(t, 0, sizeof t); memcpy(t, m2, sizeof(u32) * (size_t)qn);
    bn_add(s, s, t, M.n);
  } else {
    if (k->dlen < 1) return 0;
    mod_exp(&M, s, c, k->d, (size_t)k->dlen);
  }
  bn_to(s, M.n, sig, (size_t)klen);
  /* (checked before it is given out: a fault in the arithmetic must not leak the key) */
  return rsa_verify(&k->pub, alg, hash, sig, (size_t)klen) == 1 ? klen : 0;
}

/* ================================================================== elliptic curves */
typedef struct { const char *p, *n, *gx, *gy; int len; } Curve;
static const Curve curves[3] = {
  { NULL, NULL, NULL, NULL, 0 },
  { "ffffffff00000001000000000000000000000000ffffffffffffffffffffffff", "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551",
    "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296", "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5", 32 },
  { "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffff0000000000000000ffffffff",
    "ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf581a0db248b0a77aecec196accc52973",
    "aa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a385502f25dbf55296c3a545e3872760ab7",
    "3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c00a60b1ce1d7e819d7a431d7c90ea0e5f", 48 },
};
static void hexbytes(const char *h, u8 *out, int n) { int i; for (i = 0; i < n; i++) { unsigned v = 0; sscanf(h + 2 * i, "%2x", &v); out[i] = (u8)v; } }
typedef struct { Mont F, N; int len, n; } Ec;
#define EL 14                        /* (limbs enough for P-384's numbers: points stay small on the stack) */
typedef struct { u32 x[EL], y[EL], z[EL]; } Pt;
static int ec_init(Ec *E, int curve)
{
  u8 b[64];
  if (curve < 1 || curve > 2) return 0;
  E->len = curves[curve].len;
  hexbytes(curves[curve].p, b, E->len); if (!mont_init(&E->F, b, (size_t)E->len)) return 0;
  hexbytes(curves[curve].n, b, E->len); if (!mont_init(&E->N, b, (size_t)E->len)) return 0;
  E->n = E->F.n;
  return 1;
}
static void f_add(const Ec *E, u32 *r, const u32 *a, const u32 *b) { if (bn_add(r, a, b, E->n) || bn_cmp(r, E->F.m, E->n) >= 0) bn_sub(r, r, E->F.m, E->n); }
static void f_sub(const Ec *E, u32 *r, const u32 *a, const u32 *b) { if (bn_sub(r, a, b, E->n)) bn_add(r, r, E->F.m, E->n); }
static void f_mul(const Ec *E, u32 *r, const u32 *a, const u32 *b) { mont_mul(&E->F, r, a, b); }
static void pt_double(const Ec *E, Pt *r, const Pt *p)
{
  u32 d[EL], g[EL], be[EL], al[EL], t[EL], u[EL], x3[EL];
  if (bn_zero(p->z, E->n)) { *r = *p; return; }
  f_mul(E, d, p->z, p->z); f_mul(E, g, p->y, p->y); f_mul(E, be, p->x, g);
  f_sub(E, t, p->x, d); f_add(E, u, p->x, d); f_mul(E, al, t, u); f_add(E, t, al, al); f_add(E, al, t, al);
  f_mul(E, x3, al, al); f_add(E, t, be, be); f_add(E, t, t, t); f_add(E, u, t, t); f_sub(E, x3, x3, u);
  f_add(E, u, p->y, p->z); f_mul(E, u, u, u); f_sub(E, u, u, g); f_sub(E, r->z, u, d);
  f_sub(E, u, t, x3); f_mul(E, u, al, u); f_mul(E, g, g, g); f_add(E, g, g, g); f_add(E, g, g, g); f_add(E, g, g, g);
  f_sub(E, r->y, u, g);
  memcpy(r->x, x3, sizeof x3);
}
static void pt_add(const Ec *E, Pt *r, const Pt *a, const Pt *b)
{
  u32 z1z1[EL], z2z2[EL], u1[EL], u2[EL], s1[EL], s2[EL], h[EL], i[EL], j[EL], rr[EL], v[EL], t[EL];
  Pt o;
  if (bn_zero(a->z, E->n)) { *r = *b; return; }
  if (bn_zero(b->z, E->n)) { *r = *a; return; }
  f_mul(E, z1z1, a->z, a->z); f_mul(E, z2z2, b->z, b->z);
  f_mul(E, u1, a->x, z2z2); f_mul(E, u2, b->x, z1z1);
  f_mul(E, s1, a->y, b->z); f_mul(E, s1, s1, z2z2); f_mul(E, s2, b->y, a->z); f_mul(E, s2, s2, z1z1);
  f_sub(E, h, u2, u1); f_sub(E, rr, s2, s1);
  if (bn_zero(h, E->n)) {
    if (bn_zero(rr, E->n)) { pt_double(E, r, a); return; }
    memset(r, 0, sizeof *r); return;
  }
  f_add(E, i, h, h); f_mul(E, i, i, i); f_mul(E, j, h, i); f_add(E, rr, rr, rr); f_mul(E, v, u1, i);
  f_mul(E, o.x, rr, rr); f_sub(E, o.x, o.x, j); f_sub(E, o.x, o.x, v); f_sub(E, o.x, o.x, v);
  f_sub(E, t, v, o.x); f_mul(E, o.y, rr, t); f_mul(E, t, s1, j); f_add(E, t, t, t); f_sub(E, o.y, o.y, t);
  f_add(E, t, a->z, b->z); f_mul(E, t, t, t); f_sub(E, t, t, z1z1); f_sub(E, t, t, z2z2); f_mul(E, o.z, t, h);
  *r = o;
}
/* an affine point (bytes) into Jacobian Montgomery form */
static void pt_from(const Ec *E, Pt *p, const u8 *x, const u8 *y)
{
  u32 t[EL];
  bn_from(t, E->n, x, (size_t)E->len); to_mont(&E->F, p->x, t);
  bn_from(t, E->n, y, (size_t)E->len); to_mont(&E->F, p->y, t);
  memcpy(p->z, E->F.one, sizeof p->z);
}
/* the affine x (outside Montgomery form) of p; 0 for the point at infinity */
static int pt_x(const Ec *E, u32 *x, u32 *y, const Pt *p)
{
  u32 zi[EL], z[EL], t[EL];
  if (bn_zero(p->z, E->n)) return 0;
  from_mont(&E->F, z, p->z);
  mod_inv_prime(&E->F, zi, z);
  to_mont(&E->F, zi, zi);
  f_mul(E, t, zi, zi);
  f_mul(E, x, p->x, t); from_mont(&E->F, x, x);
  if (y) { f_mul(E, t, t, zi); f_mul(E, y, p->y, t); from_mont(&E->F, y, y); }
  return 1;
}
/* r = k1 G + k2 Q (k2 and Q may be left out), Shamir's trick */
static void pt_mul2(const Ec *E, Pt *r, const u32 *k1, const Pt *g, const u32 *k2, const Pt *q)
{
  Pt gq, acc;
  int i;
  if (q) pt_add(E, &gq, g, q);
  memset(&acc, 0, sizeof acc);
  for (i = E->n * 32 - 1; i >= 0; i--) {
    int b1 = (k1[i / 32] >> (i % 32)) & 1, b2 = q ? (k2[i / 32] >> (i % 32)) & 1 : 0;
    pt_double(E, &acc, &acc);
    if (b1 && b2) pt_add(E, &acc, &acc, &gq);
    else if (b1) pt_add(E, &acc, &acc, g);
    else if (b2) pt_add(E, &acc, &acc, q);
  }
  *r = acc;
}
/* the hash as an integer mod n (its leftmost bits, as long as n) */
static void ec_e(const Ec *E, u32 *e, const u8 *hash, int hl)
{
  if (hl > E->len) hl = E->len;
  bn_from(e, E->n, hash, (size_t)hl);
  if (bn_cmp(e, E->N.m, E->n) >= 0) bn_sub(e, e, E->N.m, E->n);
}
static int ec_verify(const PubKey *k, int alg, const u8 *hash, const u8 *sig, size_t siglen)
{
  static Ec E;
  u32 r[EL], s[EL], e[EL], w[EL], u1[EL], u2[EL], x[EL];
  Pt G, Q, R;
  Der in, seq, ri, si;
  u8 gx[64], gy[64];
  if (!ec_init(&E, k->curve) || k->nlen != E.len || k->ylen != E.len) return -1;
  in.p = sig; in.n = siglen;
  if (!der_expect(&in, 0x30, &seq) || !der_expect(&seq, 0x02, &ri) || !der_expect(&seq, 0x02, &si)) return 0;
  int_bytes(&ri); int_bytes(&si);
  if (ri.n > (size_t)E.len || si.n > (size_t)E.len) return 0;
  bn_from(r, E.n, ri.p, ri.n); bn_from(s, E.n, si.p, si.n);
  if (bn_zero(r, E.n) || bn_zero(s, E.n) || bn_cmp(r, E.N.m, E.n) >= 0 || bn_cmp(s, E.N.m, E.n) >= 0) return 0;
  ec_e(&E, e, hash, hash_len(alg));
  mod_inv_prime(&E.N, w, s);
  mod_mul(&E.N, u1, e, w); mod_mul(&E.N, u2, r, w);
  hexbytes(curves[k->curve].gx, gx, E.len); hexbytes(curves[k->curve].gy, gy, E.len);
  pt_from(&E, &G, gx, gy); pt_from(&E, &Q, k->n, k->y);
  pt_mul2(&E, &R, u1, &G, u2, &Q);
  if (!pt_x(&E, x, NULL, &R)) return 0;
  if (bn_cmp(x, E.N.m, E.n) >= 0) bn_sub(x, x, E.N.m, E.n);
  return !bn_cmp(x, r, E.n);
}
static int ec_sign(const PrivKey *k, int alg, const u8 *hash, u8 *sig, int cap)
{
  static Ec E;
  u32 d[EL], kk[EL], e[EL], r[EL], s[EL], ki[EL], t[EL];
  Pt G, R;
  u8 gx[64], gy[64], rb[66], sb[66], seed[200], kb[64];
  Buf in, out;
  int tries;
  if (!ec_init(&E, k->pub.curve) || k->dlen < 1 || k->dlen > E.len) return 0;
  bn_from(d, E.n, k->d, (size_t)k->dlen);
  ec_e(&E, e, hash, hash_len(alg));
  hexbytes(curves[k->pub.curve].gx, gx, E.len); hexbytes(curves[k->pub.curve].gy, gy, E.len);
  pt_from(&E, &G, gx, gy);
  for (tries = 0; tries < 8; tries++) {
    /* k from the key, the hash and fresh random bytes, reduced mod n */
    memcpy(seed, k->d, (size_t)k->dlen);
    memcpy(seed + k->dlen, hash, (size_t)hash_len(alg));
    pdf_random(seed + k->dlen + hash_len(alg), 32, hash, (size_t)hash_len(alg));
    sha512(seed, (size_t)(k->dlen + hash_len(alg) + 32), kb);
    bn_mod_bytes(kk, kb, 64, E.N.m, E.n);
    if (bn_zero(kk, E.n)) continue;
    pt_mul2(&E, &R, kk, &G, NULL, NULL);
    if (!pt_x(&E, r, NULL, &R)) continue;
    if (bn_cmp(r, E.N.m, E.n) >= 0) bn_sub(r, r, E.N.m, E.n);
    if (bn_zero(r, E.n)) continue;
    mod_mul(&E.N, t, r, d);
    if (bn_add(t, t, e, E.n) || bn_cmp(t, E.N.m, E.n) >= 0) bn_sub(t, t, E.N.m, E.n);
    mod_inv_prime(&E.N, ki, kk);
    mod_mul(&E.N, s, ki, t);
    if (bn_zero(s, E.n)) continue;
    break;
  }
  memset(seed, 0, sizeof seed); memset(kk, 0, sizeof kk);
  if (tries == 8) return 0;
  bn_to(r, E.n, rb, (size_t)E.len); bn_to(s, E.n, sb, (size_t)E.len);
  buf_init(&in); buf_init(&out);
  dput_uint(&in, rb, (size_t)E.len); dput_uint(&in, sb, (size_t)E.len);
  dwrap(&out, 0x30, &in);
  if (out.oom || (int)out.len > cap) { buf_free(&out); return 0; }
  memcpy(sig, out.data, out.len);
  tries = (int)out.len;
  buf_free(&out);
  return tries;
}
/* the public point d G (for an EC key given without it) */
static int ec_public(PrivKey *k)
{
  static Ec E;
  u32 d[EL], x[EL], y[EL];
  Pt G, R;
  u8 gx[64], gy[64];
  if (!ec_init(&E, k->pub.curve) || k->dlen < 1 || k->dlen > E.len) return 0;
  bn_from(d, E.n, k->d, (size_t)k->dlen);
  hexbytes(curves[k->pub.curve].gx, gx, E.len); hexbytes(curves[k->pub.curve].gy, gy, E.len);
  pt_from(&E, &G, gx, gy);
  pt_mul2(&E, &R, d, &G, NULL, NULL);
  if (!pt_x(&E, x, y, &R)) return 0;
  bn_to(x, E.n, k->pub.n, (size_t)E.len); bn_to(y, E.n, k->pub.y, (size_t)E.len);
  k->pub.nlen = k->pub.ylen = E.len;
  return 1;
}

int pk_verify(const PubKey *k, int alg, const u8 *hash, const u8 *sig, size_t siglen)
{
  if (!hash_len(alg)) return -1;
  if (k->type == PK_RSA) return rsa_verify(k, alg, hash, sig, siglen);
  if (k->type == PK_EC) return ec_verify(k, alg, hash, sig, siglen);
  return -1;
}
int pk_sign(const PrivKey *k, int alg, const u8 *hash, u8 *sig, int cap)
{
  if (!hash_len(alg)) return 0;
  if (k->pub.type == PK_RSA) return rsa_sign(k, alg, hash, sig, cap);
  if (k->pub.type == PK_EC) return ec_sign(k, alg, hash, sig, cap);
  return 0;
}

/* ================================================================== X.509 */
#define OID_RSA "2a864886f70d010101"
#define OID_ECPUB "2a8648ce3d0201"
#define OID_P256 "2a8648ce3d030107"
#define OID_P384 "2b81040022"
static int digest_oid(const Der *o)
{
  if (oid_is(o, "2b0e03021a")) return HASH_SHA1;
  if (oid_is(o, "608648016503040201")) return HASH_SHA256;
  if (oid_is(o, "608648016503040202")) return HASH_SHA384;
  if (oid_is(o, "608648016503040203")) return HASH_SHA512;
  if (oid_is(o, "2a864886f70d0205")) return HASH_MD5;
  return HASH_NONE;
}
static const char *digest_oid_hex(int alg)
{
  return alg == HASH_SHA1 ? "2b0e03021a" : alg == HASH_SHA384 ? "608648016503040202" : alg == HASH_SHA512 ? "608648016503040203" : "608648016503040201";
}
/* a signature algorithm: its hash and key kind (0 hash: unknown, or PSS) */
static void sigalg_oid(const Der *o, int *hash, int *pk)
{
  *hash = HASH_NONE; *pk = PK_NONE;
  if (oid_is(o, "2a864886f70d010105")) { *hash = HASH_SHA1; *pk = PK_RSA; }
  else if (oid_is(o, "2a864886f70d01010b")) { *hash = HASH_SHA256; *pk = PK_RSA; }
  else if (oid_is(o, "2a864886f70d01010c")) { *hash = HASH_SHA384; *pk = PK_RSA; }
  else if (oid_is(o, "2a864886f70d01010d")) { *hash = HASH_SHA512; *pk = PK_RSA; }
  else if (oid_is(o, "2a864886f70d010104")) { *hash = HASH_MD5; *pk = PK_RSA; }
  else if (oid_is(o, OID_RSA)) *pk = PK_RSA;
  else if (oid_is(o, "2a8648ce3d0401")) { *hash = HASH_SHA1; *pk = PK_EC; }
  else if (oid_is(o, "2a8648ce3d040302")) { *hash = HASH_SHA256; *pk = PK_EC; }
  else if (oid_is(o, "2a8648ce3d040303")) { *hash = HASH_SHA384; *pk = PK_EC; }
  else if (oid_is(o, "2a8648ce3d040304")) { *hash = HASH_SHA512; *pk = PK_EC; }
  else if (oid_is(o, OID_ECPUB)) *pk = PK_EC;
}
/* a directory string as UTF-8 */
static void dir_string(int tag, const Der *v, char *out, int cap)
{
  int o = 0;
  size_t i;
  if (tag == 0x1E) {
    for (i = 0; i + 1 < v->n && o + 4 < cap; i += 2) o += utf8_put(out + o, ((u32)v->p[i] << 8) | v->p[i + 1]);
  } else if (tag == 0x14) {
    for (i = 0; i < v->n && o + 3 < cap; i++) o += utf8_put(out + o, v->p[i]);
  } else {
    for (i = 0; i < v->n && o + 1 < cap; i++) out[o++] = (char)v->p[i];
  }
  out[o] = 0;
}
static void name_attr(const Der *name, const char *oidhex, char *out, int cap)
{
  Der in = *name, set, w, seq, oid, val;
  int t;
  out[0] = 0;
  while (in.n && der_next(&in, &t, &set, &w)) {
    while (set.n && der_expect(&set, 0x30, &seq)) {
      if (!der_expect(&seq, 0x06, &oid) || !der_next(&seq, &t, &val, &w)) continue;
      if (oid_is(&oid, oidhex)) dir_string(t, &val, out, cap);
    }
  }
}
static void der_time(int tag, const Der *v, char *out)
{
  char s[20];
  size_t n = v->n < 19 ? v->n : 19;
  memcpy(s, v->p, n); s[n] = 0;
  out[0] = 0;
  if (tag == 0x17 && n >= 12) snprintf(out, 16, "%s%.12s", s[0] < '5' ? "20" : "19", s);
  else if (tag == 0x18 && n >= 14) snprintf(out, 16, "%.14s", s);
}
static int spki(Der *in, PubKey *k)
{
  Der s, alg, oid, bits, par, kd, kn, ke;
  int t;
  memset(k, 0, sizeof *k);
  if (!der_expect(in, 0x30, &s) || !der_expect(&s, 0x30, &alg) || !der_expect(&alg, 0x06, &oid) || !der_expect(&s, 0x03, &bits) || bits.n < 2) return 0;
  bits.p++; bits.n--;
  if (oid_is(&oid, OID_RSA)) {
    if (!der_expect(&bits, 0x30, &kd) || !der_expect(&kd, 0x02, &kn) || !der_expect(&kd, 0x02, &ke)) return 0;
    int_bytes(&kn); int_bytes(&ke);
    if (kn.n > sizeof k->n || ke.n > sizeof k->e) return 0;
    k->type = PK_RSA;
    memcpy(k->n, kn.p, kn.n); k->nlen = (int)kn.n;
    memcpy(k->e, ke.p, ke.n); k->elen = (int)ke.n;
    return 1;
  }
  if (oid_is(&oid, OID_ECPUB)) {
    int len;
    if (!der_expect(&alg, 0x06, &par)) return 0;
    k->curve = oid_is(&par, OID_P256) ? EC_P256 : oid_is(&par, OID_P384) ? EC_P384 : EC_NONE;
    k->type = PK_EC;
    if (!k->curve) return 1;                         /* (a curve Folio can't check) */
    len = curves[k->curve].len;
    if (bits.n != (size_t)(1 + 2 * len) || bits.p[0] != 4) return 0;
    memcpy(k->n, bits.p + 1, (size_t)len); memcpy(k->y, bits.p + 1 + len, (size_t)len);
    k->nlen = k->ylen = len;
    return 1;
  }
  (void)t;
  return 1;
}
int cert_parse(const u8 *der, size_t n, Cert *c)
{
  Der in, cert, tbs, w, f, oid, val;
  int t;
  memset(c, 0, sizeof *c);
  in.p = der; in.n = n;
  if (!der_next(&in, &t, &cert, &c->der) || t != 0x30) return 0;
  if (!der_next(&cert, &t, &tbs, &c->tbs) || t != 0x30) return 0;
  if (!der_next(&cert, &t, &f, &c->sigalg_der) || t != 0x30 || !der_expect(&f, 0x06, &oid)) return 0;
  sigalg_oid(&oid, &c->sig_hash, &c->sig_pk);
  if (!der_expect(&cert, 0x03, &c->sig) || c->sig.n < 1) return 0;
  c->sig.p++; c->sig.n--;
  if (der_peek(&tbs) == 0xA0) der_skip(&tbs);
  if (!der_next(&tbs, &t, &c->serial, &w) || t != 0x02) return 0;
  if (!der_skip(&tbs)) return 0;                                      /* (the signature algorithm again) */
  if (!der_next(&tbs, &t, &f, &c->issuer) || t != 0x30) return 0;
  name_attr(&f, "550403", c->issuer_cn, sizeof c->issuer_cn);
  if (!c->issuer_cn[0]) name_attr(&f, "55040a", c->issuer_cn, sizeof c->issuer_cn);
  if (!der_expect(&tbs, 0x30, &f)) return 0;
  if (der_next(&f, &t, &val, &w)) der_time(t, &val, c->not_before);
  if (der_next(&f, &t, &val, &w)) der_time(t, &val, c->not_after);
  if (!der_next(&tbs, &t, &f, &c->subject) || t != 0x30) return 0;
  name_attr(&f, "550403", c->subject_cn, sizeof c->subject_cn);
  name_attr(&f, "55040a", c->org, sizeof c->org);
  name_attr(&f, "2a864886f70d010901", c->email, sizeof c->email);
  if (!c->subject_cn[0]) snprintf(c->subject_cn, sizeof c->subject_cn, "%s", c->org);
  if (!spki(&tbs, &c->key)) return 0;
  while (tbs.n && der_next(&tbs, &t, &f, &w)) {
    Der exts, e;
    if (t != 0xA3 || !der_expect(&f, 0x30, &exts)) continue;
    while (der_expect(&exts, 0x30, &e)) {
      Der eo, ev;
      if (!der_expect(&e, 0x06, &eo)) continue;
      if (der_peek(&e) == 0x01) der_skip(&e);
      if (!der_expect(&e, 0x04, &ev)) continue;
      if (oid_is(&eo, "551d0e")) { Der k; if (der_expect(&ev, 0x04, &k)) c->ski = k; }
      else if (oid_is(&eo, "551d13")) { Der bc, b; if (der_expect(&ev, 0x30, &bc) && der_expect(&bc, 0x01, &b) && b.n == 1 && b.p[0]) c->is_ca = 1; }
    }
  }
  return 1;
}
int cert_signed_by(const Cert *c, const Cert *issuer)
{
  u8 h[64];
  if (!c->sig_hash || !hash_len(c->sig_hash) || c->sig_pk != issuer->key.type) return -1;
  hash_of(c->sig_hash, c->tbs.p, c->tbs.n, h);
  return pk_verify(&issuer->key, c->sig_hash, h, c->sig.p, c->sig.n);
}
static int same_der(const Der *a, const Der *b) { return a->n == b->n && !memcmp(a->p, b->p, a->n); }

/* ================================================================== CMS */
#define OID_DATA "2a864886f70d010701"
#define OID_SIGNED "2a864886f70d010702"
#define OID_MSGDIGEST "2a864886f70d010904"
#define OID_SIGNTIME "2a864886f70d010905"
#define OID_CTYPE "2a864886f70d010903"
#define MAXC 16
int cms_verify(const u8 *sig, size_t siglen, const u8 *data, size_t dlen, const u8 *const *trusted, const size_t *tlen, int ntrusted,
               const char *now, CmsResult *r)
{
  Der in, ci, oid, ex, sd, w, set, f, si, sid, attrs = { NULL, 0 }, attrs_whole = { NULL, 0 }, sigv, eci, econt = { NULL, 0 };
  Cert *certs = NULL;
  int nc = 0, t, i, signer = -1, dig, sa_hash, sa_pk;
  u8 h[64], mh[64];
  memset(r, 0, sizeof *r);
  r->ok = r->digest_ok = r->time_ok = -1;
  while (siglen > 0 && sig[siglen - 1] == 0) siglen--;
  siglen += 2;                                         /* (an end-of-contents may end it) */
  in.p = sig; in.n = siglen;
  if (!der_expect(&in, 0x30, &ci) || !der_expect(&ci, 0x06, &oid) || !oid_is(&oid, OID_SIGNED) || !der_expect(&ci, 0xA0, &ex) ||
      !der_expect(&ex, 0x30, &sd)) { snprintf(r->why, sizeof r->why, "not a CMS signature"); return 0; }
  der_skip(&sd);                                       /* version */
  der_skip(&sd);                                       /* digestAlgorithms */
  if (der_expect(&sd, 0x30, &eci)) {
    Der eo, wrap;
    if (der_expect(&eci, 0x06, &eo) && der_expect(&eci, 0xA0, &wrap)) {
      int tt;
      Der c2;
      if (der_next(&wrap, &tt, &c2, &w) && (tt == 0x04 || tt == 0x24)) econt = c2;
    }
  }
  if (!(certs = (Cert *)calloc(MAXC, sizeof(Cert)))) return 0;
  if (der_peek(&sd) == 0xA0 && der_expect(&sd, 0xA0, &set)) {
    while (set.n && nc < MAXC) {
      Der c2, wh;
      if (!der_next(&set, &t, &c2, &wh)) break;
      if (t == 0x30 && cert_parse(wh.p, wh.n, &certs[nc])) nc++;
    }
  }
  if (der_peek(&sd) == 0xA1) der_skip(&sd);
  if (!der_expect(&sd, 0x31, &set) || !der_expect(&set, 0x30, &si)) { snprintf(r->why, sizeof r->why, "no signer"); free(certs); return 0; }
  der_skip(&si);                                       /* version */
  if (!der_next(&si, &t, &sid, &w)) { free(certs); return 0; }
  for (i = 0; i < nc && signer < 0; i++) {
    if (t == 0x30) {
      Der s2 = sid, iss, ser, wi;
      int t2;
      if (der_next(&s2, &t2, &iss, &wi) && der_next(&s2, &t2, &ser, &w) && same_der(&wi, &certs[i].issuer) && same_der(&ser, &certs[i].serial)) signer = i;
    } else if (t == 0x80 && certs[i].ski.n && same_der(&sid, &certs[i].ski)) signer = i;
  }
  if (!der_expect(&si, 0x30, &f) || !der_expect(&f, 0x06, &oid)) { free(certs); return 0; }
  dig = digest_oid(&oid);
  r->hash = dig;
  if (der_peek(&si) == 0xA0) der_next(&si, &t, &attrs, &attrs_whole);
  if (!der_expect(&si, 0x30, &f) || !der_expect(&f, 0x06, &oid)) { free(certs); return 0; }
  sigalg_oid(&oid, &sa_hash, &sa_pk);
  if (!der_next(&si, &t, &sigv, &w) || (t != 0x04 && t != 0x24)) { free(certs); return 0; }
  if (!hash_len(dig)) { snprintf(r->why, sizeof r->why, "unknown digest algorithm"); free(certs); return 0; }
  hash_of(dig, data, dlen, h);
  /* the signed attributes: messageDigest and signingTime */
  if (attrs_whole.n) {
    Der a = attrs, at;
    while (der_expect(&a, 0x30, &at)) {
      Der ao, av, v;
      int tv;
      if (!der_expect(&at, 0x06, &ao) || !der_expect(&at, 0x31, &av)) continue;
      if (oid_is(&ao, OID_MSGDIGEST) && der_expect(&av, 0x04, &v)) r->digest_ok = v.n == (size_t)hash_len(dig) && !memcmp(v.p, h, v.n);
      else if (oid_is(&ao, OID_SIGNTIME) && der_next(&av, &tv, &v, &w)) der_time(tv, &v, r->signed_at);
    }
  } else if (econt.n) {
    /* adbe.pkcs7.sha1: the digest of the data is the signed content */
    Buf ob; Der oc;
    u8 h1[20];
    buf_init(&ob);
    sha1(data, dlen, h1);
    if (octets(0x04 | (econt.p == NULL ? 0 : 0), &econt, &ob, &oc)) r->digest_ok = oc.n == 20 && !memcmp(oc.p, h1, 20);
    buf_free(&ob);
  } else r->digest_ok = 1;                             /* (the signature itself is over the data's digest) */
  if (signer < 0) { snprintf(r->why, sizeof r->why, "the signer's certificate isn't in the signature"); free(certs); return 1; }
  {
    Cert *sc = &certs[signer];
    int pk = sa_pk ? sa_pk : sc->key.type;
    snprintf(r->signer, sizeof r->signer, "%s", sc->subject_cn);
    snprintf(r->issuer, sizeof r->issuer, "%s", sc->issuer_cn);
    snprintf(r->org, sizeof r->org, "%s", sc->org);
    memcpy(r->not_before, sc->not_before, 16); memcpy(r->not_after, sc->not_after, 16);
    r->pk = pk;
    if (sc->key.type == PK_EC && !sc->key.curve) snprintf(r->why, sizeof r->why, "an elliptic curve Folio can't check");
    else if (attrs_whole.n) {
      u8 *buf = (u8 *)malloc(attrs_whole.n);
      if (buf) {
        memcpy(buf, attrs_whole.p, attrs_whole.n);
        buf[0] = 0x31;
        hash_of(dig, buf, attrs_whole.n, mh);
        free(buf);
        r->ok = pk_verify(&sc->key, dig, mh, sigv.p, sigv.n);
      }
    } else if (econt.n) {
      Buf ob; Der oc;
      buf_init(&ob);
      if (octets(0x04, &econt, &ob, &oc)) { hash_of(dig, oc.p, oc.n, mh); r->ok = pk_verify(&sc->key, dig, mh, sigv.p, sigv.n); }
      buf_free(&ob);
    } else r->ok = pk_verify(&sc->key, dig, h, sigv.p, sigv.n);
    if (r->ok < 0 && !r->why[0]) snprintf(r->why, sizeof r->why, "a signature method Folio can't check");
    /* the time */
    {
      const char *when = r->signed_at[0] ? r->signed_at : now;
      if (when && when[0] && sc->not_before[0] && sc->not_after[0]) r->time_ok = strcmp(when, sc->not_before) >= 0 && strcmp(when, sc->not_after) <= 0;
    }
    /* the chain: each certificate's issuer found among those given and those trusted */
    {
      Cert *tc = (Cert *)calloc(MAXC, sizeof(Cert));
      int ntc = 0, cur = signer, depth, bad = 0, trusted_hit = 0, k;
      Cert *chain_cur = sc;
      if (!tc) ntrusted = 0;
      for (k = 0; k < ntrusted && ntc < MAXC; k++) if (cert_parse(trusted[k], tlen[k], &tc[ntc])) ntc++;
      for (k = 0; k < ntc; k++) if (same_der(&tc[k].der, &sc->der)) trusted_hit = 1;
      r->chain = CHAIN_INCOMPLETE;
      r->chain_len = 1;
      for (depth = 0; depth < 8 && !trusted_hit; depth++) {
        Cert *iss = NULL;
        int self = same_der(&chain_cur->issuer, &chain_cur->subject), from_trusted = 0;
        for (k = 0; k < ntc && !iss; k++) if (same_der(&tc[k].subject, &chain_cur->issuer) && cert_signed_by(chain_cur, &tc[k]) == 1) { iss = &tc[k]; from_trusted = 1; }
        for (k = 0; k < nc && !iss; k++) if (k != cur && same_der(&certs[k].subject, &chain_cur->issuer)) { iss = &certs[k]; cur = k; }
        if (!iss) {
          if (self) r->chain = cert_signed_by(chain_cur, chain_cur) == 1 ? CHAIN_SELF : CHAIN_BAD;
          break;
        }
        if (cert_signed_by(chain_cur, iss) != 1) { bad = 1; break; }
        r->chain_len++;
        if (from_trusted) { trusted_hit = 1; break; }
        for (k = 0; k < ntc; k++) if (same_der(&tc[k].der, &iss->der)) trusted_hit = 1;
        chain_cur = iss;
      }
      if (bad) r->chain = CHAIN_BAD;
      else if (trusted_hit) r->chain = CHAIN_TRUSTED;
      free(tc);
    }
  }
  free(certs);
  return 1;
}
static int buf_cmp(const Buf *a, const Buf *b)
{
  size_t n = a->len < b->len ? a->len : b->len;
  int c = memcmp(a->data, b->data, n);
  return c ? c : a->len < b->len ? -1 : a->len > b->len;
}
u8 *cms_sign(const PrivKey *k, const u8 *const *certs, const size_t *clen, int ncerts, int hash, const u8 *data, size_t dlen,
             const char *when, size_t *outlen)
{
  Buf at[3], attrs, t, sd, si, out, tmp;
  Cert sc;
  u8 h[64], ah[64], sig[600];
  int i, sl, nat = 0;
  u8 *res = NULL;
  if (ncerts < 1 || !cert_parse(certs[0], clen[0], &sc) || !hash_len(hash)) return NULL;
  hash_of(hash, data, dlen, h);
  for (i = 0; i < 3; i++) buf_init(&at[i]);
  buf_init(&attrs); buf_init(&t); buf_init(&sd); buf_init(&si); buf_init(&out); buf_init(&tmp);
  /* contentType, signingTime, messageDigest: each an Attribute, sorted (DER's SET OF) */
  dput_oid(&t, OID_CTYPE); dput_oid(&tmp, OID_DATA); dwrap(&t, 0x31, &tmp); dwrap(&at[nat++], 0x30, &t);
  if (when && strlen(when) >= 14) {
    char ut[16];
    snprintf(ut, sizeof ut, "%.12sZ", when + 2);
    dput_oid(&t, OID_SIGNTIME); dput(&tmp, 0x17, ut, 13); dwrap(&t, 0x31, &tmp); dwrap(&at[nat++], 0x30, &t);
  }
  dput_oid(&t, OID_MSGDIGEST); dput(&tmp, 0x04, h, (size_t)hash_len(hash)); dwrap(&t, 0x31, &tmp); dwrap(&at[nat++], 0x30, &t);
  {
    int a, b;
    for (a = 0; a < nat; a++) for (b = a + 1; b < nat; b++) if (buf_cmp(&at[a], &at[b]) > 0) { Buf x = at[a]; at[a] = at[b]; at[b] = x; }
  }
  for (i = 0; i < nat; i++) { buf_add(&attrs, at[i].data, at[i].len); buf_free(&at[i]); }
  /* the signature over the attributes as a SET */
  dput(&tmp, 0x31, attrs.data, attrs.len);
  if (tmp.oom) goto done;
  hash_of(hash, (const u8 *)tmp.data, tmp.len, ah);
  buf_free(&tmp); buf_init(&tmp);
  if (!(sl = pk_sign(k, hash, ah, sig, sizeof sig))) goto done;
  /* SignerInfo */
  dput_uint(&si, (const u8 *)"\1", 1);
  {
    Buf s2; buf_init(&s2);
    buf_add(&s2, sc.issuer.p, sc.issuer.n);
    dput(&s2, 0x02, sc.serial.p, sc.serial.n);
    dwrap(&si, 0x30, &s2);
  }
  dput_oid(&t, digest_oid_hex(hash)); dput(&t, 0x05, NULL, 0); dwrap(&si, 0x30, &t);
  dput(&si, 0xA0, attrs.data, attrs.len);
  if (k->pub.type == PK_RSA) { dput_oid(&t, OID_RSA); dput(&t, 0x05, NULL, 0); }
  else dput_oid(&t, hash == HASH_SHA384 ? "2a8648ce3d040303" : hash == HASH_SHA512 ? "2a8648ce3d040304" : "2a8648ce3d040302");
  dwrap(&si, 0x30, &t);
  dput(&si, 0x04, sig, (size_t)sl);
  /* SignedData */
  dput_uint(&sd, (const u8 *)"\1", 1);
  dput_oid(&t, digest_oid_hex(hash)); dput(&t, 0x05, NULL, 0); dwrap(&tmp, 0x30, &t); dwrap(&sd, 0x31, &tmp);
  dput_oid(&t, OID_DATA); dwrap(&sd, 0x30, &t);
  for (i = 0; i < ncerts; i++) buf_add(&t, certs[i], clen[i]);
  dwrap(&sd, 0xA0, &t);
  dwrap(&tmp, 0x30, &si); dwrap(&sd, 0x31, &tmp);
  /* ContentInfo */
  dput_oid(&t, OID_SIGNED);
  dwrap(&tmp, 0x30, &sd); dwrap(&t, 0xA0, &tmp);
  dwrap(&out, 0x30, &t);
  if (!out.oom) { *outlen = out.len; res = (u8 *)buf_take(&out); }
done:
  for (i = 0; i < 3; i++) buf_free(&at[i]);
  buf_free(&attrs); buf_free(&t); buf_free(&sd); buf_free(&si); buf_free(&out); buf_free(&tmp);
  return res;
}

/* ================================================================== the signer's file */
static void put_bytes(u8 *dst, int cap, int *len, Der v) { int_bytes(&v); if (v.n > (size_t)cap) v.n = (size_t)cap; memcpy(dst, v.p, v.n); *len = (int)v.n; }
/* RSAPrivateKey (PKCS #1) */
static int rsa_key(Der in, PrivKey *k)
{
  Der s, v[9];
  int i, l;
  if (!der_expect(&in, 0x30, &s)) return 0;
  for (i = 0; i < 9; i++) if (!der_expect(&s, 0x02, &v[i])) return 0;
  k->pub.type = PK_RSA;
  put_bytes(k->pub.n, sizeof k->pub.n, &k->pub.nlen, v[1]);
  put_bytes(k->pub.e, sizeof k->pub.e, &k->pub.elen, v[2]);
  put_bytes(k->d, sizeof k->d, &k->dlen, v[3]);
  /* the CRT parts, each as long as p */
  {
    Der p = v[4];
    int_bytes(&p);
    l = (int)p.n;
    if (l > 0 && l <= (int)sizeof k->p) {
      for (i = 0; i < 5; i++) {
        Der x = v[4 + i];
        u8 *dst = i == 0 ? k->p : i == 1 ? k->q : i == 2 ? k->dp : i == 3 ? k->dq : k->qi;
        int_bytes(&x);
        if ((int)x.n > l) { k->plen = 0; return 1; }
        memset(dst, 0, (size_t)l);
        memcpy(dst + l - (int)x.n, x.p, x.n);
      }
      k->plen = l;
    }
  }
  return 1;
}
/* ECPrivateKey (SEC 1); curve given by the algorithm, or inside */
static int ec_key(Der in, int curve, PrivKey *k)
{
  Der s, d, w, c;
  int t;
  if (!der_expect(&in, 0x30, &s) || !der_skip(&s) || !der_expect(&s, 0x04, &d)) return 0;
  k->pub.type = PK_EC;
  while (s.n && der_next(&s, &t, &c, &w)) {
    Der o, b;
    if (t == 0xA0 && der_expect(&c, 0x06, &o)) curve = oid_is(&o, OID_P256) ? EC_P256 : oid_is(&o, OID_P384) ? EC_P384 : EC_NONE;
    if (t == 0xA1 && der_expect(&c, 0x03, &b) && curve && b.n == (size_t)(2 + 2 * curves[curve].len) && b.p[1] == 4) {
      int len = curves[curve].len;
      memcpy(k->pub.n, b.p + 2, (size_t)len); memcpy(k->pub.y, b.p + 2 + len, (size_t)len); k->pub.nlen = k->pub.ylen = len;
    }
  }
  if (!curve || d.n > 66) return 0;
  k->pub.curve = curve;
  memcpy(k->d, d.p, d.n); k->dlen = (int)d.n;
  if (!k->pub.nlen && !ec_public(k)) return 0;
  return 1;
}
/* PrivateKeyInfo (PKCS #8) */
static int pkcs8(Der in, PrivKey *k)
{
  Der s, alg, oid, par, key;
  if (!der_expect(&in, 0x30, &s) || !der_skip(&s) || !der_expect(&s, 0x30, &alg) || !der_expect(&alg, 0x06, &oid) || !der_expect(&s, 0x04, &key)) return 0;
  if (oid_is(&oid, OID_RSA)) return rsa_key(key, k);
  if (oid_is(&oid, OID_ECPUB)) {
    int curve = EC_NONE;
    if (der_expect(&alg, 0x06, &par)) curve = oid_is(&par, OID_P256) ? EC_P256 : oid_is(&par, OID_P384) ? EC_P384 : EC_NONE;
    return ec_key(key, curve, k);
  }
  return 0;
}
/* data encrypted with PBES2 (PBKDF2 and AES-CBC) decrypted: malloc'd, NULL if it can't be */
static u8 *pbes2(Der alg, const u8 *enc, size_t elen, const char *pw, size_t *outlen, char *err, int ecap)
{
  Der oid, par, kdf, kdfo, kdfp, salt, it, sch, scho, iv, w, prf;
  long iter = 0;
  int keylen = 0, prfh = HASH_SHA1, t;
  size_t i;
  u8 key[32], *in, *out;
  if (!der_expect(&alg, 0x06, &oid)) return NULL;
  if (!oid_is(&oid, "2a864886f70d01050d")) { snprintf(err, (size_t)ecap, "encrypted with an older method Folio can't read: export it again with AES-256 (openssl pkcs12 -export -keypbe AES-256-CBC -certpbe AES-256-CBC)"); return NULL; }
  if (!der_expect(&alg, 0x30, &par) || !der_expect(&par, 0x30, &kdf) || !der_expect(&kdf, 0x06, &kdfo) || !oid_is(&kdfo, "2a864886f70d01050c") ||
      !der_expect(&kdf, 0x30, &kdfp) || !der_expect(&kdfp, 0x04, &salt) || !der_expect(&kdfp, 0x02, &it)) return NULL;
  for (i = 0; i < it.n && i < 4; i++) iter = (iter << 8) | it.p[i];
  if (der_peek(&kdfp) == 0x02) { Der kl; der_expect(&kdfp, 0x02, &kl); for (i = 0; i < kl.n; i++) keylen = (keylen << 8) | kl.p[i]; }
  if (der_peek(&kdfp) == 0x30 && der_expect(&kdfp, 0x30, &prf) && der_next(&prf, &t, &w, &kdfo)) {
    if (oid_is(&w, "2a864886f70d0209")) prfh = HASH_SHA256;
    else if (oid_is(&w, "2a864886f70d020a")) prfh = HASH_SHA384;
    else if (oid_is(&w, "2a864886f70d020b")) prfh = HASH_SHA512;
  }
  if (!der_expect(&par, 0x30, &sch) || !der_expect(&sch, 0x06, &scho) || !der_expect(&sch, 0x04, &iv) || iv.n != 16) return NULL;
  if (oid_is(&scho, "608648016503040102")) keylen = 16;
  else if (oid_is(&scho, "608648016503040116")) keylen = 24;
  else if (oid_is(&scho, "60864801650304012a")) keylen = 32;
  else { snprintf(err, (size_t)ecap, "a cipher Folio can't read (AES-CBC only)"); return NULL; }
  if (iter < 1 || iter > 10000000L || !pbkdf2(prfh, (const u8 *)pw, strlen(pw), salt.p, salt.n, iter, key, keylen)) return NULL;
  if (!(in = (u8 *)malloc(elen + 16))) return NULL;
  memcpy(in, iv.p, 16); memcpy(in + 16, enc, elen);
  out = aes_cbc_decrypt(key, keylen, in, elen + 16, outlen);
  free(in);
  return out;
}
static int add_cert(SignerId *s, const u8 *p, size_t n)
{
  Cert c;
  if (s->ncerts >= 8 || !cert_parse(p, n, &c) || !(s->certs[s->ncerts] = (u8 *)malloc(c.der.n))) return 0;
  memcpy(s->certs[s->ncerts], c.der.p, c.der.n);
  s->clen[s->ncerts++] = c.der.n;
  return 1;
}
static int safe_contents(SignerId *s, Der in, const char *pw, int *keys);
static int safe_bag(SignerId *s, Der bag, const char *pw, int *keys)
{
  Der oid, val, w;
  int t;
  if (!der_expect(&bag, 0x06, &oid) || !der_expect(&bag, 0xA0, &val)) return 0;
  if (oid_is(&oid, "2a864886f70d010c0a0101")) { if (pkcs8(val, &s->key)) (*keys)++; }
  else if (oid_is(&oid, "2a864886f70d010c0a0102")) {
    Der e, alg, enc;
    u8 *pl;
    size_t pn;
    if (!der_expect(&val, 0x30, &e) || !der_expect(&e, 0x30, &alg) || !der_expect(&e, 0x04, &enc)) return 0;
    if (!(pl = pbes2(alg, enc.p, enc.n, pw, &pn, s->err, sizeof s->err))) return 0;
    { Der k; k.p = pl; k.n = pn; if (pkcs8(k, &s->key)) (*keys)++; else if (!s->err[0]) snprintf(s->err, sizeof s->err, "wrong password, or a key Folio can't read"); }
    memset(pl, 0, pn); free(pl);
  } else if (oid_is(&oid, "2a864886f70d010c0a0103")) {
    Der cb, co, cw, cv;
    if (der_expect(&val, 0x30, &cb) && der_expect(&cb, 0x06, &co) && der_expect(&cb, 0xA0, &cw) && der_next(&cw, &t, &cv, &w) && (t == 0x04 || t == 0x24)) {
      Buf ob; Der oc;
      buf_init(&ob);
      if (octets(t, &cv, &ob, &oc)) add_cert(s, oc.p, oc.n);
      buf_free(&ob);
    }
  } else if (oid_is(&oid, "2a864886f70d010c0a0106")) {
    Der sc;
    if (der_expect(&val, 0x30, &sc)) { Der whole; whole.p = sc.p - 0; whole.n = sc.n; safe_contents(s, whole, pw, keys); }
  }
  return 1;
}
static int safe_contents(SignerId *s, Der in, const char *pw, int *keys)
{
  Der seq, bag;
  if (!der_expect(&in, 0x30, &seq)) {
    /* (called with the SEQUENCE's contents already) */
    seq = in;
  }
  while (seq.n && der_expect(&seq, 0x30, &bag)) safe_bag(s, bag, pw, keys);
  return 1;
}
static int pkcs12(SignerId *s, const u8 *data, size_t n, const char *pw)
{
  Der in, pfx, ci, oid, ex, w, as, item;
  Buf ob;
  int t, keys = 0;
  in.p = data; in.n = n;
  if (!der_expect(&in, 0x30, &pfx) || !der_skip(&pfx) || !der_expect(&pfx, 0x30, &ci) || !der_expect(&ci, 0x06, &oid) || !oid_is(&oid, OID_DATA) ||
      !der_expect(&ci, 0xA0, &ex)) return 0;
  buf_init(&ob);
  {
    Der c2, oc;
    if (!der_next(&ex, &t, &c2, &w) || (t != 0x04 && t != 0x24) || !octets(t, &c2, &ob, &oc)) { buf_free(&ob); return 0; }
    in = oc;
  }
  if (!der_expect(&in, 0x30, &as)) { buf_free(&ob); return 0; }
  while (as.n && der_expect(&as, 0x30, &item)) {
    Der io, iw;
    if (!der_expect(&item, 0x06, &io) || !der_expect(&item, 0xA0, &iw)) continue;
    if (oid_is(&io, OID_DATA)) {
      Der c2, oc;
      Buf b2;
      buf_init(&b2);
      if (der_next(&iw, &t, &c2, &w) && (t == 0x04 || t == 0x24) && octets(t, &c2, &b2, &oc)) safe_contents(s, oc, pw, &keys);
      buf_free(&b2);
    } else if (oid_is(&io, "2a864886f70d010706")) {
      Der ed, eci, ct, alg, enc;
      if (der_expect(&iw, 0x30, &ed) && der_skip(&ed) && der_expect(&ed, 0x30, &eci) && der_expect(&eci, 0x06, &ct) && der_expect(&eci, 0x30, &alg) &&
          der_next(&eci, &t, &enc, &w) && (t == 0x80 || t == 0xA0)) {
        Buf b2; Der oc;
        u8 *pl;
        size_t pn;
        buf_init(&b2);
        if (octets(t == 0xA0 ? 0x24 : 0x04, &enc, &b2, &oc) && (pl = pbes2(alg, oc.p, oc.n, pw, &pn, s->err, sizeof s->err))) {
          Der d2; d2.p = pl; d2.n = pn;
          safe_contents(s, d2, pw, &keys);
          free(pl);
        }
        buf_free(&b2);
      }
    }
  }
  buf_free(&ob);
  return keys > 0;
}
static int b64v(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1; }
/* the PEM blocks of a text file */
static int pem(SignerId *s, const u8 *data, size_t n, const char *pw)
{
  const char *p = (const char *)data, *end = p + n;
  int keys = 0;
  while (p < end) {
    const char *b = NULL, *q;
    char label[40];
    size_t i;
    for (q = p; q + 11 <= end; q++) if (!memcmp(q, "-----BEGIN ", 11)) { b = q; break; }
    if (!b) break;
    q = b + 11;
    for (i = 0; q < end && *q != '-' && i < sizeof label - 1; q++) label[i++] = *q;
    label[i] = 0;
    while (q < end && *q != '\n') q++;
    {
      Buf raw;
      int acc = 0, bits = 0, encrypted = 0;
      const char *e = q;
      buf_init(&raw);
      for (; e < end && !(e + 9 <= end && !memcmp(e, "-----END ", 9)); e++) {
        int v = b64v((u8)*e);
        if (*e == ':') encrypted = 1;                /* (an old-style "Proc-Type: 4,ENCRYPTED" header) */
        if (v < 0) continue;
        acc = ((acc << 6) | v) & 0xFFFFFF; bits += 6;
        if (bits >= 8) { bits -= 8; buf_addc(&raw, (acc >> bits) & 255); }
      }
      if (!raw.oom && raw.len) {
        Der d; d.p = (const u8 *)raw.data; d.n = raw.len;
        if (!strcmp(label, "CERTIFICATE")) add_cert(s, d.p, d.n);
        else if (encrypted) snprintf(s->err, sizeof s->err, "an old-style encrypted key: convert it with openssl pkcs8 -topk8 -v2 aes-256-cbc");
        else if (!strcmp(label, "PRIVATE KEY")) keys += pkcs8(d, &s->key);
        else if (!strcmp(label, "RSA PRIVATE KEY")) keys += rsa_key(d, &s->key);
        else if (!strcmp(label, "EC PRIVATE KEY")) keys += ec_key(d, EC_NONE, &s->key);
        else if (!strcmp(label, "ENCRYPTED PRIVATE KEY")) {
          Der e2, alg, enc;
          u8 *pl;
          size_t pn;
          if (der_expect(&d, 0x30, &e2) && der_expect(&e2, 0x30, &alg) && der_expect(&e2, 0x04, &enc) &&
              (pl = pbes2(alg, enc.p, enc.n, pw, &pn, s->err, sizeof s->err))) {
            Der k; k.p = pl; k.n = pn;
            if (pkcs8(k, &s->key)) keys++; else if (!s->err[0]) snprintf(s->err, sizeof s->err, "wrong password");
            memset(pl, 0, pn); free(pl);
          }
        }
      }
      buf_free(&raw);
      p = e + 9;
    }
  }
  return keys > 0;
}
static int same_key(const PubKey *a, const PubKey *b)
{
  return a->type == b->type && a->nlen == b->nlen && !memcmp(a->n, b->n, (size_t)a->nlen) && (a->type != PK_EC || (a->ylen == b->ylen && !memcmp(a->y, b->y, (size_t)a->ylen)));
}
int signer_load(const u8 *data, size_t n, const char *password, SignerId *s)
{
  int ok, i;
  memset(s, 0, sizeof *s);
  if (!password) password = "";
  ok = n > 10 && data[0] == 0x30 ? pkcs12(s, data, n, password) : pem(s, data, n, password);
  if (!ok) { if (!s->err[0]) snprintf(s->err, sizeof s->err, "no private key found (wrong password?)"); signer_free(s); return 0; }
  /* the key's own certificate first */
  for (i = 0; i < s->ncerts; i++) {
    Cert c;
    if (cert_parse(s->certs[i], s->clen[i], &c) && same_key(&c.key, &s->key.pub)) {
      u8 *t = s->certs[0]; size_t tl = s->clen[0];
      s->certs[0] = s->certs[i]; s->clen[0] = s->clen[i]; s->certs[i] = t; s->clen[i] = tl;
      break;
    }
  }
  if (i == s->ncerts) { snprintf(s->err, sizeof s->err, "no certificate for the private key in the file"); signer_free(s); return 0; }
  return 1;
}
void signer_free(SignerId *s)
{
  int i;
  for (i = 0; i < s->ncerts; i++) free(s->certs[i]);
  s->ncerts = 0;
  memset(&s->key, 0, sizeof s->key);
}
int pki_read_certs(const u8 *data, size_t n, SignerId *s)
{
  memset(s, 0, sizeof *s);
  if (n > 4 && data[0] == 0x30) { add_cert(s, data, n); return s->ncerts; }
  pem(s, data, n, "");
  memset(&s->key, 0, sizeof s->key);
  return s->ncerts;
}
