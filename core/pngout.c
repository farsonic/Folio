/*
 * pngout.c - a rendered page written as an 8-bit RGB PNG (see pngout.h): each row filtered with
 * whichever of the five PNG filters gives the smallest sum of its bytes taken as signed, the whole
 * deflated (filters.c), and the resolution recorded.
 */
#include <stdlib.h>
#include <string.h>
#include "pngout.h"
#include "filters.h"
#include "zip.h"

static void put32(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24)); buf_addc(b, (int)((v >> 16) & 255)); buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void chunk(Buf *b, const char *type, const u8 *data, u32 len)
{
  u32 crc = crc32_update(0, (const u8 *)type, 4);
  put32(b, len);
  buf_add(b, type, 4);
  if (len) { buf_add(b, data, len); crc = crc32_update(crc, data, len); }
  put32(b, crc);
}
static int paeth(int a, int b, int c)
{
  int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
static u8 filt(int f, const u8 *cur, const u8 *prev, u32 x)
{
  int a = x >= 3 ? cur[x - 3] : 0, b = prev ? prev[x] : 0, c = prev && x >= 3 ? prev[x - 3] : 0;
  switch (f) {
  case 1: return (u8)(cur[x] - a);
  case 2: return (u8)(cur[x] - b);
  case 3: return (u8)(cur[x] - ((a + b) >> 1));
  case 4: return (u8)(cur[x] - paeth(a, b, c));
  }
  return cur[x];
}
u8 *png_write_rgb(const u8 *rgb, int w, int h, int dpi, size_t *len)
{
  u32 rb = (u32)w * 3, x;
  int y, f, best;
  u8 *filtered, *z, hdr[13];
  size_t zlen;
  Buf b;
  if (w <= 0 || h <= 0 || !(filtered = (u8 *)malloc((size_t)h * (rb + 1)))) return NULL;
  for (y = 0; y < h; y++) {
    const u8 *cur = rgb + (size_t)y * rb, *prev = y ? cur - rb : NULL;
    u8 *o = filtered + (size_t)y * (rb + 1);
    unsigned long sum, bestsum = 0;
    for (best = 0, f = 0; f < 5; f++) {
      for (sum = 0, x = 0; x < rb; x++) { int v = filt(f, cur, prev, x); sum += (unsigned long)(v < 128 ? v : 256 - v); }
      if (!f || sum < bestsum) { bestsum = sum; best = f; }
    }
    o[0] = (u8)best;
    for (x = 0; x < rb; x++) o[x + 1] = filt(best, cur, prev, x);
  }
  z = flt_deflate(filtered, (size_t)h * (rb + 1), 6, &zlen);
  free(filtered);
  if (!z) return NULL;
  buf_init(&b);
  buf_add(&b, "\211PNG\r\n\032\n", 8);
  hdr[0] = (u8)(w >> 24); hdr[1] = (u8)(w >> 16); hdr[2] = (u8)(w >> 8); hdr[3] = (u8)w;
  hdr[4] = (u8)(h >> 24); hdr[5] = (u8)(h >> 16); hdr[6] = (u8)(h >> 8); hdr[7] = (u8)h;
  hdr[8] = 8; hdr[9] = 2; hdr[10] = hdr[11] = hdr[12] = 0;
  chunk(&b, "IHDR", hdr, 13);
  if (dpi > 0) {
    u8 phys[9];
    u32 ppm = (u32)(((unsigned long)dpi * 10000UL + 127) / 254);
    phys[0] = phys[4] = (u8)(ppm >> 24); phys[1] = phys[5] = (u8)(ppm >> 16); phys[2] = phys[6] = (u8)(ppm >> 8); phys[3] = phys[7] = (u8)ppm;
    phys[8] = 1;
    chunk(&b, "pHYs", phys, 9);
  }
  chunk(&b, "IDAT", z, (u32)zlen);
  free(z);
  chunk(&b, "IEND", NULL, 0);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
