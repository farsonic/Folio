/*
 * filtertest.c - tests for core/filters.c and core/crypto.c.
 *
 * Run alone, it checks round trips, edge cases, known-answer vectors, fuzzing and speed. Given a directory written by
 * tests/filtertest.py (which is how it is normally run: python3 tests/filtertest.py ./filtertest), it also checks the
 * vectors listed in its manifest.txt and writes flt_deflate's output there for Python's zlib to verify.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/util.h"
#include "core/filters.h"
#include "core/crypto.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
                           printf("\n"); } } while (0)

static u32 rng = 2463534242UL;
/* xorshift32 */
static u32 rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* hex string to bytes; returns the length */
static size_t unhex(const char *s, u8 *out)
{
  size_t n = 0;
  while (s[0] && s[1]) {
    unsigned v;
    sscanf(s, "%2x", &v);
    out[n++] = (u8)v;
    s += 2;
  }
  return n;
}
/* does buf equal the hex string? */
static int eqhex(const u8 *buf, size_t n, const char *hex)
{
  u8 t[256];
  size_t k = unhex(hex, t);
  return k == n && !memcmp(buf, t, n);
}
static double now(void) { return (double)clock() / CLOCKS_PER_SEC; }

/* ------------------------------------------------------------------ */
/* test data */
static u8 *make_text(size_t n)
{
  static const char *w[] = { "BT", "/F1", "12", "Tf", "72", "712", "Td", "(Hello, world)", "Tj", "ET", "q", "Q", "0.5", "re",
                             "f", "cm", "1", "0", "0", "1", "m", "l", "S", "stream", "endobj" };
  u8 *p = (u8 *)malloc(n + 1);
  size_t i = 0;
  while (i < n) {
    const char *s = w[rnd() % 25];
    while (*s && i < n) p[i++] = (u8)*s++;
    if (i < n) p[i++] = (rnd() % 7) ? ' ' : '\n';
    if (i + 6 < n && rnd() % 5 == 0) i += (size_t)sprintf((char *)p + i, "%u ", rnd() % 1000);
  }
  return p;
}
static u8 *make_data(int kind, size_t n)
{
  u8 *p;
  size_t i;
  if (kind == 0) return make_text(n);
  p = (u8 *)malloc(n + 1);
  for (i = 0; i < n; i++) {
    if (kind == 1) p[i] = (u8)rnd();
    else if (kind == 2) p[i] = (u8)((i / 97) & 3 ? 0 : (i / 1000));
    else if (kind == 3) p[i] = (u8)((i % 300) * (i / 300) / 7 + (rnd() % 3));
    else p[i] = 0;
  }
  return p;
}

/* ------------------------------------------------------------------ */
/* inflate / deflate */
static void test_flate_roundtrip(void)
{
  static const size_t sizes[] = { 0, 1, 2, 3, 100, 4000, 70000, 300000, 4000000 };
  int kind, lvl;
  size_t si;
  for (kind = 0; kind < 5; kind++)
    for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
      size_t n = sizes[si];
      u8 *d = make_data(kind, n);
      for (lvl = 1; lvl <= 9; lvl++) {
        size_t clen, dlen, cap;
        u8 *c, *x;
        if (n > 1000000 && lvl != 1 && lvl != 6 && lvl != 9) continue;
        c = flt_deflate(d, n, lvl, &clen);
        CHECK(c != NULL, "deflate kind %d n %lu level %d", kind, (unsigned long)n, lvl);
        if (!c) continue;
        x = flt_flate(c, clen, 0, &dlen);
        CHECK(x && dlen == n && !memcmp(x, d, n), "round trip kind %d n %lu level %d", kind, (unsigned long)n, lvl);
        free(x);
        if (lvl == 6 && n) {   /* the cap: exactly that much, the right bytes */
          cap = n / 3 + 1;
          x = flt_flate(c, clen, cap, &dlen);
          CHECK(x && dlen == cap && !memcmp(x, d, cap), "maxout kind %d n %lu", kind, (unsigned long)n);
          free(x);
        }
        if (lvl == 6 && n > 2 && kind == 1) CHECK(clen < n + n / 1000 + 64, "random data stays near its size (stored)");
        free(c);
      }
      free(d);
    }
}
static void test_flate_edges(void)
{
  static const u8 stored[] = { 0x01, 0x05, 0x00, 0xfa, 0xff, 'h', 'e', 'l', 'l', 'o' };
  static const u8 empty[] = { 0x03, 0x00 };
  static const u8 zempty[] = { 0x78, 0x9c, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01 };
  static const u8 bad[] = { 0x07, 0x12, 0x34 };   /* block type 3 */
  size_t n;
  u8 *o;
  o = flt_flate(stored, sizeof stored, 0, &n);
  CHECK(o && n == 5 && !memcmp(o, "hello", 5), "stored block");
  free(o);
  o = flt_flate(stored, sizeof stored - 2, 0, &n);
  CHECK(o && n == 3 && !memcmp(o, "hel", 3), "truncated stored block keeps what is there");
  free(o);
  o = flt_flate(stored, sizeof stored, 2, &n);
  CHECK(o && n == 2, "stored block under a cap");
  free(o);
  o = flt_flate(empty, sizeof empty, 0, &n);
  CHECK(o && n == 0, "an empty stream is a (non-NULL) empty result");
  free(o);
  o = flt_flate(zempty, sizeof zempty, 0, &n);
  CHECK(o && n == 0, "zlib-wrapped empty stream");
  free(o);
  o = flt_flate(bad, sizeof bad, 0, &n);
  CHECK(!o && n == 0, "nothing decodes: NULL");
  o = flt_flate(NULL, 0, 0, &n);
  CHECK(!o && n == 0, "no input: NULL");
  {
    /* a bomb: 10 MB of zeros, capped at 1 MB */
    u8 *z = (u8 *)calloc(10000000, 1), *c;
    size_t cl;
    c = flt_deflate(z, 10000000, 9, &cl);
    CHECK(c && cl < 20000, "zeros compress well (%lu)", (unsigned long)cl);
    o = flt_flate(c, cl, 1000000, &n);
    CHECK(o && n == 1000000, "bomb capped");
    free(o); free(c); free(z);
  }
}

