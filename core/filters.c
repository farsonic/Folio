/*
 * filters.c - the PDF stream filters (see filters.h).
 *
 * Inflate is table-driven: a 10-bit (literal/length) or 9-bit (distance) lookup table resolves almost every code in
 * one step, and the rare longer codes fall back to a canonical search. All decoder state lives on the heap (the
 * Amiga's stack is small), every read and write is bounds-checked, and truncated input stops at the last whole
 * symbol. The deflate encoder uses hash chains (greedy at levels 1-3, lazy matching above) and picks the smallest
 * of dynamic, fixed and stored blocks.
 */
#include <stdlib.h>
#include <string.h>
#include "filters.h"

/* ------------------------------------------------------------------ */
/* growable output with an optional cap */
typedef struct { u8 *p; size_t len, cap, max; int oom, full; } Out;

/* start an output buffer with room for about cap bytes (never more than max, if max is set) */
static int out_init(Out *o, size_t cap, size_t max)
{
  o->len = 0; o->max = max; o->oom = 0; o->full = 0;
  if (cap < 64) cap = 64;
  if (max && cap > max) cap = max;
  o->cap = cap;
  if (!(o->p = (u8 *)malloc(cap))) { o->oom = 1; return 0; }
  return 1;
}
/* make room for k more bytes; returns how many of them may be written (fewer when the cap is reached: o->full is
   then set), 0 when out of memory */
static size_t out_room(Out *o, size_t k)
{
  size_t nc;
  u8 *q;
  if (o->oom) return 0;
  if (o->max && k > o->max - o->len) { k = o->max - o->len; o->full = 1; }
  if (k <= o->cap - o->len) return k;
  nc = o->cap;
  while (nc - o->len < k) {
    if (nc > ((size_t)-1) / 2) { nc = o->len + k; break; }
    nc *= 2;
  }
  if (o->max && nc > o->max) nc = o->max;
  if (!(q = (u8 *)realloc(o->p, nc))) { o->oom = 1; return 0; }
  o->p = q; o->cap = nc;
  return k;
}
/* append n bytes (as many as the cap allows); 0 when the output stopped */
static int out_put(Out *o, const u8 *s, size_t n)
{
  size_t k = out_room(o, n);
  if (k) { memcpy(o->p + o->len, s, k); o->len += k; }
  return k == n;
}
/* append one byte; 0 when the output stopped */
static int out_putc(Out *o, int c)
{
  if (o->len >= o->cap || (o->max && o->len >= o->max)) { if (!out_room(o, 1)) return 0; }
  o->p[o->len++] = (u8)c;
  return 1;
}
/* hand the result over: NULL when out of memory, or when there was an error before anything decoded */
static u8 *out_done(Out *o, int err, size_t *outlen)
{
  u8 *q;
  *outlen = 0;
  if (o->oom || (err && o->len == 0)) { free(o->p); o->p = NULL; return NULL; }
  if (o->cap > o->len + 256 && (q = (u8 *)realloc(o->p, o->len ? o->len : 1)) != NULL) o->p = q;
  *outlen = o->len;
  return o->p;
}

/* ------------------------------------------------------------------ */
/* deflate's tables (RFC 1951) */
static const u16 len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                                  131, 163, 195, 227, 258 };
static const u8 len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const u16 dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                                   2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const u8 clen_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* bytes with their bits reversed */
#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64
#define R4(n) R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)
#define R6(n) R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)
static const u8 rev8[256] = { R6(0), R6(2), R6(1), R6(3) };

/* the low k bits of v, reversed (k <= 16) */
static u32 rev_bits(u32 v, int k)
{
  return (((u32)rev8[v & 0xFF] << 8) | rev8[(v >> 8) & 0xFF]) >> (16 - k);
}

/* ------------------------------------------------------------------ */
/* inflate */
#define LFAST 10
#define DFAST 9
typedef struct {
  u16 fast[1 << LFAST];        /* by the next fastbits input bits: (length << 9) | symbol, 0 = a longer code */
  u32 maxcode[17];             /* canonical search for the longer codes, as in stb_image */
  u16 firstcode[16], firstsym[16];
  u16 sym[288];
  u8 size[288];
  int fastbits, nsym;
} Huff;

typedef struct {
  const u8 *in; size_t n, pos;   /* pos may run past n: the bits beyond are zeros, and using them is truncation */
  u32 bits; int nbits;
  Huff lit, dist, clen;
  Out o;
  int err;
} Inf;

/* build a decoding table from n code lengths (each 0-15); 0 if the lengths are over-subscribed. Incomplete codes
   are allowed: their missing codes decode as errors */
static int huff_build(Huff *h, const u8 *lens, int n, int fastbits)
{
  int count[16], next[16], i, code = 0, k = 0;
  memset(count, 0, sizeof count);
  memset(h->fast, 0, sizeof(u16) << fastbits);
  h->fastbits = fastbits; h->nsym = n;
  for (i = 0; i < n; i++) count[lens[i]]++;
  count[0] = 0;
  for (i = 1; i < 16; i++) {
    next[i] = code; h->firstcode[i] = (u16)code; h->firstsym[i] = (u16)k;
    code += count[i];
    if (count[i] && code - 1 >= (1 << i)) return 0;
    h->maxcode[i] = (u32)code << (16 - i);
    code <<= 1; k += count[i];
  }
  h->maxcode[16] = 0x10000UL;
  for (i = 0; i < n; i++) {
    int s = lens[i];
    if (s) {
      int c = next[s] - h->firstcode[s] + h->firstsym[s];
      h->size[c] = (u8)s; h->sym[c] = (u16)i;
      if (s <= fastbits) {
        int j = (int)rev_bits((u32)next[s], s);
        for (; j < (1 << fastbits); j += 1 << s) h->fast[j] = (u16)((s << 9) | i);
      }
      next[s]++;
    }
  }
  return 1;
}
/* decode a code longer than the fast table covers (bits holds at least 16 bits); (length << 9) | symbol, or -1 */
static int huff_slow(const Huff *h, u32 bits)
{
  u32 k = rev_bits(bits, 16);
  int s, b;
  for (s = h->fastbits + 1; s < 16; s++) if (k < h->maxcode[s]) break;
  if (s >= 16) return -1;
  b = (int)(k >> (16 - s)) - h->firstcode[s] + h->firstsym[s];
  if (b < 0 || b >= h->nsym || h->size[b] != s) return -1;
  return (s << 9) | h->sym[b];
}

/* refill so that at least 25 bits are buffered (zeros past the end of the input) */
#define NEEDBITS() do { while (nbits <= 24) { bits |= (u32)(pos < n ? in[pos] : 0) << nbits; pos++; nbits += 8; } } while (0)
/* have we used bits that were not really there? */
#define OVERRUN() (pos > n && (pos - n) * 8 > (size_t)nbits)
#define LOAD() const u8 *in = s->in; size_t n = s->n, pos = s->pos; u32 bits = s->bits; int nbits = s->nbits
#define SAVE() do { s->pos = pos; s->bits = bits; s->nbits = nbits; } while (0)

