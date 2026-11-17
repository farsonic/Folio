/*
 * crypto.c - MD5, RC4, AES and SHA-2 for the PDF Standard Security Handler (see crypto.h).
 *
 * AES uses the usual 32-bit T-tables, built from the S-box on first use (the S-box itself is a const table), so a
 * block costs 16 table lookups a round on the 68040. SHA-384/512 use unsigned long long, which vbcc supports.
 * Message schedules are kept as rolling 16-word windows to spare the Amiga's small stack.
 */
#include <stdlib.h>
#include <string.h>
#include "crypto.h"

#define ROL32(x, k) (((x) << (k)) | ((x) >> (32 - (k))))
#define ROR32(x, k) (((x) >> (k)) | ((x) << (32 - (k))))

/* a big-endian 32-bit word from 4 bytes, and back */
static u32 get_be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

/* ------------------------------------------------------------------ */
/* MD5 (RFC 1321) */
static const u32 md5_k[64] = {
  0xd76aa478UL, 0xe8c7b756UL, 0x242070dbUL, 0xc1bdceeeUL, 0xf57c0fafUL, 0x4787c62aUL, 0xa8304613UL, 0xfd469501UL,
  0x698098d8UL, 0x8b44f7afUL, 0xffff5bb1UL, 0x895cd7beUL, 0x6b901122UL, 0xfd987193UL, 0xa679438eUL, 0x49b40821UL,
  0xf61e2562UL, 0xc040b340UL, 0x265e5a51UL, 0xe9b6c7aaUL, 0xd62f105dUL, 0x02441453UL, 0xd8a1e681UL, 0xe7d3fbc8UL,
  0x21e1cde6UL, 0xc33707d6UL, 0xf4d50d87UL, 0x455a14edUL, 0xa9e3e905UL, 0xfcefa3f8UL, 0x676f02d9UL, 0x8d2a4c8aUL,
  0xfffa3942UL, 0x8771f681UL, 0x6d9d6122UL, 0xfde5380cUL, 0xa4beea44UL, 0x4bdecfa9UL, 0xf6bb4b60UL, 0xbebfbc70UL,
  0x289b7ec6UL, 0xeaa127faUL, 0xd4ef3085UL, 0x04881d05UL, 0xd9d4d039UL, 0xe6db99e5UL, 0x1fa27cf8UL, 0xc4ac5665UL,
  0xf4292244UL, 0x432aff97UL, 0xab9423a7UL, 0xfc93a039UL, 0x655b59c3UL, 0x8f0ccc92UL, 0xffeff47dUL, 0x85845dd1UL,
  0x6fa87e4fUL, 0xfe2ce6e0UL, 0xa3014314UL, 0x4e0811a1UL, 0xf7537e82UL, 0xbd3af235UL, 0x2ad7d2bbUL, 0xeb86d391UL };
static const u8 md5_r[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9,
                              14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15,
                              21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };

/* process one 64-byte block */
static void md5_block(u32 s[4], const u8 *p)
{
  u32 x[16], a = s[0], b = s[1], c = s[2], d = s[3], f, t;
  int i, g;
  for (i = 0; i < 16; i++) x[i] = (u32)p[4 * i] | ((u32)p[4 * i + 1] << 8) | ((u32)p[4 * i + 2] << 16) | ((u32)p[4 * i + 3] << 24);
  for (i = 0; i < 64; i++) {
    if (i < 16) { f = (b & c) | (~b & d); g = i; }
    else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
    else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
    else { f = c ^ (b | ~d); g = (7 * i) & 15; }
    t = a + f + md5_k[i] + x[g];
    a = d; d = c; c = b;
    b += ROL32(t, md5_r[i]);
  }
  s[0] += a; s[1] += b; s[2] += c; s[3] += d;
}
void md5_init(Md5 *m)
{
  m->s[0] = 0x67452301UL; m->s[1] = 0xefcdab89UL; m->s[2] = 0x98badcfeUL; m->s[3] = 0x10325476UL;
  m->lo = m->hi = 0;
}
void md5_update(Md5 *m, const u8 *in, size_t n)
{
  size_t have = m->lo & 63, k;
  u32 old = m->lo;
  m->lo += (u32)n;
  if (m->lo < old) m->hi++;
  if (sizeof(size_t) > 4) m->hi += (u32)(((n >> 16) >> 16) & 0xFFFFFFFFUL);
  if (have) {
    k = 64 - have < n ? 64 - have : n;
    memcpy(m->buf + have, in, k);
    in += k; n -= k; have += k;
    if (have < 64) return;
    md5_block(m->s, m->buf);
  }
  for (; n >= 64; in += 64, n -= 64) md5_block(m->s, in);
  if (n) memcpy(m->buf, in, n);
}
void md5_final(Md5 *m, u8 out[16])
{
  u8 pad[72];
  u32 lo = m->lo << 3, hi = (m->hi << 3) | (m->lo >> 29);
  size_t have = m->lo & 63, padn = have < 56 ? 56 - have : 120 - have;
  int i;
  memset(pad, 0, sizeof pad);
  pad[0] = 0x80;
  for (i = 0; i < 4; i++) { pad[padn + (size_t)i] = (u8)(lo >> (8 * i)); pad[padn + 4 + (size_t)i] = (u8)(hi >> (8 * i)); }
  md5_update(m, pad, padn + 8);
  for (i = 0; i < 16; i++) out[i] = (u8)(m->s[i >> 2] >> (8 * (i & 3)));
}
void md5(const u8 *in, size_t n, u8 out[16])
{
  Md5 m;
  md5_init(&m); md5_update(&m, in, n); md5_final(&m, out);
}