/* ------------------------------------------------------------------ */
/* LZW, ASCII85, ASCIIHex, RunLength */
static void test_lzw(void)
{
  static const u8 ex[] = { 0x80, 0x0B, 0x60, 0x50, 0x22, 0x0C, 0x0C, 0x85, 0x01 };   /* the PDF reference's example */
  size_t n;
  u8 *o = flt_lzw(ex, sizeof ex, 1, 0, &n);
  CHECK(o && n == 10 && !memcmp(o, "-----A---B", 10), "LZW example");
  free(o);
  o = flt_lzw(ex, sizeof ex, 1, 4, &n);
  CHECK(o && n == 4 && !memcmp(o, "----", 4), "LZW capped");
  free(o);
  o = flt_lzw(ex, 3, 1, 0, &n);
  CHECK(o && n >= 1 && !memcmp(o, "-----A---B", n), "LZW truncated is a prefix");
  free(o);
}

/* ASCII85-encode (for round trips), with 'z' and the '~>' end */
static size_t a85_enc(const u8 *in, size_t n, u8 *out)
{
  size_t i, o = 0;
  for (i = 0; i < n; i += 4) {
    u32 v = 0;
    size_t k = n - i < 4 ? n - i : 4, j;
    char c[5];
    for (j = 0; j < 4; j++) v = (v << 8) | (j < k ? in[i + j] : 0);
    if (v == 0 && k == 4) { out[o++] = 'z'; continue; }
    for (j = 5; j-- > 0; ) { c[j] = (char)('!' + v % 85); v /= 85; }
    for (j = 0; j < k + 1; j++) { out[o++] = (u8)c[j]; if (rnd() % 40 == 0) out[o++] = '\n'; }
  }
  out[o++] = '~'; out[o++] = '>';
  return o;
}
static void test_ascii(void)
{
  static const struct { const char *in, *out; int outlen; } a85[] = {
    { "9jqo^BlbD-BleB1DJ+*+F(f,q~>", "Man is distinguished", 20 },
    { "<~9jqo^~>", "Man ", 4 },
    { "  <~ 9j qo\n^ ~>", "Man ", 4 },
    { "z~>", "\0\0\0\0", 4 },
    { "9jqo^z9jqo^~>", "Man \0\0\0\0Man ", 12 },
    { "9jqo~>", "Man", 3 },
    { "9jq~>", "Ma", 2 },
    { "9j~>", "M", 1 },
    { "9~>", "", 0 },
    { "9jqo^9jq", "Man Ma", 6 },          /* no end marker */
    { "9jqo^{garbage", "Man ", 4 },       /* bad character: what came before */
    { "~>", "", 0 },
    { "", "", 0 },
  };
  static const struct { const char *in, *out; int outlen; } hex[] = {
    { "48656c6C6f>", "Hello", 5 }, { "48 65\n6c 6c 6f", "Hello", 5 }, { "486>", "H`", 2 }, { "4>", "@", 1 }, { ">", "", 0 },
    { "4865zz", "He", 2 },
  };
  size_t i, n;
  u8 *o;
  for (i = 0; i < sizeof a85 / sizeof a85[0]; i++) {
    o = flt_ascii85((const u8 *)a85[i].in, strlen(a85[i].in), &n);
    CHECK(o && n == (size_t)a85[i].outlen && !memcmp(o, a85[i].out, n), "ASCII85 case %d", (int)i);
    free(o);
  }
  o = flt_ascii85((const u8 *)"{", 1, &n);
  CHECK(!o, "ASCII85 garbage only: NULL");
  for (i = 0; i < sizeof hex / sizeof hex[0]; i++) {
    o = flt_asciihex((const u8 *)hex[i].in, strlen(hex[i].in), &n);
    CHECK(o && n == (size_t)hex[i].outlen && !memcmp(o, hex[i].out, n), "ASCIIHex case %d", (int)i);
    free(o);
  }
  for (i = 0; i < 200; i++) {   /* round trips */
    size_t len = rnd() % 3000, k, el;
    u8 *d = make_data((int)(rnd() % 5), len), *e = (u8 *)malloc(len * 3 + 16), *h = (u8 *)malloc(len * 3 + 4);
    el = a85_enc(d, len, e);
    o = flt_ascii85(e, el, &n);
    CHECK(o && n == len && !memcmp(o, d, len), "ASCII85 round trip %lu", (unsigned long)len);
    free(o);
    for (k = 0; k < len; k++) sprintf((char *)h + 2 * k, "%02X", d[k]);
    o = flt_asciihex(h, 2 * len, &n);
    CHECK(o && n == len && !memcmp(o, d, len), "ASCIIHex round trip");
    free(o); free(d); free(e); free(h);
  }
}