/* read k bits (k <= 16); s->err set if they ran past the input */
static u32 inf_bits(Inf *s, int k)
{
  u32 v;
  LOAD();
  NEEDBITS();
  v = bits & ((1UL << k) - 1);
  bits >>= k; nbits -= k;
  if (OVERRUN()) s->err = 1;
  SAVE();
  return v;
}
/* decode one symbol with table h, -1 on a bad code or truncation */
static int inf_sym(Inf *s, const Huff *h)
{
  int e;
  LOAD();
  NEEDBITS();
  e = h->fast[bits & ((1UL << h->fastbits) - 1)];
  if (!e && (e = huff_slow(h, bits)) < 0) { s->err = 1; return -1; }
  bits >>= e >> 9; nbits -= e >> 9;
  if (OVERRUN()) { s->err = 1; return -1; }
  SAVE();
  return e & 511;
}

/* decode the compressed data of one block with the current tables; 0 at its end, 1 to stop (damage, cap, memory) */
static int inf_codes(Inf *s)
{
  const Huff *lt = &s->lit, *dt = &s->dist;
  Out *o = &s->o;
  u8 *op = o->p;
  size_t olen = o->len, ocap = o->cap;
  int e;
  u32 sym, len, dist;
  LOAD();
  for (;;) {
    NEEDBITS();
    e = lt->fast[bits & ((1 << LFAST) - 1)];
    if (!e && (e = huff_slow(lt, bits)) < 0) break;
    bits >>= e >> 9; nbits -= e >> 9;
    if (OVERRUN()) break;
    sym = (u32)e & 511;
    if (sym < 256) {
      if (olen >= ocap) {
        o->len = olen;
        if (!out_room(o, 1)) break;
        op = o->p; ocap = o->cap;
      }
      op[olen++] = (u8)sym;
      continue;
    }
    if (sym == 256) { o->len = olen; SAVE(); return 0; }
    sym -= 257;
    if (sym >= 29) break;
    len = len_base[sym];
    if (len_extra[sym]) { len += bits & ((1UL << len_extra[sym]) - 1); bits >>= len_extra[sym]; nbits -= len_extra[sym]; }
    NEEDBITS();
    e = dt->fast[bits & ((1 << DFAST) - 1)];
    if (!e && (e = huff_slow(dt, bits)) < 0) break;
    bits >>= e >> 9; nbits -= e >> 9;
    sym = (u32)e & 511;
    if (sym >= 30) break;
    dist = dist_base[sym];
    if (dist_extra[sym]) {
      NEEDBITS();
      dist += bits & ((1UL << dist_extra[sym]) - 1); bits >>= dist_extra[sym]; nbits -= dist_extra[sym];
    }
    if (OVERRUN() || dist > olen) break;
    if (len > ocap - olen) {
      size_t k;
      o->len = olen;
      k = out_room(o, len);
      op = o->p; ocap = o->cap;
      if (k < len) {   /* the cap: copy what fits and stop */
        size_t i;
        for (i = 0; i < k; i++) op[olen + i] = op[olen + i - dist];
        olen += k;
        break;
      }
    }
    {
      u8 *d = op + olen;
      const u8 *src = d - dist;
      olen += len;
      if (dist >= len) memcpy(d, src, len);
      else if (dist == 1) memset(d, *src, len);
      else while (len--) *d++ = *src++;
    }
  }
  o->len = olen; SAVE();
  return 1;
}

/* read a dynamic block's code length tables into s->lit and s->dist; 0 on damage */
static int inf_dynamic(Inf *s)
{
  u8 lens[286 + 30], cl[19];
  int hlit, hdist, hclen, i, k;
  hlit = (int)inf_bits(s, 5) + 257;
  hdist = (int)inf_bits(s, 5) + 1;
  hclen = (int)inf_bits(s, 4) + 4;
  if (s->err || hlit > 286 || hdist > 30) return 0;
  memset(cl, 0, sizeof cl);
  for (i = 0; i < hclen; i++) cl[clen_order[i]] = (u8)inf_bits(s, 3);
  if (s->err || !huff_build(&s->clen, cl, 19, 7)) return 0;
  for (i = 0; i < hlit + hdist; ) {
    int c = inf_sym(s, &s->clen), rep, v = 0;
    if (c < 0) return 0;
    if (c < 16) { lens[i++] = (u8)c; continue; }
    if (c == 16) { if (!i) return 0; v = lens[i - 1]; rep = 3 + (int)inf_bits(s, 2); }
    else if (c == 17) rep = 3 + (int)inf_bits(s, 3);
    else rep = 11 + (int)inf_bits(s, 7);
    if (s->err || i + rep > hlit + hdist) return 0;
    for (k = 0; k < rep; k++) lens[i++] = (u8)v;
  }
  if (!lens[256]) return 0;
  return huff_build(&s->lit, lens, hlit, LFAST) && huff_build(&s->dist, lens + hlit, hdist, DFAST);
}
/* the fixed tables of block type 1 */
static void inf_fixed(Inf *s)
{
  u8 lens[288];
  int i;
  for (i = 0; i < 144; i++) lens[i] = 8;
  for (; i < 256; i++) lens[i] = 9;
  for (; i < 280; i++) lens[i] = 7;
  for (; i < 288; i++) lens[i] = 8;
  huff_build(&s->lit, lens, 288, LFAST);
  for (i = 0; i < 30; i++) lens[i] = 5;
  huff_build(&s->dist, lens, 30, DFAST);
}
/* a stored block: copy what is there; 0 at its end, 1 to stop */
static int inf_stored(Inf *s)
{
  size_t len, nlen, avail;
  /* back to a byte boundary, returning the whole buffered bytes to the input */
  s->pos -= (size_t)(s->nbits >> 3); s->bits = 0; s->nbits = 0;
  if (s->pos > s->n || s->n - s->pos < 4) { s->err = 1; return 1; }
  len = s->in[s->pos] | (size_t)s->in[s->pos + 1] << 8;
  nlen = s->in[s->pos + 2] | (size_t)s->in[s->pos + 3] << 8;
  s->pos += 4;
  if ((len ^ 0xFFFF) != nlen) { s->err = 1; return 1; }
  avail = s->n - s->pos;
  if (len > avail) { out_put(&s->o, s->in + s->pos, avail); s->pos = s->n; s->err = 1; return 1; }
  if (!out_put(&s->o, s->in + s->pos, len)) return 1;
  s->pos += len;
  return 0;
}

u8 *flt_flate(const u8 *in, size_t n, size_t maxout, size_t *outlen)
{
  Inf *s;
  u8 *r;
  int final = 0, err;
  size_t start = 0;
  *outlen = 0;
  if (!(s = (Inf *)malloc(sizeof *s))) return NULL;
  /* a zlib header, checked loosely: method 8, window <= 32K, check bits right */
  if (n >= 2 && (in[0] & 0x0F) == 8 && (in[0] >> 4) <= 7 && ((u32)in[0] * 256 + in[1]) % 31 == 0)
    start = (in[1] & 0x20) ? 6 : 2;
  if (start > n) start = n;
  s->in = in + start; s->n = n - start; s->pos = 0; s->bits = 0; s->nbits = 0; s->err = 0;
  if (!out_init(&s->o, s->n > ((size_t)-1) / 8 ? s->n : s->n * 4 + 1024, maxout)) { free(s); return NULL; }
  while (!final && !s->o.full) {
    u32 type;
    final = (int)inf_bits(s, 1);
    type = inf_bits(s, 2);
    if (s->err) break;
    if (type == 0) { if (inf_stored(s)) break; }
    else if (type == 1) { inf_fixed(s); if (inf_codes(s)) { s->err = 1; break; } }
    else if (type == 2) { if (!inf_dynamic(s) || inf_codes(s)) { s->err = 1; break; } }
    else { s->err = 1; break; }
  }
  err = s->err && !s->o.full;
  r = out_done(&s->o, err, outlen);
  free(s);
  return r;
}