/* ------------------------------------------------------------------ */
/* RC4 */
void rc4_init(Rc4 *r, const u8 *key, int keylen)
{
  static const u8 zero = 0;
  int i, j = 0;
  u8 t;
  if (keylen <= 0) { key = &zero; keylen = 1; }
  if (keylen > 256) keylen = 256;
  for (i = 0; i < 256; i++) r->s[i] = (u8)i;
  for (i = 0; i < 256; i++) {
    j = (j + r->s[i] + key[i % keylen]) & 255;
    t = r->s[i]; r->s[i] = r->s[j]; r->s[j] = t;
  }
  r->i = r->j = 0;
}
void rc4_crypt(Rc4 *r, const u8 *in, u8 *out, size_t n)
{
  u8 i = r->i, j = r->j, t, *s = r->s;
  size_t k;
  for (k = 0; k < n; k++) {
    i = (u8)(i + 1); j = (u8)(j + s[i]);
    t = s[i]; s[i] = s[j]; s[j] = t;
    out[k] = in[k] ^ s[(u8)(s[i] + s[j])];
  }
  r->i = i; r->j = j;
}

/* ------------------------------------------------------------------ */
/* AES (FIPS-197) */
static const u8 aes_sbox[256] = {
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa,
  0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5,
  0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2,
  0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
  0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45,
  0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
  0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f,
  0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
  0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65,
  0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e,
  0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e,
  0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
  0xb0, 0x54, 0xbb, 0x16 };
static u8 aes_isbox[256];
static u32 aes_te[256], aes_td[256];   /* one table each way; the other three are its byte rotations */
static int aes_ready;

/* multiply in GF(2^8) modulo the AES polynomial */
static u32 gmul(u32 a, u32 b)
{
  u32 r = 0;
  while (b) {
    if (b & 1) r ^= a;
    a <<= 1;
    if (a & 0x100) a ^= 0x11b;
    b >>= 1;
  }
  return r;
}
/* build the inverse S-box and the T-tables (once) */
static void aes_tables(void)
{
  int i;
  if (aes_ready) return;
  for (i = 0; i < 256; i++) aes_isbox[aes_sbox[i]] = (u8)i;
  for (i = 0; i < 256; i++) {
    u32 s = aes_sbox[i], t = aes_isbox[i];
    aes_te[i] = (gmul(s, 2) << 24) | (s << 16) | (s << 8) | gmul(s, 3);
    aes_td[i] = (gmul(t, 14) << 24) | (gmul(t, 9) << 16) | (gmul(t, 13) << 8) | gmul(t, 11);
  }
  aes_ready = 1;
}

typedef struct { u32 rk[60]; int nr; } AesKey;