/* RunLength-encode (for round trips) */
static size_t rl_enc(const u8 *in, size_t n, u8 *out)
{
  size_t i = 0, o = 0;
  while (i < n) {
    size_t r = 1;
    while (i + r < n && r < 128 && in[i + r] == in[i]) r++;
    if (r >= 2) { out[o++] = (u8)(257 - r); out[o++] = in[i]; i += r; continue; }
    r = 1;
    while (i + r < n && r < 128 && !(i + r + 1 < n && in[i + r] == in[i + r + 1])) r++;
    out[o++] = (u8)(r - 1);
    memcpy(out + o, in + i, r); o += r; i += r;
  }
  out[o++] = 128;
  return o;
}
static void test_runlength(void)
{
  static const u8 ex[] = { 2, 'a', 'b', 'c', 254, 'x', 128, 'q' };
  size_t i, n;
  u8 *o = flt_runlength(ex, sizeof ex, 0, &n);
  CHECK(o && n == 6 && !memcmp(o, "abcxxx", 6), "RunLength example");
  free(o);
  o = flt_runlength(ex, 3, 0, &n);
  CHECK(o && n == 2 && !memcmp(o, "ab", 2), "RunLength truncated literal");
  free(o);
  o = flt_runlength(ex, sizeof ex, 5, &n);
  CHECK(o && n == 5 && !memcmp(o, "abcxx", 5), "RunLength capped");
  free(o);
  o = flt_runlength(ex + 6, 1, 0, &n);
  CHECK(o && n == 0, "RunLength EOD only");
  free(o);
  for (i = 0; i < 200; i++) {
    size_t len = rnd() % 5000, el;
    u8 *d = make_data((int)(rnd() % 5), len), *e = (u8 *)malloc(len * 2 + 8);
    el = rl_enc(d, len, e);
    o = flt_runlength(e, el, 0, &n);
    CHECK(o && n == len && !memcmp(o, d, len), "RunLength round trip %lu", (unsigned long)len);
    free(o); free(d); free(e);
  }
}