/* ------------------------------------------------------------------ */
/* LZW (PDF's variant: MSB-first codes of 9-12 bits, 256 clears, 257 ends) */
typedef struct { u16 prefix[4096], len[4096]; u8 suffix[4096], first[4096]; } Lzw;

u8 *flt_lzw(const u8 *in, size_t n, int early_change, size_t maxout, size_t *outlen)
{
  Lzw *t;
  Out o;
  u32 buf = 0;
  int cnt = 0, width = 9, next = 258, prev = -1, err = 0, i;
  size_t pos = 0;
  u8 *r;
  *outlen = 0;
  early_change = early_change ? 1 : 0;
  if (!(t = (Lzw *)malloc(sizeof *t))) return NULL;
  for (i = 0; i < 256; i++) { t->suffix[i] = (u8)i; t->first[i] = (u8)i; t->len[i] = 1; t->prefix[i] = 0; }
  if (!out_init(&o, n > ((size_t)-1) / 4 ? n : n * 3 + 256, maxout)) { free(t); return NULL; }
  for (;;) {
    int code, c, k, L;
    while (cnt < width && pos < n) { buf = (buf << 8) | in[pos++]; cnt += 8; }
    if (cnt < width) break;               /* the data ran out without an EOD: lenient */
    code = (int)((buf >> (cnt - width)) & ((1UL << width) - 1));
    cnt -= width;
    if (code == 256) { width = 9; next = 258; prev = -1; continue; }
    if (code == 257) break;
    if (prev < 0) {
      if (code > 255) { err = 1; break; }
      if (!out_putc(&o, code)) break;
      prev = code;
      continue;
    }
    if (code > next || (code == next && next >= 4096)) { err = 1; break; }
    if (next < 4096) {   /* the new entry: the previous string plus the first byte of this one */
      t->prefix[next] = (u16)prev;
      t->suffix[next] = code == next ? t->first[prev] : t->first[code];
      t->first[next] = t->first[prev];
      t->len[next] = (u16)(t->len[prev] + 1);
      next++;
    }
    /* write the string backwards into place, keeping only what fits under the cap */
    L = t->len[code];
    k = (int)out_room(&o, (size_t)L);
    for (c = code, i = L - 1; i >= 0; i--) {
      if (i < k) o.p[o.len + (size_t)i] = t->suffix[c];
      c = t->prefix[c];
    }
    o.len += (size_t)k;
    if (k < L) break;
    prev = code;
    if (next + early_change >= (1 << width) && width < 12) width++;
  }
  r = out_done(&o, err, outlen);
  free(t);
  return r;
}

/* ------------------------------------------------------------------ */
/* ASCII85, ASCIIHex, RunLength */
#define IS_WS(c) ((c) == ' ' || (c) == '\n' || (c) == '\r' || (c) == '\t' || (c) == '\f' || (c) == 0)

u8 *flt_ascii85(const u8 *in, size_t n, size_t *outlen)
{
  Out o;
  size_t i = 0;
  u32 v = 0;
  int k = 0, err = 0;
  u8 b[4];
  *outlen = 0;
  while (i < n && IS_WS(in[i])) i++;
  if (n - i >= 2 && in[i] == '<' && in[i + 1] == '~') i += 2;
  if (!out_init(&o, n / 5 * 4 + 8, 0)) return NULL;
  for (; i < n; i++) {
    int c = in[i];
    if (IS_WS(c)) continue;
    if (c == '~') break;
    if (c == 'z' && k == 0) { memset(b, 0, 4); if (!out_put(&o, b, 4)) break; continue; }
    if (c < '!' || c > 'u') { err = 1; break; }
    v = v * 85 + (u32)(c - '!');
    if (++k == 5) {
      b[0] = (u8)(v >> 24); b[1] = (u8)(v >> 16); b[2] = (u8)(v >> 8); b[3] = (u8)v;
      if (!out_put(&o, b, 4)) break;
      k = 0; v = 0;
    }
  }
  if (k >= 2 && !o.oom) {   /* a final partial group: pad with 'u' and keep k - 1 bytes */
    int j;
    for (j = k; j < 5; j++) v = v * 85 + 84;
    b[0] = (u8)(v >> 24); b[1] = (u8)(v >> 16); b[2] = (u8)(v >> 8); b[3] = (u8)v;
    out_put(&o, b, (size_t)(k - 1));
  }
  return out_done(&o, err, outlen);
}

/* the value of a hex digit, -1 if it is not one */
static int hexval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
u8 *flt_asciihex(const u8 *in, size_t n, size_t *outlen)
{
  Out o;
  size_t i;
  int hi = -1, err = 0;
  *outlen = 0;
  if (!out_init(&o, n / 2 + 1, 0)) return NULL;
  for (i = 0; i < n; i++) {
    int c = in[i], d;
    if (IS_WS(c)) continue;
    if (c == '>') break;
    if ((d = hexval(c)) < 0) { err = 1; break; }
    if (hi < 0) hi = d;
    else { if (!out_putc(&o, hi << 4 | d)) break; hi = -1; }
  }
  if (hi >= 0) out_putc(&o, hi << 4);
  return out_done(&o, err, outlen);
}

u8 *flt_runlength(const u8 *in, size_t n, size_t maxout, size_t *outlen)
{
  Out o;
  size_t i = 0;
  int err = 0;
  *outlen = 0;
  if (!out_init(&o, n > ((size_t)-1) / 4 ? n : n * 2 + 64, maxout)) return NULL;
  while (i < n) {
    int L = in[i++];
    if (L == 128) break;
    if (L < 128) {
      size_t k = (size_t)L + 1;
      if (k > n - i) { out_put(&o, in + i, n - i); err = 1; break; }
      if (!out_put(&o, in + i, k)) break;
      i += k;
    } else {
      size_t k = (size_t)(257 - L), got;
      if (i >= n) { err = 1; break; }
      got = out_room(&o, k);
      memset(o.p + o.len, in[i++], got);
      o.len += got;
      if (got < k) break;
    }
  }
  return out_done(&o, err, outlen);
}

/* ------------------------------------------------------------------ */
/* predictors */