/* expand a key for encryption; 0 if the length is not 16, 24 or 32 */
static int aes_key_enc(AesKey *k, const u8 *key, int keylen)
{
  int nk = keylen / 4, i, total;
  u32 rcon = 1;
  if (keylen != 16 && keylen != 24 && keylen != 32) return 0;
  aes_tables();
  k->nr = nk + 6;
  total = 4 * (k->nr + 1);
  for (i = 0; i < nk; i++) k->rk[i] = get_be32(key + 4 * i);
  for (i = nk; i < total; i++) {
    u32 t = k->rk[i - 1];
    if (i % nk == 0) {
      t = ((u32)aes_sbox[(t >> 16) & 255] << 24) | ((u32)aes_sbox[(t >> 8) & 255] << 16) | ((u32)aes_sbox[t & 255] << 8) |
          aes_sbox[t >> 24];
      t ^= rcon << 24;
      rcon = gmul(rcon, 2);
    } else if (nk > 6 && i % nk == 4) {
      t = ((u32)aes_sbox[t >> 24] << 24) | ((u32)aes_sbox[(t >> 16) & 255] << 16) | ((u32)aes_sbox[(t >> 8) & 255] << 8) |
          aes_sbox[t & 255];
    }
    k->rk[i] = k->rk[i - nk] ^ t;
  }
  return 1;
}
/* expand a key for decryption: the round keys reversed, the inner ones through InvMixColumns */
static int aes_key_dec(AesKey *k, const u8 *key, int keylen)
{
  AesKey e;
  int r, j;
  if (!aes_key_enc(&e, key, keylen)) return 0;
  k->nr = e.nr;
  for (r = 0; r <= e.nr; r++)
    for (j = 0; j < 4; j++) {
      u32 w = e.rk[4 * (e.nr - r) + j];
      if (r > 0 && r < e.nr)
        w = aes_td[aes_sbox[w >> 24]] ^ ROR32(aes_td[aes_sbox[(w >> 16) & 255]], 8) ^
            ROR32(aes_td[aes_sbox[(w >> 8) & 255]], 16) ^ ROR32(aes_td[aes_sbox[w & 255]], 24);
      k->rk[4 * r + j] = w;
    }
  return 1;
}
#define TE(x, r) ROR32(aes_te[x], r)
#define TD(x, r) ROR32(aes_td[x], r)
/* encrypt one block */
static void aes_enc(const AesKey *k, const u8 in[16], u8 out[16])
{
  const u32 *rk = k->rk;
  u32 s0 = get_be32(in) ^ rk[0], s1 = get_be32(in + 4) ^ rk[1], s2 = get_be32(in + 8) ^ rk[2], s3 = get_be32(in + 12) ^ rk[3];
  u32 t0, t1, t2, t3;
  int r;
  for (r = 1; r < k->nr; r++) {
    rk += 4;
    t0 = aes_te[s0 >> 24] ^ TE((s1 >> 16) & 255, 8) ^ TE((s2 >> 8) & 255, 16) ^ TE(s3 & 255, 24) ^ rk[0];
    t1 = aes_te[s1 >> 24] ^ TE((s2 >> 16) & 255, 8) ^ TE((s3 >> 8) & 255, 16) ^ TE(s0 & 255, 24) ^ rk[1];
    t2 = aes_te[s2 >> 24] ^ TE((s3 >> 16) & 255, 8) ^ TE((s0 >> 8) & 255, 16) ^ TE(s1 & 255, 24) ^ rk[2];
    t3 = aes_te[s3 >> 24] ^ TE((s0 >> 16) & 255, 8) ^ TE((s1 >> 8) & 255, 16) ^ TE(s2 & 255, 24) ^ rk[3];
    s0 = t0; s1 = t1; s2 = t2; s3 = t3;
  }
  rk += 4;
#define FIN(a, b, c, d, w) (((u32)aes_sbox[(a) >> 24] << 24) | ((u32)aes_sbox[((b) >> 16) & 255] << 16) | \
                            ((u32)aes_sbox[((c) >> 8) & 255] << 8) | aes_sbox[(d) & 255]) ^ (w)
  put_be32(out, FIN(s0, s1, s2, s3, rk[0]));
  put_be32(out + 4, FIN(s1, s2, s3, s0, rk[1]));
  put_be32(out + 8, FIN(s2, s3, s0, s1, rk[2]));
  put_be32(out + 12, FIN(s3, s0, s1, s2, rk[3]));