/* ------------------------------------------------------------------ */
/* predictors */
static void test_predict(void)
{
  int t;
  size_t n;
  u8 *o;
  /* TIFF at 8 and 16 bpc: encode in C, decode, compare */
  for (t = 0; t < 40; t++) {
    int colors = 1 + (int)(rnd() % 4), bpc = (rnd() & 1) ? 8 : 16, cols = 1 + (int)(rnd() % 50), rows = 1 + (int)(rnd() % 10);
    size_t bps = (size_t)bpc / 8, rb = (size_t)colors * (size_t)cols * bps, r, i, len = rb * (size_t)rows;
    u8 *raw = make_data(3, len), *enc = (u8 *)malloc(len + 1);
    for (r = 0; r < (size_t)rows; r++) {
      const u8 *s = raw + r * rb;
      u8 *e = enc + r * rb;
      for (i = 0; i < rb / bps; i++) {
        u32 v = bpc == 8 ? s[i] : ((u32)s[2 * i] << 8 | s[2 * i + 1]), p = 0;
        if (i >= (size_t)colors) p = bpc == 8 ? s[i - (size_t)colors] : ((u32)s[2 * (i - (size_t)colors)] << 8 | s[2 * (i - (size_t)colors) + 1]);
        v -= p;
        if (bpc == 8) e[i] = (u8)v; else { e[2 * i] = (u8)(v >> 8); e[2 * i + 1] = (u8)v; }
      }
    }
    o = flt_predict(enc, len, 2, colors, bpc, cols, &n);
    CHECK(o && n == len && !memcmp(o, raw, len), "TIFF predictor %d colors %d bpc", colors, bpc);
    free(o); free(raw); free(enc);
  }
  {
    /* PNG: a partial last row, an unknown filter type, Up on the first row */
    static const u8 in[] = { 2, 1, 2, 3, 2, 1, 1, 1, 9, 5, 5, 5, 1, 7 };
    o = flt_predict(in, sizeof in, 15, 1, 8, 3, &n);
    CHECK(o && n == 10 && eqhex(o, n, "01020302030405050507"), "PNG edge rows");
    free(o);
    o = flt_predict(in, sizeof in, 1, 1, 8, 3, &n);
    CHECK(o && n == sizeof in && !memcmp(o, in, n), "predictor 1 copies");
    free(o);
    o = flt_predict(in, sizeof in, 12, 0, 8, 3, &n);
    CHECK(o && n == sizeof in, "bad parameters copy");
    free(o);
    o = flt_predict(in, 0, 12, 1, 8, 3, &n);
    CHECK(o && n == 0, "empty input");
    free(o);
  }
}

/* ------------------------------------------------------------------ */
/* CCITT, hand-made */
static void test_ccitt(void)
{
  /* 1-D, 8 columns: white 8 | white 2, black 3, white 3 */
  static const u8 g3[] = { 0x9B, 0xD0 };
  size_t n;
  u8 *o = flt_ccitt(g3, sizeof g3, 0, 8, 2, 0, 0, 0, 1, &n);
  CHECK(o && n == 2 && o[0] == 0xFF && o[1] == 0xC7, "G3 1-D hand-made");
  free(o);
  o = flt_ccitt(g3, sizeof g3, 0, 8, 2, 1, 0, 0, 1, &n);
  CHECK(o && n == 2 && o[0] == 0x00 && o[1] == 0x38, "G3 1-D BlackIs1");
  free(o);
  o = flt_ccitt(g3, sizeof g3, 0, 8, 0, 0, 0, 0, 1, &n);
  CHECK(o && n == 2 && o[0] == 0xFF && o[1] == 0xC7, "G3 1-D rows until the data ends");
  free(o);
  {
    /* G4 row 0: H(W2,B3) = 001 0111 10, then V0 (b1 at columns) = 1; row 1: V0 V0 V0 = 111. Bits:
       0010 1111 0111 1000 -> 0x2F 0x78 */
    static const u8 g4[] = { 0x2F, 0x78 };
    o = flt_ccitt(g4, sizeof g4, -1, 8, 2, 0, 0, 0, 1, &n);
    CHECK(o && n == 2 && o[0] == 0xC7 && o[1] == 0xC7, "G4 hand-made (%02x %02x)", o && n > 0 ? o[0] : 0, o && n > 1 ? o[1] : 0);
    free(o);
  }
  o = flt_ccitt(g3, sizeof g3, 0, 0, 2, 0, 0, 0, 1, &n);
  CHECK(!o, "zero columns: NULL");
}