/* the PNG Paeth predictor */
static int paeth(int a, int b, int c)
{
  int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

u8 *flt_predict(const u8 *in, size_t n, int predictor, int colors, int bpc, int columns, size_t *outlen)
{
  size_t pixbits, rowbytes, bpp, i, total;
  u8 *out;
  *outlen = 0;
  if (colors < 1 || colors > 32 || (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16) || columns < 1) {
    predictor = 1; colors = 1; bpc = 8; columns = 1;
  }
  pixbits = (size_t)colors * (size_t)bpc;
  if ((size_t)columns > (((size_t)-1) - 8) / pixbits / 2) { predictor = 1; columns = 1; }
  rowbytes = (pixbits * (size_t)columns + 7) / 8;
  bpp = (pixbits + 7) / 8;
  if (predictor >= 10) {
    /* PNG: each row is a filter byte and rowbytes of data (the last may be short) */
    size_t nrows = n / (rowbytes + 1), rem = n % (rowbytes + 1), r;
    const u8 *src = in;
    u8 *prev = NULL;
    total = nrows * rowbytes + (rem > 1 ? rem - 1 : 0);
    if (!(out = (u8 *)malloc(total ? total : 1))) return NULL;
    for (r = 0; r <= nrows; r++) {
      size_t L = r < nrows ? rowbytes : (rem > 1 ? rem - 1 : 0);
      u8 *cur = out + r * rowbytes;
      int ft;
      if (!L) break;
      ft = *src++;
      switch (ft) {
      case 1:
        for (i = 0; i < L; i++) cur[i] = (u8)(src[i] + (i >= bpp ? cur[i - bpp] : 0));
        break;
      case 2:
        for (i = 0; i < L; i++) cur[i] = (u8)(src[i] + (prev ? prev[i] : 0));
        break;
      case 3:
        for (i = 0; i < L; i++) cur[i] = (u8)(src[i] + (((i >= bpp ? cur[i - bpp] : 0) + (prev ? prev[i] : 0)) >> 1));
        break;
      case 4:
        for (i = 0; i < L; i++)
          cur[i] = (u8)(src[i] + paeth(i >= bpp ? cur[i - bpp] : 0, prev ? prev[i] : 0, i >= bpp && prev ? prev[i - bpp] : 0));
        break;
      default:   /* 0 (None), and lenient for unknown types */
        memcpy(cur, src, L);
        break;
      }
      src += L;
      prev = cur;
    }
    *outlen = total;
    return out;
  }
  if (!(out = (u8 *)malloc(n ? n : 1))) return NULL;
  if (n) memcpy(out, in, n);
  *outlen = n;
  if (predictor == 2) {
    /* TIFF: each sample is the difference from the same component of the pixel to its left */
    size_t r0;
    for (r0 = 0; r0 < n; r0 += rowbytes) {
      u8 *row = out + r0;
      size_t L = n - r0 < rowbytes ? n - r0 : rowbytes, c = (size_t)colors;
      if (bpc == 8) {
        for (i = c; i < L; i++) row[i] = (u8)(row[i] + row[i - c]);
      } else if (bpc == 16) {
        for (i = c; i < L / 2; i++) {
          u32 v = (((u32)row[2 * i] << 8) | row[2 * i + 1]) + (((u32)row[2 * (i - c)] << 8) | row[2 * (i - c) + 1]);
          row[2 * i] = (u8)(v >> 8); row[2 * i + 1] = (u8)v;
        }
      } else {
        size_t ns = L * 8 / (size_t)bpc, mask = (1u << bpc) - 1;
        if (ns > c * (size_t)columns) ns = c * (size_t)columns;   /* not the padding bits */
        for (i = c; i < ns; i++) {
          size_t bit = i * (size_t)bpc, pbit = (i - c) * (size_t)bpc;
          int sh = 8 - bpc - (int)(bit & 7), psh = 8 - bpc - (int)(pbit & 7);
          u32 v = ((u32)(row[bit >> 3] >> sh) + (u32)(row[pbit >> 3] >> psh)) & mask;
          row[bit >> 3] = (u8)((row[bit >> 3] & ~(mask << sh)) | (v << sh));
        }
      }
    }
  }
  return out;
}

/* ------------------------------------------------------------------ */
/* CCITT fax (ITU-T T.4 and T.6) */

/* the run-length codes: terminating codes 0-63 and make-up codes 64-1728 for each colour, then the extended
   make-up codes 1792-2560 that both colours share */
static const char *const fax_wterm[64] = {
  "00110101", "000111", "0111", "1000", "1011", "1100", "1110", "1111", "10011", "10100", "00111", "01000", "001000",
  "000011", "110100", "110101", "101010", "101011", "0100111", "0001100", "0001000", "0010111", "0000011", "0000100",
  "0101000", "0101011", "0010011", "0100100", "0011000", "00000010", "00000011", "00011010", "00011011", "00010010",
  "00010011", "00010100", "00010101", "00010110", "00010111", "00101000", "00101001", "00101010", "00101011",
  "00101100", "00101101", "00000100", "00000101", "00001010", "00001011", "01010010", "01010011", "01010100",
  "01010101", "00100100", "00100101", "01011000", "01011001", "01011010", "01011011", "01001010", "01001011",
  "00110010", "00110011", "00110100" };
static const char *const fax_wmake[27] = {
  "11011", "10010", "010111", "0110111", "00110110", "00110111", "01100100", "01100101", "01101000", "01100111",
  "011001100", "011001101", "011010010", "011010011", "011010100", "011010101", "011010110", "011010111", "011011000",
  "011011001", "011011010", "011011011", "010011000", "010011001", "010011010", "011000", "010011011" };
static const char *const fax_bterm[64] = {
  "0000110111", "010", "11", "10", "011", "0011", "0010", "00011", "000101", "000100", "0000100", "0000101", "0000111",
  "00000100", "00000111", "000011000", "0000010111", "0000011000", "0000001000", "00001100111", "00001101000",
  "00001101100", "00000110111", "00000101000", "00000010111", "00000011000", "000011001010", "000011001011",
  "000011001100", "000011001101", "000001101000", "000001101001", "000001101010", "000001101011", "000011010010",
  "000011010011", "000011010100", "000011010101", "000011010110", "000011010111", "000001101100", "000001101101",
  "000011011010", "000011011011", "000001010100", "000001010101", "000001010110", "000001010111", "000001100100",
  "000001100101", "000001010010", "000001010011", "000000100100", "000000110111", "000000111000", "000000100111",
  "000000101000", "000001011000", "000001011001", "000000101011", "000000101100", "000001011010", "000001100110",
  "000001100111" };
static const char *const fax_bmake[27] = {
  "0000001111", "000011001000", "000011001001", "000001011011", "000000110011", "000000110100", "000000110101",
  "0000001101100", "0000001101101", "0000001001010", "0000001001011", "0000001001100", "0000001001101",
  "0000001110010", "0000001110011", "0000001110100", "0000001110101", "0000001110110", "0000001110111",
  "0000001010010", "0000001010011", "0000001010100", "0000001010101", "0000001011010", "0000001011011",
  "0000001100100", "0000001100101" };
static const char *const fax_xmake[13] = {
  "00000001000", "00000001100", "00000001101", "000000010010", "000000010011", "000000010100", "000000010101",
  "000000010110", "000000010111", "000000011100", "000000011101", "000000011110", "000000011111" };

#define WBITS 12
#define FAX_MAXOUT ((size_t)1 << 28)   /* with rows 0, a bomb guard: 256 MB of output at most */
#define BBITS 13
enum { M_PASS, M_HORIZ, M_V0 = 5, M_BAD = 9 };   /* vertical modes are M_V0 + offset (-3..3) */
typedef struct {
  const u8 *in; size_t n, bp, nbits;   /* bp: the bit position; nbits: the input's length in bits */
  u16 white[1 << WBITS], black[1 << BBITS];   /* by the next bits: (length << 12) | run, 0 = no such code */
  u8 mode[128];                               /* 2-D mode codes by the next 7 bits: (length << 4) | mode */
  int *ref, *cur, nref, ncur, cap;            /* changing elements of the reference and coding lines */
} Fax;

/* enter one code, written as a string of '0' and '1', into a lookup table indexed by the next bits input bits */
static void fax_add(u16 *tab, int bits, const char *code, int run)
{
  int len = (int)strlen(code), c = 0, i;
  for (i = 0; i < len; i++) c = c * 2 + (code[i] - '0');
  c <<= bits - len;
  for (i = 0; i < (1 << (bits - len)); i++) tab[c + i] = (u16)((len << 12) | run);
}
/* enter a 2-D mode code */
static void fax_mode(Fax *f, int code, int len, int mode)
{
  int i, c = code << (7 - len);
  for (i = 0; i < (1 << (7 - len)); i++) f->mode[c + i] = (u8)((len << 4) | mode);
}
/* build the lookup tables */
static void fax_tables(Fax *f)
{
  int i;
  memset(f->white, 0, sizeof f->white);
  memset(f->black, 0, sizeof f->black);
  for (i = 0; i < 64; i++) { fax_add(f->white, WBITS, fax_wterm[i], i); fax_add(f->black, BBITS, fax_bterm[i], i); }
  for (i = 0; i < 27; i++) {
    fax_add(f->white, WBITS, fax_wmake[i], 64 * (i + 1));
    fax_add(f->black, BBITS, fax_bmake[i], 64 * (i + 1));
  }
  for (i = 0; i < 13; i++) {
    fax_add(f->white, WBITS, fax_xmake[i], 1792 + 64 * i);
    fax_add(f->black, BBITS, fax_xmake[i], 1792 + 64 * i);
  }
  memset(f->mode, M_BAD, sizeof f->mode);   /* length 0: 0000001 (extensions) and 0000000 are not decoded */
  fax_mode(f, 1, 1, M_V0);
  fax_mode(f, 3, 3, M_V0 + 1); fax_mode(f, 2, 3, M_V0 - 1);
  fax_mode(f, 1, 3, M_HORIZ);
  fax_mode(f, 1, 4, M_PASS);
  fax_mode(f, 3, 6, M_V0 + 2); fax_mode(f, 2, 6, M_V0 - 2);
  fax_mode(f, 3, 7, M_V0 + 3); fax_mode(f, 2, 7, M_V0 - 3);
}
/* the next k bits (k <= 24), zeros past the end */
static u32 fax_peek(const Fax *f, int k)
{
  size_t b = f->bp >> 3;
  u32 v = 0;
  int i;
  for (i = 0; i < 4; i++) v = (v << 8) | (b + (size_t)i < f->n ? f->in[b + (size_t)i] : 0);
  return (v << (f->bp & 7)) >> (32 - k);
}
/* one run (make-up codes then a terminating code) of the given colour; -1 on a bad code or truncation */
static int fax_run(Fax *f, int black)
{
  int total = 0;
  for (;;) {
    u32 e = black ? f->black[fax_peek(f, BBITS)] : f->white[fax_peek(f, WBITS)];
    int run = (int)(e & 0xFFF);
    if (!e) return -1;
    f->bp += e >> 12;
    if (f->bp > f->nbits) return -1;
    total += run;
    if (run < 64) return total;
    if (total > 1 << 24) return -1;
  }
}
/* add a changing element to the coding line, kept in order and inside the line; 0 if the line is full */
static int fax_push(Fax *f, int x, int columns)
{
  if (f->ncur >= f->cap - 4) return 0;
  if (x > columns) x = columns;
  if (f->ncur && x < f->cur[f->ncur - 1]) x = f->cur[f->ncur - 1];
  f->cur[f->ncur++] = x;
  return 1;
}
/* decode a 1-D (Modified Huffman) line; 0 on damage */
static int fax_line1d(Fax *f, int columns)
{
  int a0 = 0, c = 0;
  f->ncur = 0;
  while (a0 < columns) {
    int run = fax_run(f, c);
    if (run < 0) return 0;
    a0 += run;
    if (a0 > columns) a0 = columns;
    if (!fax_push(f, a0, columns)) return 0;
    c ^= 1;
  }
  return 1;
}
/* decode a 2-D line against the reference line; 0 on damage */
static int fax_line2d(Fax *f, int columns)
{
  int a0 = -1, c = 0, ib = 0, lim = f->nref + 3;
  f->ncur = 0;
  while (a0 < columns) {
    int b1, b2, m, e;
    while (ib < lim - 1 && (f->ref[ib] <= a0 || (ib & 1) != c)) ib++;
    b1 = f->ref[ib]; b2 = f->ref[ib + 1 < lim ? ib + 1 : ib];
    e = f->mode[fax_peek(f, 7)];
    m = e & 15;
    if (m == M_BAD) return 0;
    f->bp += (size_t)(e >> 4);
    if (f->bp > f->nbits) return 0;
    if (m == M_PASS) {
      a0 = b2;
    } else if (m == M_HORIZ) {
      int r1 = fax_run(f, c), r2, a1;
      if (r1 < 0 || (r2 = fax_run(f, !c)) < 0) return 0;
      a1 = (a0 < 0 ? 0 : a0) + r1;
      if (a1 > columns) a1 = columns;
      if (!fax_push(f, a1, columns) || !fax_push(f, a1 + r2, columns)) return 0;
      a0 = f->cur[f->ncur - 1];
    } else {
      int a1 = b1 + m - M_V0;
      if (a1 < a0) a1 = a0;
      if (a1 < 0) a1 = 0;
      if (!fax_push(f, a1, columns)) return 0;
      a0 = f->cur[f->ncur - 1];
      c ^= 1;
      if (ib > 0) ib--;
    }
  }
  return 1;
}
/* draw the coding line into a row of packed bits: white is 1 unless black_is_1 */
static void fax_render(const Fax *f, u8 *row, int columns, int black_is_1)
{
  int i, x = 0, rb = (columns + 7) / 8;
  memset(row, black_is_1 ? 0x00 : 0xFF, (size_t)rb);
  for (i = 0; i < f->ncur; i++) {
    int e = f->cur[i];
    if (e > columns) e = columns;
    if (i & 1) {   /* a black run from x to e */
      for (; x < e && (x & 7); x++) row[x >> 3] ^= (u8)(0x80 >> (x & 7));
      for (; x + 8 <= e; x += 8) row[x >> 3] ^= 0xFF;
      for (; x < e; x++) row[x >> 3] ^= (u8)(0x80 >> (x & 7));
    }
    x = e;
  }
  if (f->ncur & 1)   /* the line ended black */
    for (; x < columns; x++) row[x >> 3] ^= (u8)(0x80 >> (x & 7));
}

u8 *flt_ccitt(const u8 *in, size_t n, int K, int columns, int rows, int black_is_1, int encoded_byte_align,
              int end_of_line, int end_of_block, size_t *outlen)
{
  Fax *f;
  Out o;
  size_t rb, init;
  int row = 0, next2d = 0, err = 0, *t;
  u8 *r;
  *outlen = 0;
  if (columns < 1 || columns > (1 << 20) || n > ((size_t)-1) / 8) return NULL;
  if (rows < 0) rows = 0;
  rb = ((size_t)columns + 7) / 8;
  init = n * 4 + rb;
  if (rows && (size_t)rows < ((size_t)1 << 24) / rb) init = rb * (size_t)rows;
  if (!(f = (Fax *)malloc(sizeof *f))) return NULL;
  f->cap = columns + 8;
  f->ref = (int *)malloc(sizeof(int) * (size_t)f->cap);
  f->cur = (int *)malloc(sizeof(int) * (size_t)f->cap);
  if (!f->ref || !f->cur || !out_init(&o, init, rows ? 0 : FAX_MAXOUT)) {
    free(f->ref); free(f->cur); free(f); return NULL;
  }
  fax_tables(f);
  f->in = in; f->n = n; f->bp = 0; f->nbits = n * 8;
  f->nref = 0;
  f->ref[0] = f->ref[1] = f->ref[2] = columns;
  /* skip any leading zero bits and an end-of-line, then get the first 2-D tag */
  while (f->bp < f->nbits && fax_peek(f, 12) == 0) f->bp++;
  if (fax_peek(f, 12) == 1) { f->bp += 12; end_of_line = 1; }
  if (K > 0) { next2d = !fax_peek(f, 1); f->bp++; }
  while (!rows || row < rows) {
    int got = 0, i;
    if (f->bp >= f->nbits) break;
    if (!(K < 0 || (K > 0 && next2d) ? fax_line2d(f, columns) : fax_line1d(f, columns))) {
      /* damaged: with end-of-line markers, keep the partial line and resynchronise on the next one */
      if (!end_of_line) { err = 1; break; }
      while (f->bp < f->nbits && fax_peek(f, 12) != 1) f->bp++;
      if (f->bp >= f->nbits) { err = 1; break; }
    }
    if (out_room(&o, rb) < rb) break;
    fax_render(f, o.p + o.len, columns, black_is_1);
    o.len += rb;
    row++;
    t = f->ref; f->ref = f->cur; f->cur = t;
    f->nref = f->ncur;
    for (i = 0; i < 3; i++) f->ref[f->nref + i] = columns;
    if (!end_of_block && rows && row >= rows) break;
    /* an end-of-line marker, skipping fill bits (as xpdf does: with EncodedByteAlign and no EndOfLine, a run of
       zeros could be a false marker, so it is not looked for then) */
    if (end_of_line || !encoded_byte_align) {
      if (end_of_line) while (f->bp < f->nbits && fax_peek(f, 12) != 1) f->bp++;
      else while (f->bp < f->nbits && fax_peek(f, 12) == 0) f->bp++;
      if (f->bp < f->nbits && fax_peek(f, 12) == 1) { f->bp += 12; got = 1; }
    }
    if (encoded_byte_align && !got) f->bp = (f->bp + 7) & ~(size_t)7;
    if (f->bp >= f->nbits) break;
    if (K > 0) { next2d = !fax_peek(f, 1); f->bp++; }
    if (end_of_block && !end_of_line && encoded_byte_align && fax_peek(f, 24) == 0x001001) { f->bp += 12; got = 1; }
    if (end_of_block && got && fax_peek(f, 12) == 1) break;   /* EOFB or RTC */
  }
  r = out_done(&o, err, outlen);
  free(f->ref); free(f->cur); free(f);
  return r;
}

/* ------------------------------------------------------------------ */
/* deflate (the encoder) */
#define WSIZE 32768
#define WMASK (WSIZE - 1)
#define HBITS 15
#define HSIZE (1 << HBITS)
#define BLOCKSYM 16384

typedef struct {
  const u8 *in; size_t n;
  u32 *head, *prev;              /* hash chains: positions + 1, 0 = none */
  u16 *sl, *sd; int ns;          /* the block's symbols: a literal (sd 0) or a match (length, distance) */
  u32 lf[288], df[32];           /* their frequencies */
  size_t bstart, bend;           /* the input the block covers */
  int chain, nice, good, lazy;
  u8 lcode[259], dlo[256], dhi[256];
  u8 *op; size_t olen, ocap; u32 bits; int nbits, oom;   /* the output, bit by bit */
  /* scratch for building codes */
  u32 w[2 * 288]; int par[2 * 288], dep[2 * 288], srt[288];
  u32 tf[288];
  u8 ll[288], fl[288], all[286 + 30], cls[286 + 30], cle[286 + 30];
  u16 lc[288];
} Def;

/* append one byte to the output */
static void def_byte(Def *d, int c)
{
  if (d->olen >= d->ocap) {
    size_t nc = d->ocap * 2;
    u8 *q;
    if (d->oom || nc < d->ocap || !(q = (u8 *)realloc(d->op, nc))) { d->oom = 1; return; }
    d->op = q; d->ocap = nc;
  }
  d->op[d->olen++] = (u8)c;
}
/* append k bits (k <= 16), least significant first */
static void def_put(Def *d, u32 v, int k)
{
  d->bits |= v << d->nbits;
  d->nbits += k;
  while (d->nbits >= 8) { def_byte(d, (int)(d->bits & 0xFF)); d->bits >>= 8; d->nbits -= 8; }
}
/* pad to a byte boundary */
static void def_align(Def *d)
{
  if (d->nbits > 0) def_byte(d, (int)(d->bits & 0xFF));
  d->bits = 0; d->nbits = 0;
}

/* Huffman code lengths for n symbols of frequencies freq, none longer than limit; unused symbols get 0 */
static void def_lengths(Def *d, const u32 *freq, int n, int limit, u8 *len)
{
  int m, i, j, k, a, b, maxd, *srt = d->srt;
  u32 *f = d->tf, *w = d->w;
  memcpy(f, freq, sizeof(u32) * (size_t)n);
  for (;;) {
    memset(len, 0, (size_t)n);
    for (m = 0, i = 0; i < n; i++) if (f[i]) srt[m++] = i;
    if (m == 0) return;
    if (m == 1) { len[srt[0]] = 1; return; }
    /* sort by frequency (Shell sort; n <= 288) */
    for (k = m / 2; k > 0; k /= 2)
      for (i = k; i < m; i++) {
        int s = srt[i];
        for (j = i; j >= k && f[srt[j - k]] > f[s]; j -= k) srt[j] = srt[j - k];
        srt[j] = s;
      }
    for (i = 0; i < m; i++) w[i] = f[srt[i]];
    /* two queues: the sorted leaves and the internal nodes, which are made in increasing weight */
    for (i = 0, j = m, k = m; k < 2 * m - 1; k++) {
      a = (i < m && (j >= k || w[i] <= w[j])) ? i++ : j++;
      b = (i < m && (j >= k || w[i] <= w[j])) ? i++ : j++;
      w[k] = w[a] + w[b];
      d->par[a] = d->par[b] = k;
    }
    d->dep[2 * m - 2] = 0;
    for (maxd = 0, k = 2 * m - 3; k >= 0; k--) {
      d->dep[k] = d->dep[d->par[k]] + 1;
      if (k < m && d->dep[k] > maxd) maxd = d->dep[k];
    }
    if (maxd <= limit) {
      for (i = 0; i < m; i++) len[srt[i]] = (u8)d->dep[i];
      return;
    }
    for (i = 0; i < n; i++) if (f[i]) f[i] = (f[i] >> 1) | 1;   /* flatten the frequencies and try again */
  }
}
/* canonical codes (bit-reversed, ready to send) for code lengths */
static void def_codes(const u8 *len, int n, u16 *code)
{
  int count[16], next[16], i, c = 0;
  memset(count, 0, sizeof count);
  for (i = 0; i < n; i++) count[len[i]]++;
  count[0] = 0;
  for (i = 1; i < 16; i++) { c = (c + count[i - 1]) << 1; next[i] = c; }
  for (i = 0; i < n; i++) code[i] = len[i] ? (u16)rev_bits((u32)next[len[i]]++, len[i]) : 0;
}
/* the distance code of a distance 1-32768 */
static int def_dcode(const Def *d, u32 dist)
{
  return dist <= 256 ? d->dlo[dist - 1] : d->dhi[(dist - 1) >> 7];
}
/* send the block's symbols with the given codes */
static void def_symbols(Def *d, const u16 *lc, const u8 *ll, const u16 *dc, const u8 *dl)
{
  int i;
  for (i = 0; i < d->ns; i++) {
    u32 v = d->sl[i], dist = d->sd[i];
    if (!dist) { def_put(d, lc[v], ll[v]); continue; }
    {
      int c = d->lcode[v], e = def_dcode(d, dist);
      def_put(d, lc[257 + c], ll[257 + c]);
      if (len_extra[c]) def_put(d, v - len_base[c], len_extra[c]);
      def_put(d, dc[e], dl[e]);
      if (dist_extra[e]) def_put(d, dist - dist_base[e], dist_extra[e]);
    }
  }
  def_put(d, lc[256], ll[256]);
}

/* write the collected block as the smallest of dynamic, fixed and stored */
static void def_flush(Def *d, int final)
{
  u8 *ll = d->ll, *fl = d->fl, *all = d->all, *cls = d->cls, *cle = d->cle, dl[32], fd[32], cl[19];
  u16 *lc = d->lc, dc[32], cc[19];
  u32 cf[19], dyn, fix, sto, raw = (u32)(d->bend - d->bstart), extra = 0;
  int i, hlit, hdist, hclen, ncl = 0, used = 0;
  d->lf[256]++;
  for (i = 0; i < 30; i++) used += d->df[i] != 0;
  def_lengths(d, d->lf, 286, 15, ll);
  def_lengths(d, d->df, 30, 15, dl);
  if (!used) dl[0] = 1;
  for (hlit = 286; hlit > 257 && !ll[hlit - 1]; hlit--) ;
  for (hdist = 30; hdist > 1 && !dl[hdist - 1]; hdist--) ;
  /* run-length code the code lengths */
  memcpy(all, ll, (size_t)hlit); memcpy(all + hlit, dl, (size_t)hdist);
  memset(cf, 0, sizeof cf);
  for (i = 0; i < hlit + hdist; ) {
    int v = all[i], run = 1;
    while (i + run < hlit + hdist && all[i + run] == v) run++;
    if (v == 0 && run >= 3) {
      int r = run > 138 ? 138 : run;
      if (r >= 11) { cls[ncl] = 18; cle[ncl++] = (u8)(r - 11); }
      else { cls[ncl] = 17; cle[ncl++] = (u8)(r - 3); }
      i += r;
    } else if (v != 0 && run >= 4) {
      cls[ncl] = (u8)v; cle[ncl++] = 0;
      i++; run--;
      while (run >= 3) {
        int r = run > 6 ? 6 : run;
        cls[ncl] = 16; cle[ncl++] = (u8)(r - 3);
        i += r; run -= r;
      }
    } else {
      cls[ncl] = (u8)v; cle[ncl++] = 0; i++;
    }
  }
  for (i = 0; i < ncl; i++) cf[cls[i]]++;
  def_lengths(d, cf, 19, 7, cl);
  for (used = 0, i = 0; i < 19; i++) used += cl[i] != 0;
  if (used == 1) cl[cl[0] ? 1 : 0] = 1;   /* zlib rejects an incomplete code-length code */
  for (hclen = 19; hclen > 4 && !cl[clen_order[hclen - 1]]; hclen--) ;
  /* the costs in bits */
  for (i = 0; i < 29; i++) extra += d->lf[257 + i] * len_extra[i];
  for (i = 0; i < 30; i++) extra += d->df[i] * dist_extra[i];
  dyn = 3 + 14 + 3 * (u32)hclen + extra;
  for (i = 0; i < 19; i++) dyn += cf[i] * cl[i];
  dyn += cf[16] * 2 + cf[17] * 3 + cf[18] * 7;
  for (i = 0; i < 286; i++) dyn += d->lf[i] * ll[i];
  for (i = 0; i < 30; i++) dyn += d->df[i] * dl[i];
  for (i = 0; i < 288; i++) fl[i] = (u8)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
  for (i = 0; i < 32; i++) fd[i] = 5;
  fix = 3 + extra;
  for (i = 0; i < 286; i++) fix += d->lf[i] * fl[i];
  for (i = 0; i < 30; i++) fix += d->df[i] * 5;
  sto = raw * 8 + (raw / 65535 + 1) * 42;
  if (sto < dyn && sto < fix) {
    const u8 *p = d->in + d->bstart;
    do {
      u32 k = raw > 65535 ? 65535 : raw;
      raw -= k;
      def_put(d, final && !raw ? 1 : 0, 1); def_put(d, 0, 2);
      def_align(d);
      def_byte(d, (int)(k & 0xFF)); def_byte(d, (int)(k >> 8));
      def_byte(d, (int)(~k & 0xFF)); def_byte(d, (int)((~k >> 8) & 0xFF));
      if (!d->oom && d->ocap - d->olen < k) {   /* room for the raw bytes in one go */
        size_t nc = d->olen + k + d->ocap / 2;
        u8 *q = (u8 *)realloc(d->op, nc);
        if (q) { d->op = q; d->ocap = nc; } else d->oom = 1;
      }
      if (!d->oom) { memcpy(d->op + d->olen, p, k); d->olen += k; }
      p += k;
    } while (raw);
  } else if (fix <= dyn) {
    def_put(d, final ? 1 : 0, 1); def_put(d, 1, 2);
    def_codes(fl, 288, lc); def_codes(fd, 32, dc);
    def_symbols(d, lc, fl, dc, fd);
  } else {
    def_put(d, final ? 1 : 0, 1); def_put(d, 2, 2);
    def_put(d, (u32)(hlit - 257), 5); def_put(d, (u32)(hdist - 1), 5); def_put(d, (u32)(hclen - 4), 4);
    for (i = 0; i < hclen; i++) def_put(d, cl[clen_order[i]], 3);
    def_codes(cl, 19, cc);
    for (i = 0; i < ncl; i++) {
      def_put(d, cc[cls[i]], cl[cls[i]]);
      if (cls[i] == 16) def_put(d, cle[i], 2);
      else if (cls[i] == 17) def_put(d, cle[i], 3);
      else if (cls[i] == 18) def_put(d, cle[i], 7);
    }
    def_codes(ll, 286, lc); def_codes(dl, 30, dc);
    def_symbols(d, lc, ll, dc, dl);
  }
  d->ns = 0;
  memset(d->lf, 0, sizeof d->lf); memset(d->df, 0, sizeof d->df);
  d->bstart = d->bend;
}
/* add a literal to the block */
static void def_lit(Def *d, int c)
{
  d->sl[d->ns] = (u16)c; d->sd[d->ns++] = 0;
  d->lf[c]++; d->bend++;
  if (d->ns == BLOCKSYM) def_flush(d, 0);
}
/* add a match to the block */
static void def_match(Def *d, int len, u32 dist)
{
  d->sl[d->ns] = (u16)len; d->sd[d->ns++] = (u16)dist;
  d->lf[257 + d->lcode[len]]++; d->df[def_dcode(d, dist)]++;
  d->bend += (size_t)len;
  if (d->ns == BLOCKSYM) def_flush(d, 0);
}
/* enter position pos (with 3 bytes after it) into the hash chains; returns the previous chain head */
static u32 def_insert(Def *d, size_t pos)
{
  const u8 *p = d->in + pos;
  u32 h = (((u32)p[0] << 10) ^ ((u32)p[1] << 5) ^ p[2]) & (HSIZE - 1), old = d->head[h];
  d->prev[pos & WMASK] = old;
  d->head[h] = (u32)pos + 1;
  return old;
}
/* the longest match for pos along the chain from cand, longer than 2 (else 2); its distance in *dist */
static int def_longest(Def *d, size_t pos, u32 cand, int chain, u32 *dist)
{
  const u8 *p = d->in + pos;
  size_t avail = d->n - pos;
  int maxlen = avail > 258 ? 258 : (int)avail, best = 2, nice = d->nice < maxlen ? d->nice : maxlen;
  while (cand && chain-- > 0) {
    size_t c = cand - 1;
    const u8 *q = d->in + c;
    if (pos - c >= WSIZE) break;
    if (q[best] == p[best] && q[0] == p[0] && q[1] == p[1]) {
      int l = 2;
      while (l < maxlen && q[l] == p[l]) l++;
      if (l > best) {
        best = l; *dist = (u32)(pos - c);
        if (l >= nice) break;
      }
    }
    cand = d->prev[c & WMASK];
  }
  return best;
}

/* run the matcher over the input, sending blocks as they fill */
static void def_run(Def *d)
{
  size_t pos = 0, n = d->n;
  u32 cand, dist = 0, pdist = 0;
  if (!d->lazy) {
    /* greedy: take the first good match; a long match's later positions are not hashed (faster) */
    while (pos < n) {
      int len = 2;
      if (n - pos >= 3 && (cand = def_insert(d, pos)) != 0) len = def_longest(d, pos, cand, d->chain, &dist);
      if (len >= 3) {
        size_t end = pos + (size_t)len, k;
        def_match(d, len, dist);
        if (len <= d->good) for (k = pos + 1; k < end && k + 3 <= n; k++) def_insert(d, k);
        pos = end;
      } else {
        def_lit(d, d->in[pos]);
        pos++;
      }
    }
    return;
  }
  {
    /* lazy: a match is only taken if the next position does not have a longer one */
    int plen = 2, avail = 0;
    while (pos < n) {
      int len = 2;
      cand = n - pos >= 3 ? def_insert(d, pos) : 0;
      if (cand && plen < d->lazy) {
        len = def_longest(d, pos, cand, plen >= d->good ? d->chain >> 2 : d->chain, &dist);
        if (len == 3 && dist > 4096) len = 2;
      }
      if (plen >= 3 && len <= plen) {
        size_t end = pos - 1 + (size_t)plen, k;
        def_match(d, plen, pdist);
        for (k = pos + 1; k < end && k + 3 <= n; k++) def_insert(d, k);
        pos = end; avail = 0; plen = 2;
      } else {
        if (avail) def_lit(d, d->in[pos - 1]);
        avail = 1; plen = len; pdist = dist; pos++;
      }
    }
    if (avail) def_lit(d, d->in[pos - 1]);
  }
}

u8 *flt_deflate(const u8 *in, size_t n, int level, size_t *outlen)
{
  /* good (or max insert for greedy), lazy (0 = greedy), nice, chain, by level */
  static const u16 cfg[9][4] = { { 4, 0, 8, 4 }, { 5, 0, 16, 8 }, { 6, 0, 32, 32 }, { 4, 4, 16, 16 }, { 8, 16, 32, 32 },
                                 { 8, 16, 128, 128 }, { 8, 32, 128, 256 }, { 32, 128, 258, 1024 },
                                 { 32, 258, 258, 4096 } };
  Def *d;
  int i, c;
  u32 a = 1, b = 0;
  size_t k;
  u8 *r;
  *outlen = 0;
  if (level < 1) level = 1;
  if (level > 9) level = 9;
  if (!(d = (Def *)calloc(1, sizeof *d))) return NULL;
  d->in = in; d->n = n;
  d->good = cfg[level - 1][0]; d->lazy = cfg[level - 1][1]; d->nice = cfg[level - 1][2]; d->chain = cfg[level - 1][3];
  d->ocap = n / 2 + 1024;
  d->head = (u32 *)calloc(HSIZE, sizeof(u32));
  d->prev = (u32 *)calloc(WSIZE, sizeof(u32));
  d->sl = (u16 *)malloc(BLOCKSYM * sizeof(u16));
  d->sd = (u16 *)malloc(BLOCKSYM * sizeof(u16));
  d->op = (u8 *)malloc(d->ocap);
  if (!d->head || !d->prev || !d->sl || !d->sd || !d->op) {
    free(d->head); free(d->prev); free(d->sl); free(d->sd); free(d->op); free(d);
    return NULL;
  }
  for (c = 0; c < 29; c++)
    for (i = 0; i < (1 << len_extra[c]) && len_base[c] + i <= 258; i++) d->lcode[len_base[c] + i] = (u8)c;
  for (c = 0; c < 30; c++)
    for (i = 0; i < (1 << dist_extra[c]); i++) {
      int dd = dist_base[c] + i;
      if (dd <= 256) d->dlo[dd - 1] = (u8)c; else d->dhi[(dd - 1) >> 7] = (u8)c;
    }
  /* the zlib header: deflate, 32K window, a level hint */
  c = level == 1 ? 0 : level < 6 ? 1 : level == 6 ? 2 : 3;
  def_byte(d, 0x78);
  def_byte(d, (c << 6) + 31 - ((0x78 * 256 + (c << 6)) % 31));
  def_run(d);
  def_flush(d, 1);
  def_align(d);
  /* the Adler-32 trailer */
  for (k = 0; k < n; ) {
    size_t e = k + 5552 < n ? k + 5552 : n;
    for (; k < e; k++) { a += in[k]; b += a; }
    a %= 65521; b %= 65521;
  }
  def_byte(d, (int)(b >> 8)); def_byte(d, (int)(b & 0xFF)); def_byte(d, (int)(a >> 8)); def_byte(d, (int)(a & 0xFF));
  free(d->head); free(d->prev); free(d->sl); free(d->sd);
  if (d->oom) { free(d->op); free(d); return NULL; }
  r = d->op;
  *outlen = d->olen;
  free(d);
  return r;
}