#undef FIN
}
/* decrypt one block (with a decryption key schedule) */
static void aes_dec(const AesKey *k, const u8 in[16], u8 out[16])
{
  const u32 *rk = k->rk;
  u32 s0 = get_be32(in) ^ rk[0], s1 = get_be32(in + 4) ^ rk[1], s2 = get_be32(in + 8) ^ rk[2], s3 = get_be32(in + 12) ^ rk[3];
  u32 t0, t1, t2, t3;
  int r;
  for (r = 1; r < k->nr; r++) {
    rk += 4;
    t0 = aes_td[s0 >> 24] ^ TD((s3 >> 16) & 255, 8) ^ TD((s2 >> 8) & 255, 16) ^ TD(s1 & 255, 24) ^ rk[0];
    t1 = aes_td[s1 >> 24] ^ TD((s0 >> 16) & 255, 8) ^ TD((s3 >> 8) & 255, 16) ^ TD(s2 & 255, 24) ^ rk[1];
    t2 = aes_td[s2 >> 24] ^ TD((s1 >> 16) & 255, 8) ^ TD((s0 >> 8) & 255, 16) ^ TD(s3 & 255, 24) ^ rk[2];
    t3 = aes_td[s3 >> 24] ^ TD((s2 >> 16) & 255, 8) ^ TD((s1 >> 8) & 255, 16) ^ TD(s0 & 255, 24) ^ rk[3];
    s0 = t0; s1 = t1; s2 = t2; s3 = t3;
  }
  rk += 4;
#define FIN(a, b, c, d, w) (((u32)aes_isbox[(a) >> 24] << 24) | ((u32)aes_isbox[((b) >> 16) & 255] << 16) | \
                            ((u32)aes_isbox[((c) >> 8) & 255] << 8) | aes_isbox[(d) & 255]) ^ (w)
  put_be32(out, FIN(s0, s3, s2, s1, rk[0]));
  put_be32(out + 4, FIN(s1, s0, s3, s2, rk[1]));
  put_be32(out + 8, FIN(s2, s1, s0, s3, rk[2]));
  put_be32(out + 12, FIN(s3, s2, s1, s0, rk[3]));
#undef FIN
}

void aes_encrypt_block(const u8 *key, int keylen, const u8 in[16], u8 out[16])
{
  AesKey k;
  if (aes_key_enc(&k, key, keylen)) aes_enc(&k, in, out);
  else memset(out, 0, 16);
}
void aes_ecb_decrypt_block(const u8 *key, int keylen, const u8 in[16], u8 out[16])
{
  AesKey k;
  if (aes_key_dec(&k, key, keylen)) aes_dec(&k, in, out);
  else memset(out, 0, 16);
}
u8 *aes_cbc_encrypt_nopad(const u8 *key, int keylen, const u8 iv[16], const u8 *in, size_t n)
{
  AesKey k;
  u8 *out, x[16];
  const u8 *prev = iv;
  size_t b, i;
  n &= ~(size_t)15;
  if (!aes_key_enc(&k, key, keylen) || !(out = (u8 *)malloc(n ? n : 1))) return NULL;
  for (b = 0; b < n; b += 16) {
    for (i = 0; i < 16; i++) x[i] = in[b + i] ^ prev[i];
    aes_enc(&k, x, out + b);
    prev = out + b;
  }
  return out;
}
u8 *aes_cbc_decrypt(const u8 *key, int keylen, const u8 *in, size_t n, size_t *outlen)
{
  AesKey k;
  u8 *out;
  size_t len, b, i;
  *outlen = 0;
  if (n < 16 || !aes_key_dec(&k, key, keylen)) return NULL;
  len = (n - 16) & ~(size_t)15;
  if (!(out = (u8 *)malloc(len ? len : 1))) return NULL;
  for (b = 0; b < len; b += 16) {
    aes_dec(&k, in + 16 + b, out + b);
    for (i = 0; i < 16; i++) out[b + i] ^= in[b + i];   /* the previous ciphertext block (the IV first) */
  }
  if (len) {   /* PKCS#5 padding, if it is valid */
    int p = out[len - 1], ok = p >= 1 && p <= 16;
    for (i = 1; ok && i <= (size_t)p; i++) if (out[len - i] != p) ok = 0;
    if (ok) len -= (size_t)p;
  }
  *outlen = len;
  return out;
}