/* ------------------------------------------------------------------ */
/* crypto known answers */
static void test_crypto_kat(void)
{
  static const char *md5v[][2] = {
    { "", "d41d8cd98f00b204e9800998ecf8427e" }, { "a", "0cc175b9c0f1b6a831c399e269772661" },
    { "abc", "900150983cd24fb0d6963f7d28e17f72" }, { "message digest", "f96b697d7cb7938d525a2f31aaf161d0" },
    { "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b" },
    { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f" },
    { "12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a" } };
  static const char *msg896 = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
  static const char *msg448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  u8 h[64], k[64], b[128], c[128], *o;
  size_t i, n;
  Md5 m;
  Rc4 r;
  for (i = 0; i < 7; i++) {
    md5((const u8 *)md5v[i][0], strlen(md5v[i][0]), h);
    CHECK(eqhex(h, 16, md5v[i][1]), "MD5 %s", md5v[i][0]);
  }
  {
    /* incremental in odd pieces = one go */
    u8 *d = make_data(1, 100000), h2[16];
    size_t p = 0;
    md5_init(&m);
    while (p < 100000) { size_t s = rnd() % 200; if (s > 100000 - p) s = 100000 - p; md5_update(&m, d + p, s); p += s; }
    md5_final(&m, h);
    md5(d, 100000, h2);
    CHECK(!memcmp(h, h2, 16), "MD5 incremental");
    free(d);
  }
  rc4_init(&r, (const u8 *)"Key", 3); rc4_crypt(&r, (const u8 *)"Plaintext", b, 9);
  CHECK(eqhex(b, 9, "bbf316e8d940af0ad3"), "RC4 Key");
  rc4_init(&r, (const u8 *)"Wiki", 4); rc4_crypt(&r, (const u8 *)"pedia", b, 5);
  CHECK(eqhex(b, 5, "1021bf0420"), "RC4 Wiki");
  memcpy(b, "Attack at dawn", 14);
  rc4_init(&r, (const u8 *)"Secret", 6); rc4_crypt(&r, b, b, 14);
  CHECK(eqhex(b, 14, "45a01f645fc35b383552544b9bf5"), "RC4 Secret, in place");
  /* FIPS-197 appendix C */
  unhex("00112233445566778899aabbccddeeff", b);
  unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", k);
  aes_encrypt_block(k, 16, b, c);
  CHECK(eqhex(c, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"), "AES-128 FIPS-197");
  aes_ecb_decrypt_block(k, 16, c, c);
  CHECK(!memcmp(b, c, 16), "AES-128 decrypt");
  aes_encrypt_block(k, 24, b, c);
  CHECK(eqhex(c, 16, "dda97ca4864cdfe06eaf70a0ec0d7191"), "AES-192 FIPS-197");
  aes_ecb_decrypt_block(k, 24, c, c);
  CHECK(!memcmp(b, c, 16), "AES-192 decrypt");
  aes_encrypt_block(k, 32, b, c);
  CHECK(eqhex(c, 16, "8ea2b7ca516745bfeafc49904b496089"), "AES-256 FIPS-197");
  aes_ecb_decrypt_block(k, 32, c, c);
  CHECK(!memcmp(b, c, 16), "AES-256 decrypt");
  /* NIST SP 800-38A F.2.1 and F.2.5 (CBC) */
  {
    static const char *pt = "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52ef"
                            "f69f2445df4f9b17ad2b417be66c3710";
    static const char *ct128 = "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e22229516"
                               "3ff1caa1681fac09120eca307586e1a7";
    static const char *ct256 = "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d39f23369a9d9bacfa530e26304231461"
                               "b2eb05e2c39be9fcda6c19078c6a9d1b";
    u8 iv[16], key[32], p[64], ivct[80];
    unhex("000102030405060708090a0b0c0d0e0f", iv);
    unhex(pt, p);
    unhex("2b7e151628aed2a6abf7158809cf4f3c", key);
    o = aes_cbc_encrypt_nopad(key, 16, iv, p, 64);
    CHECK(o && eqhex(o, 64, ct128), "AES-128-CBC encrypt (SP 800-38A)");
    memcpy(ivct, iv, 16); if (o) memcpy(ivct + 16, o, 64);
    free(o);
    o = aes_cbc_decrypt(key, 16, ivct, 80, &n);
    CHECK(o && n == 64 && !memcmp(o, p, 64), "AES-128-CBC decrypt (bad padding kept)");
    free(o);
    o = aes_cbc_decrypt(key, 16, ivct, 87, &n);
    CHECK(o && n == 64 && !memcmp(o, p, 64), "AES-CBC partial final block ignored");
    free(o);
    unhex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key);
    o = aes_cbc_encrypt_nopad(key, 32, iv, p, 64);
    CHECK(o && eqhex(o, 64, ct256), "AES-256-CBC encrypt (SP 800-38A)");
    memcpy(ivct + 16, o ? o : p, 64);
    free(o);
    o = aes_cbc_decrypt(key, 32, ivct, 80, &n);
    CHECK(o && n == 64 && !memcmp(o, p, 64), "AES-256-CBC decrypt");
    free(o);
    o = aes_cbc_decrypt(key, 32, ivct, 15, &n);
    CHECK(!o, "AES-CBC under 16 bytes: NULL");
    o = aes_cbc_decrypt(key, 32, ivct, 16, &n);
    CHECK(o && n == 0, "AES-CBC IV only: empty");
    free(o);
    o = aes_cbc_decrypt(key, 20, ivct, 80, &n);
    CHECK(!o, "AES bad key length: NULL");
  }
  sha256((const u8 *)"abc", 3, h);
  CHECK(eqhex(h, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 abc");
  sha256(NULL, 0, h);
  CHECK(eqhex(h, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "SHA-256 empty");
  sha256((const u8 *)msg448, strlen(msg448), h);
  CHECK(eqhex(h, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "SHA-256 448 bits");
  sha384((const u8 *)"abc", 3, h);
  CHECK(eqhex(h, 48, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"), "SHA-384 abc");
  sha384((const u8 *)msg896, strlen(msg896), h);
  CHECK(eqhex(h, 48, "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039"), "SHA-384 896");
  sha512((const u8 *)"abc", 3, h);
  CHECK(eqhex(h, 64, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd"
                     "454d4423643ce80e2a9ac94fa54ca49f"), "SHA-512 abc");
  sha512((const u8 *)msg896, strlen(msg896), h);
  CHECK(eqhex(h, 64, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433a"
                     "c7d329eeb6dd26545e96e55b874be909"), "SHA-512 896");
  {
    u8 *a = (u8 *)malloc(1000000);
    memset(a, 'a', 1000000);
    sha256(a, 1000000, h);
    CHECK(eqhex(h, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "SHA-256 million a");
    sha512(a, 1000000, h);
    CHECK(eqhex(h, 64, "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31b"
                       "eb009c5c2c49aa2e4eadb217ad8cc09b"), "SHA-512 million a");
    md5(a, 1000000, h);
    CHECK(eqhex(h, 16, "7707d6ae4e027c70eea2a935c2296f21"), "MD5 million a");
    free(a);
  }
}

/* ------------------------------------------------------------------ */
/* fuzzing: every decoder on truncated and corrupted input, with and without caps */
static size_t fuzzruns;
/* run every decoder that might take this input; check the caps */
static void fuzz_one(const u8 *d, size_t n)
{
  size_t len, cap = rnd() % 3 ? 0 : 1 + rnd() % 5000;
  u8 *o;
  int k;
  fuzzruns++;
  o = flt_flate(d, n, cap, &len); CHECK(!cap || len <= cap, "flate cap"); CHECK(o || !len, "flate"); free(o);
  o = flt_lzw(d, n, (int)(rnd() & 1), cap, &len); CHECK(!cap || len <= cap, "lzw cap"); free(o);
  o = flt_runlength(d, n, cap, &len); CHECK(!cap || len <= cap, "rl cap"); free(o);
  o = flt_ascii85(d, n, &len); free(o);
  o = flt_asciihex(d, n, &len); free(o);
  o = flt_predict(d, n, (int)(rnd() % 2 ? 2 : 10 + rnd() % 6), 1 + (int)(rnd() % 4), 1 << (rnd() % 5), 1 + (int)(rnd() % 40), &len);
  free(o);
  k = (int)(rnd() % 3) - 1;
  {
    int cols = 1 + (int)(rnd() % 300), rows = (int)(rnd() % 3 ? 0 : rnd() % 50);
    o = flt_ccitt(d, n, k, cols, rows, (int)(rnd() & 1), (int)(rnd() & 1), (int)(rnd() & 1), (int)(rnd() & 1), &len);
    CHECK(len % (((size_t)cols + 7) / 8) == 0 && (!rows || len <= (size_t)rows * (((size_t)cols + 7) / 8)), "ccitt rows");
    free(o);
  }
}
/* fuzz with one valid input: truncations and corruptions */
static void fuzz_seed(const u8 *d, size_t n, int tries)
{
  u8 *t = (u8 *)malloc(n + 1);
  int i;
  for (i = 0; i < tries; i++) {
    size_t len = n, j, nc;
    if (n) memcpy(t, d, n);
    if (i % 3 == 0 && n) len = rnd() % n;
    else for (nc = 1 + rnd() % 8, j = 0; j < nc && n; j++) t[rnd() % n] ^= (u8)(1 + rnd() % 255);
    fuzz_one(t, len);
  }
  free(t);
}
static void test_fuzz(void)
{
  int i, lvl;
  for (lvl = 1; lvl <= 9; lvl += 4) {
    u8 *d = make_data(lvl == 5 ? 3 : 0, 20000), *c;
    size_t cl;
    c = flt_deflate(d, 20000, lvl, &cl);
    fuzz_seed(c, cl, 300);
    free(c); free(d);
  }
  for (i = 0; i < 300; i++) {
    size_t n = rnd() % 2000, j;
    u8 *g = (u8 *)malloc(n + 1);
    for (j = 0; j < n; j++) g[j] = (u8)(i & 1 ? rnd() : (u32)(u8)"0123456789abcdef \n~>z!u<"[rnd() % 24]);
    fuzz_one(g, n);
    free(g);
  }
}

/* ------------------------------------------------------------------ */
/* speed */
static void test_speed(void)
{
  size_t n = 5u << 20, cl, dl;
  u8 *d = make_data(0, n), *c, *x;
  double t0, t;
  int lvl, rep;
  for (lvl = 1; lvl <= 9; lvl += 4) {
    t0 = now();
    c = flt_deflate(d, n, lvl, &cl);
    t = now() - t0;
    printf("  deflate level %d: %.1f MB -> %.2f MB in %.3f s (%.1f MB/s)\n", lvl, n / 1048576.0, cl / 1048576.0, t, n / 1048576.0 / t);
    if (lvl == 5) {
      t0 = now();
      for (rep = 0; rep < 5; rep++) { x = flt_flate(c, cl, 0, &dl); CHECK(x && dl == n && !memcmp(x, d, n), "speed round trip"); free(x); }
      t = (now() - t0) / 5;
      printf("  inflate: %.1f MB of output in %.4f s (%.1f MB/s)\n", n / 1048576.0, t, n / 1048576.0 / t);
    }
    free(c);
  }
  free(d);
}

/* ------------------------------------------------------------------ */
/* the vectors from filtertest.py */
static u8 *readf(const char *dir, const char *name, const char *ext, size_t *n)
{
  char path[1024];
  u8 *p;
  sprintf(path, "%s/%s.%s", dir, name, ext);
  p = file_read(path, n);
  if (!p) { printf("cannot read %s\n", path); *n = 0; }
  return p;
}
/* compare decoded rows of packed bits, ignoring the padding bits; inv compares against the inverse */
static int rows_equal(const u8 *a, const u8 *b, size_t n, int columns, int inv)
{
  size_t rb = ((size_t)columns + 7) / 8, i;
  u8 last = (u8)(0xFF << ((8 - columns % 8) % 8));
  for (i = 0; i < n; i++) {
    u8 m = (i % rb == rb - 1) ? last : 0xFF;
    if (((a[i] ^ (inv ? ~b[i] : b[i])) & m) != 0) return 0;
  }
  return 1;
}
static void test_vectors(const char *dir)
{
  char path[1024], line[512], kind[32], name[128];
  FILE *f;
  int count = 0;
  sprintf(path, "%s/manifest.txt", dir);
  if (!(f = fopen(path, "r"))) { CHECK(0, "no manifest in %s", dir); return; }
  while (fgets(line, sizeof line, f)) {
    size_t n, en, on, kn;
    u8 *in, *ex, *o, *key;
    int a[8];
    if (sscanf(line, "%31s %127s", kind, name) != 2) continue;
    count++;
    in = readf(dir, name, "in", &n);
    if (!in) { CHECK(0, "missing %s", name); continue; }
    ex = strcmp(kind, "deflate") ? readf(dir, name, "out", &en) : NULL;
    if (!strcmp(kind, "flate") || !strcmp(kind, "lzw")) {
      int early = 1;
      int lz = kind[0] == 'l';
      if (lz) sscanf(line, "%*s %*s %d", &early);
#define DEC(len, cap) (lz ? flt_lzw(in, len, early, cap, &on) : flt_flate(in, len, cap, &on))
      o = DEC(n, 0);
      CHECK(o && on == en && !memcmp(o, ex, en), "%s: %lu bytes, expected %lu", name, (unsigned long)on, (unsigned long)en);
      free(o);
      if (en > 1) {
        o = DEC(n, en / 2);
        CHECK(o && on == en / 2 && !memcmp(o, ex, on), "%s capped", name);
        free(o);
      }
      if (n > 2) {   /* truncated: whatever decodes is a prefix of the real thing */
        size_t cut = rnd() % n;
        o = DEC(cut, 0);
        CHECK(on <= en && (!on || !memcmp(o, ex, on)), "%s truncated at %lu is not a prefix", name, (unsigned long)cut);
        free(o);
      }
      if (n < 200000) fuzz_seed(in, n, 4);
#undef DEC
    } else if (!strcmp(kind, "predict")) {
      size_t zn, pn;
      u8 *z = flt_flate(in, n, 0, &zn);
      sscanf(line, "%*s %*s %d %d %d %d", &a[0], &a[1], &a[2], &a[3]);
      o = z ? flt_predict(z, zn, a[0], a[1], a[2], a[3], &pn) : NULL;
      CHECK(o && pn == en && !memcmp(o, ex, en), "%s: predictor %d colors %d bpc %d columns %d", name, a[0], a[1], a[2], a[3]);
      free(o);
      if (z) {
        o = flt_predict(z, zn / 2 + 1, a[0], a[1], a[2], a[3], &pn);   /* partial rows: no crash */
        free(o);
      }
      free(z);
    } else if (!strcmp(kind, "ccitt")) {
      sscanf(line, "%*s %*s %d %d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6]);
      o = flt_ccitt(in, n, a[0], a[1], a[2], a[3], a[4], a[5], a[6], &on);
      CHECK(o && on == en && rows_equal(o, ex, en, a[1], 0), "%s: K %d columns %d rows %d align %d eol %d: %lu bytes, expected %lu",
            name, a[0], a[1], a[2], a[4], a[5], (unsigned long)on, (unsigned long)en);
      free(o);
      o = flt_ccitt(in, n, a[0], a[1], a[2], !a[3], a[4], a[5], a[6], &on);
      CHECK(o && on == en && rows_equal(o, ex, en, a[1], 1), "%s with BlackIs1 flipped", name);
      free(o);
      fuzz_seed(in, n, 3);
    } else if (!strcmp(kind, "md5") || !strcmp(kind, "sha256") || !strcmp(kind, "sha384") || !strcmp(kind, "sha512")) {
      u8 h[64];
      if (kind[0] == 'm') md5(in, n, h);
      else if (kind[3] == '2') sha256(in, n, h);
      else if (kind[3] == '3') sha384(in, n, h);
      else sha512(in, n, h);
      CHECK(!memcmp(h, ex, en), "%s %s", kind, name);
    } else if (!strcmp(kind, "rc4")) {
      Rc4 r;
      key = readf(dir, name, "key", &kn);
      o = (u8 *)malloc(n + 1);
      rc4_init(&r, key, (int)kn);
      rc4_crypt(&r, in, o, n);
      CHECK(!memcmp(o, ex, n), "rc4 %s", name);
      free(o); free(key);
    } else if (!strcmp(kind, "aescbc")) {
      sscanf(line, "%*s %*s %d", &a[0]);
      key = readf(dir, name, "key", &kn);
      o = aes_cbc_decrypt(key, a[0], in, n, &on);
      CHECK(o && on == en && !memcmp(o, ex, en), "aes cbc decrypt %s", name);
      free(o); free(key);
    } else if (!strcmp(kind, "aesenc")) {
      size_t ivn;
      u8 *iv = readf(dir, name, "iv", &ivn);
      sscanf(line, "%*s %*s %d", &a[0]);
      key = readf(dir, name, "key", &kn);
      o = aes_cbc_encrypt_nopad(key, a[0], iv, in, n);
      CHECK(o && !memcmp(o, ex, en), "aes cbc encrypt %s", name);
      free(o); free(key); free(iv);
    } else if (!strcmp(kind, "deflate")) {
      int lv[3] = { 1, 6, 9 }, i;
      for (i = 0; i < 3; i++) {
        size_t cl, dl;
        u8 *c = flt_deflate(in, n, lv[i], &cl), *x;
        char ext[16];
        CHECK(c != NULL, "deflate %s", name);
        if (!c) continue;
        x = flt_flate(c, cl, 0, &dl);
        CHECK(x && dl == n && !memcmp(x, in, n), "deflate round trip %s level %d", name, lv[i]);
        sprintf(ext, "%d.def", lv[i]);
        sprintf(path, "%s/%s.%s", dir, name, ext);
        file_write(path, c, cl);
        if (n > 10000) printf("  deflate %-12s level %d: %8lu -> %8lu\n", name, lv[i], (unsigned long)n, (unsigned long)cl);
        free(x); free(c);
      }
    } else CHECK(0, "unknown vector kind %s", kind);
    free(in); free(ex);
  }
  fclose(f);
  printf("vectors: %d checked\n", count);
}

int main(int argc, char **argv)
{
  test_flate_roundtrip();
  test_flate_edges();
  test_lzw();
  test_ascii();
  test_runlength();
  test_predict();
  test_ccitt();
  test_crypto_kat();
  test_fuzz();
  if (argc > 1) test_vectors(argv[1]);
  printf("speed:\n");
  test_speed();
  printf("fuzz runs: %lu\n", (unsigned long)fuzzruns);
  printf("%d checks, %d failures\n", checks, fails);
  return fails != 0;
}
