/*
 * jpegout.c - writing baseline JPEG (YCbCr, 4:2:0 below quality 90, else 4:4:4, the Annex K
 * tables) from RGB rows: for Optimize, which stores large pictures again at a lower resolution.
 * The encoder is Easel's (jpegenc.c, the same author's, GPL-2.0), taking RGB instead of RGBA:
 * a macroblock at a time, AAN floating-point DCT, the scale factors folded into the divisors.
 */
#include <stdlib.h>
#include <string.h>
#include "jpegout.h"

typedef struct { const u8 *px; i32 w, h; } Img;

/* ------------------------------------------------------------------ tables */
static const u8 zigzag[64] = {        /* the natural position of each coefficient in zigzag order */
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const u8 std_lum_q[64] = {     /* Annex K.1, natural order */
  16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
  18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
static const u8 std_chr_q[64] = {
  17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };
/* Annex K.3: code counts per length 1-16, then the symbols */
static const u8 dc_lum_bits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const u8 dc_chr_bits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const u8 dc_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const u8 ac_lum_bits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const u8 ac_lum_vals[162] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };
static const u8 ac_chr_bits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const u8 ac_chr_vals[162] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

typedef struct { u16 code[256]; u8 size[256]; } HuffEnc;
/* the canonical codes for a table given as counts per length and symbols (Annex C) */
static void huff_build(HuffEnc *h, const u8 *bits, const u8 *vals)
{
  int len, i, k = 0;
  u32 code = 0;
  memset(h, 0, sizeof *h);
  for (len = 1; len <= 16; len++) {
    for (i = 0; i < bits[len - 1]; i++, k++) { h->code[vals[k]] = (u16)code++; h->size[vals[k]] = (u8)len; }
    code <<= 1;
  }
}

/* ------------------------------------------------------------------ encoding */
typedef struct {
  Buf o;
  u32 bits; int nbits;                /* bits waiting to make a byte, most significant first */
  HuffEnc dc[2], ac[2];
  float div[2][64];                   /* 1 / (quantizer * AAN scale * 8), natural order */
  int pred[3];                        /* the last DC of each component */
} Enc;

/* append the low n bits of v; every 0xFF byte in the entropy data is followed by a stuffed 0 */
static void putbits(Enc *e, u32 v, int n)
{
  e->bits = (e->bits << n) | (v & ((1UL << n) - 1));
  e->nbits += n;
  while (e->nbits >= 8) {
    int c = (int)((e->bits >> (e->nbits - 8)) & 255);
    buf_addc(&e->o, c);
    if (c == 255) buf_addc(&e->o, 0);
    e->nbits -= 8;
  }
}
/* the forward DCT of an 8 x 8 block in place (AAN; the output is scaled, see div) */
static void fdct(float *d)
{
  float t0, t1, t2, t3, t4, t5, t6, t7, t10, t11, t12, t13, z1, z2, z3, z4, z5, z11, z13, *p;
  int i;
  for (i = 0; i < 16; i++) {
    int s = i < 8 ? 1 : 8;              /* rows, then columns */
    p = i < 8 ? d + i * 8 : d + (i - 8);
    t0 = p[0] + p[7 * s]; t7 = p[0] - p[7 * s]; t1 = p[s] + p[6 * s]; t6 = p[s] - p[6 * s];
    t2 = p[2 * s] + p[5 * s]; t5 = p[2 * s] - p[5 * s]; t3 = p[3 * s] + p[4 * s]; t4 = p[3 * s] - p[4 * s];
    t10 = t0 + t3; t13 = t0 - t3; t11 = t1 + t2; t12 = t1 - t2;
    p[0] = t10 + t11; p[4 * s] = t10 - t11;
    z1 = (t12 + t13) * 0.707106781f;
    p[2 * s] = t13 + z1; p[6 * s] = t13 - z1;
    t10 = t4 + t5; t11 = t5 + t6; t12 = t6 + t7;
    z5 = (t10 - t12) * 0.382683433f; z2 = 0.541196100f * t10 + z5; z4 = 1.306562965f * t12 + z5; z3 = t11 * 0.707106781f;
    z11 = t7 + z3; z13 = t7 - z3;
    p[5 * s] = z13 + z2; p[3 * s] = z13 - z2; p[s] = z11 + z4; p[7 * s] = z11 - z4;
  }
}
/* the size category of a coefficient (the bits its magnitude needs) */
static int category(int v)
{
  int n = 0;
  if (v < 0) v = -v;
  while (v) { n++; v >>= 1; }
  return n;
}
/* transform, quantize and Huffman-code one block of level-shifted samples for component c
   (table t: 0 luminance, 1 chrominance) */
static void block(Enc *e, float *blk, int c, int t)
{
  int q[64], k, run, diff, cat;
  fdct(blk);
  for (k = 0; k < 64; k++) {
    float v = blk[zigzag[k]] * e->div[t][zigzag[k]];
    q[k] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
  }
  diff = q[0] - e->pred[c]; e->pred[c] = q[0];
  cat = category(diff);
  putbits(e, e->dc[t].code[cat], e->dc[t].size[cat]);
  if (cat) putbits(e, (u32)(diff < 0 ? diff - 1 : diff), cat);
  for (run = 0, k = 1; k < 64; k++) {
    int v = q[k], sym;
    if (!v) { run++; continue; }
    while (run > 15) { putbits(e, e->ac[t].code[0xF0], e->ac[t].size[0xF0]); run -= 16; }
    cat = category(v);
    sym = (run << 4) | cat;
    putbits(e, e->ac[t].code[sym], e->ac[t].size[sym]);
    putbits(e, (u32)(v < 0 ? v - 1 : v), cat);
    run = 0;
  }
  if (run) putbits(e, e->ac[t].code[0], e->ac[t].size[0]);
}
static void put16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
/* a DHT table: class/id, counts, symbols */
static void dht(Buf *b, int tc, const u8 *bits, const u8 *vals)
{
  int i, n = 0;
  buf_addc(b, tc);
  for (i = 0; i < 16; i++) { buf_addc(b, bits[i]); n += bits[i]; }
  buf_add(b, vals, (size_t)n);
}

/* the picture's samples over white, as Y, Cb and Cr minus 128, for an mcu x mcu square at x0, y0
   (edges repeated past the picture's right and bottom) */
static void mcu_samples(const Img *im, i32 x0, i32 y0, int mcu, float *Y, float *Cb, float *Cr)
{
  int x, y;
  for (y = 0; y < mcu; y++) {
    i32 sy = y0 + y < im->h ? y0 + y : im->h - 1;
    for (x = 0; x < mcu; x++) {
      i32 sx = x0 + x < im->w ? x0 + x : im->w - 1;
      const u8 *p = im->px + ((long)sy * im->w + sx) * 3;
      float r = p[0], g = p[1], b = p[2];
      Y[y * mcu + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128;
      Cb[y * mcu + x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
      Cr[y * mcu + x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
    }
  }
}

/* the markers (SOI, APP0 JFIF, DQT, SOF0, DHT, SOS), the entropy-coded macroblocks, EOI */
u8 *jpeg_write_rgb(const u8 *rgb, int w, int h, int quality, size_t *len)
{
  Img imv, *im = &imv;
  int dpi = 0;
  static const double aan[8] = { 1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100, 0.275899379 };
  Enc *e;
  u8 qt[2][64];
  int sub = quality < 90, mcu = sub ? 16 : 8, i, t, scale;
  i32 mx, my;
  float *Y, *Cb, *Cr, blk[64];
  u8 *res;
  imv.px = rgb; imv.w = w; imv.h = h;
  if (!im->px || im->w < 1 || im->h < 1 || im->w > 65535 || im->h > 65535) return NULL;
  if (!(e = (Enc *)calloc(1, sizeof *e))) return NULL;
  if (!(Y = (float *)malloc(sizeof(float) * 256 * 3))) { free(e); return NULL; }
  Cb = Y + 256; Cr = Y + 512;
  /* IJG quality scaling, clamped to baseline's 8-bit quantizers */
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;
  scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
  for (t = 0; t < 2; t++)
    for (i = 0; i < 64; i++) {
      long v = ((long)(t ? std_chr_q[i] : std_lum_q[i]) * scale + 50) / 100;
      qt[t][i] = (u8)(v < 1 ? 1 : v > 255 ? 255 : v);
      e->div[t][i] = (float)(1.0 / (qt[t][i] * aan[i >> 3] * aan[i & 7] * 8.0));
    }
  huff_build(&e->dc[0], dc_lum_bits, dc_vals); huff_build(&e->ac[0], ac_lum_bits, ac_lum_vals);
  huff_build(&e->dc[1], dc_chr_bits, dc_vals); huff_build(&e->ac[1], ac_chr_bits, ac_chr_vals);
  buf_init(&e->o);
  /* SOI, JFIF 1.01 with the density in dots per inch (or just square pixels) */
  buf_add(&e->o, "\377\330\377\340\000\020JFIF\000\001\001", 13);
  buf_addc(&e->o, dpi > 0 ? 1 : 0);
  put16(&e->o, (u32)(dpi > 0 && dpi < 65536 ? dpi : 1)); put16(&e->o, (u32)(dpi > 0 && dpi < 65536 ? dpi : 1));
  buf_addc(&e->o, 0); buf_addc(&e->o, 0);
  /* both quantization tables, in zigzag order */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xDB); put16(&e->o, 2 + 65 * 2);
  for (t = 0; t < 2; t++) { buf_addc(&e->o, t); for (i = 0; i < 64; i++) buf_addc(&e->o, qt[t][zigzag[i]]); }
  /* the frame: 8-bit, three components; Y sampled 2x2 for 4:2:0 */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xC0); put16(&e->o, 17); buf_addc(&e->o, 8);
  put16(&e->o, (u32)im->h); put16(&e->o, (u32)im->w); buf_addc(&e->o, 3);
  buf_addc(&e->o, 1); buf_addc(&e->o, sub ? 0x22 : 0x11); buf_addc(&e->o, 0);
  buf_addc(&e->o, 2); buf_addc(&e->o, 0x11); buf_addc(&e->o, 1);
  buf_addc(&e->o, 3); buf_addc(&e->o, 0x11); buf_addc(&e->o, 1);
  /* the four Huffman tables */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xC4); put16(&e->o, 2 + 4 * 17 + 12 * 2 + 162 * 2);
  dht(&e->o, 0x00, dc_lum_bits, dc_vals); dht(&e->o, 0x10, ac_lum_bits, ac_lum_vals);
  dht(&e->o, 0x01, dc_chr_bits, dc_vals); dht(&e->o, 0x11, ac_chr_bits, ac_chr_vals);
  /* the scan: all three components, the whole spectrum */
  buf_add(&e->o, "\377\332\000\014\003\001\000\002\021\003\021\000\077\000", 14);
  for (my = 0; my < im->h; my += mcu)
    for (mx = 0; mx < im->w; mx += mcu) {
      int bx, by, x, y;
      mcu_samples(im, mx, my, mcu, Y, Cb, Cr);
      for (by = 0; by < mcu; by += 8)
        for (bx = 0; bx < mcu; bx += 8) {
          for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) blk[y * 8 + x] = Y[(by + y) * mcu + bx + x];
          block(e, blk, 0, 0);
        }
      for (t = 0; t < 2; t++) {
        float *c = t ? Cr : Cb;
        if (sub) for (y = 0; y < 8; y++) for (x = 0; x < 8; x++)
          blk[y * 8 + x] = (c[(2 * y) * 16 + 2 * x] + c[(2 * y) * 16 + 2 * x + 1] + c[(2 * y + 1) * 16 + 2 * x] + c[(2 * y + 1) * 16 + 2 * x + 1]) * 0.25f;
        else memcpy(blk, c, sizeof blk);
        block(e, blk, 1 + t, 1);
      }
    }
  if (e->nbits) putbits(e, 0x7F, 8 - e->nbits);       /* pad the last byte with 1 bits */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xD9);
  free(Y);
  if (e->o.oom) { buf_free(&e->o); free(e); return NULL; }
  *len = e->o.len;
  res = (u8 *)buf_take(&e->o);
  free(e);
  return res;
}