/* ------------------------------------------------------------------ */
/* SHA-256 (FIPS 180-4) */
static const u32 sha256_k[64] = {
  0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
  0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
  0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
  0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
  0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
  0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
  0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
  0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL };

/* process one 64-byte block */
static void sha256_block(u32 h[8], const u8 *p)
{
  u32 w[16], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7], t1, t2;
  int i;
  for (i = 0; i < 64; i++) {
    if (i < 16) w[i] = get_be32(p + 4 * i);
    else {
      u32 x = w[(i - 15) & 15], y = w[(i - 2) & 15];
      w[i & 15] += (ROR32(x, 7) ^ ROR32(x, 18) ^ (x >> 3)) + w[(i - 7) & 15] + (ROR32(y, 17) ^ ROR32(y, 19) ^ (y >> 10));
    }
    t1 = hh + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g)) + sha256_k[i] + w[i & 15];
    t2 = (ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
void sha256(const u8 *in, size_t n, u8 out[32])
{
  u32 h[8] = { 0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL, 0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL };
  u8 last[128];
  size_t rem = n & 63, i, tail;
  for (i = 0; i + 64 <= n; i += 64) sha256_block(h, in + i);
  memset(last, 0, sizeof last);
  if (rem) memcpy(last, in + i, rem);
  last[rem] = 0x80;
  tail = rem < 56 ? 64 : 128;
  put_be32(last + tail - 8, (u32)(((n >> 16) >> 16) << 3) | (u32)(n >> 29));
  put_be32(last + tail - 4, (u32)(n << 3));
  sha256_block(h, last);
  if (tail == 128) sha256_block(h, last + 64);
  for (i = 0; i < 8; i++) put_be32(out + 4 * i, h[i]);
}

/* ------------------------------------------------------------------ */
/* SHA-512 and SHA-384 */
typedef unsigned long long u64;
#define ROR64(x, k) (((x) >> (k)) | ((x) << (64 - (k))))
static const u64 sha512_k[80] = {
  0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
  0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
  0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
  0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
  0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
  0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
  0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
  0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
  0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
  0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
  0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
  0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
  0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
  0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
  0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };

/* a big-endian 64-bit word from 8 bytes */
static u64 get_be64(const u8 *p) { return ((u64)get_be32(p) << 32) | get_be32(p + 4); }
/* process one 128-byte block */
static void sha512_block(u64 h[8], const u8 *p)
{
  u64 w[16], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7], t1, t2;
  int i;
  for (i = 0; i < 80; i++) {
    if (i < 16) w[i] = get_be64(p + 8 * i);
    else {
      u64 x = w[(i - 15) & 15], y = w[(i - 2) & 15];
      w[i & 15] += (ROR64(x, 1) ^ ROR64(x, 8) ^ (x >> 7)) + w[(i - 7) & 15] + (ROR64(y, 19) ^ ROR64(y, 61) ^ (y >> 6));
    }
    t1 = hh + (ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41)) + ((e & f) ^ (~e & g)) + sha512_k[i] + w[i & 15];
    t2 = (ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
/* SHA-512 from the initial hash h, writing the first outn bytes of the result */
static void sha512_run(u64 h[8], const u8 *in, size_t n, u8 *out, int outn)
{
  u8 last[256];
  size_t rem = n & 127, i, tail;
  int k;
  for (i = 0; i + 128 <= n; i += 128) sha512_block(h, in + i);
  memset(last, 0, sizeof last);
  if (rem) memcpy(last, in + i, rem);
  last[rem] = 0x80;
  tail = rem < 112 ? 128 : 256;
  put_be32(last + tail - 8, (u32)(((n >> 16) >> 16) << 3) | (u32)(n >> 29));
  put_be32(last + tail - 4, (u32)(n << 3));
  sha512_block(h, last);
  if (tail == 256) sha512_block(h, last + 128);
  for (k = 0; k < outn; k++) out[k] = (u8)(h[k >> 3] >> (56 - 8 * (k & 7)));
}
void sha512(const u8 *in, size_t n, u8 out[64])
{
  u64 h[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
               0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
  sha512_run(h, in, n, out, 64);
}
void sha384(const u8 *in, size_t n, u8 out[48])
{
  u64 h[8] = { 0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
               0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL };
  sha512_run(h, in, n, out, 48);
}
