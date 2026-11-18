/*
 * pdfwrite.c - a document written out as a new PDF (see pdfwrite.h): every object reachable from
 * the catalog renumbered and written plainly (decrypted, object streams unpacked), the page tree
 * rebuilt flat from the pages in the order given, with inherited attributes copied into each page.
 * Pages left out (an extract, or deleted ones) become null wherever something else refers to them.
 *
 * With SaveOpts: streams without a filter deflated; objects that aren't streams packed a hundred
 * at a time into object streams, with a cross-reference stream; and the Standard Security Handler
 * applied (AES-128, V4 R4, or AES-256, V5 R6): every string and stream encrypted with its own IV.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "pdfwrite.h"
#include "crypto.h"
#include "filters.h"

typedef struct {
  PDoc *d;
  const SaveOpts *o;
  int *map;                 /* old object number -> new (0 not yet, -1 excluded) */
  int nmap;
  int *queue; int nq, capq; /* old numbers waiting to be written */
  /* each new number's cross-reference entry: type 1 at offset f2, or type 2 in object stream f2 at index f3 */
  u8 *xtype; long *xf2; int *xf3; int capx, noffs;
  int next;                 /* the next new number */
  Buf out;
  Buf *b;                   /* where put_obj writes (out, or the object stream being filled) */
  int pages_num;            /* the new page tree's number */
  PObj **pages; int npages; /* the pages kept, in order */
  int *page_new;            /* their new numbers */
  /* encryption */
  int crypt; u8 key[32]; int keylen; int cur; int nocrypt; u32 ivn; u8 ivseed[16];
  int enc_num;              /* the /Encrypt dictionary's new number */
  /* the object stream being filled: its objects' numbers and offsets */
  Buf os, osi; int os_n, os_nums[100];
} W;

/* ------------------------------------------------------------------ randomness */
void pdf_random(u8 *out, int n, const void *seed, size_t seedlen)
{
  static u8 state[32];
  static u32 counter;
  while (n > 0) {
    u8 buf[32 + 4 + 4 + 4 + 64], h[32];
    size_t k = 0, sl = seedlen > 64 ? 64 : seedlen;
    u32 t = (u32)time(NULL), c = (u32)clock(), i;
    memcpy(buf, state, 32); k = 32;
    for (i = 0; i < 4; i++) buf[k++] = (u8)(t >> (8 * i));
    for (i = 0; i < 4; i++) buf[k++] = (u8)(c >> (8 * i));
    counter++;
    for (i = 0; i < 4; i++) buf[k++] = (u8)(counter >> (8 * i));
    if (seed && sl) { memcpy(buf + k, seed, sl); k += sl; }
    sha256(buf, k, h);
    memcpy(state, h, 32);
    sha256(h, 32, h);
    memcpy(out, h, (size_t)(n < 16 ? n : 16));
    out += 16; n -= 16;
  }
}

/* ------------------------------------------------------------------ the security handler */
static const u8 pad32[32] = {
  0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
  0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A
};
static void padpw(const char *pw, u8 out[32])
{
  size_t n = pw ? strlen(pw) : 0;
  if (n > 32) n = 32;
  if (n) memcpy(out, pw, n);
  memcpy(out + n, pad32, 32 - n);
}
/* the R6 hash (algorithm 2.B), as pdf.c reads it */
static void hash_r6(const u8 *pw, int pwlen, const u8 *salt, const u8 *udata, u8 out[32])
{
  u8 k[64], *k1, tmp[128 + 8 + 48];
  int klen = 32, round = 0, n;
  memcpy(tmp, pw, (size_t)pwlen); n = pwlen;
  memcpy(tmp + n, salt, 8); n += 8;
  if (udata) { memcpy(tmp + n, udata, 48); n += 48; }
  sha256(tmp, (size_t)n, k);
  if (!(k1 = (u8 *)malloc((size_t)(pwlen + 64 + 48) * 64))) { memcpy(out, k, 32); return; }
  for (;;) {
    int len1 = 0, i, sum = 0;
    u8 *enc;
    for (i = 0; i < 64; i++) {
      memcpy(k1 + len1, pw, (size_t)pwlen); len1 += pwlen;
      memcpy(k1 + len1, k, (size_t)klen); len1 += klen;
      if (udata) { memcpy(k1 + len1, udata, 48); len1 += 48; }
    }
    if (!(enc = aes_cbc_encrypt_nopad(k, 16, k + 16, k1, (size_t)len1))) break;
    for (i = 0; i < 16; i++) sum += enc[i];
    switch (sum % 3) {
    case 0: sha256(enc, (size_t)len1, k); klen = 32; break;
    case 1: sha384(enc, (size_t)len1, k); klen = 48; break;
    default: sha512(enc, (size_t)len1, k); klen = 64; break;
    }
    round++;
    if (round >= 64 && enc[len1 - 1] <= round - 32) { free(enc); break; }
    free(enc);
  }
  free(k1);
  memcpy(out, k, 32);
}
static PObj *bstr(const u8 *p, int n) { return pobj_string((const char *)p, n); }
/* the /Encrypt dictionary for the options, the file key in w; NULL if out of memory */
static PObj *make_encrypt(W *w, const u8 *id0, int idlen)
{
  const SaveOpts *o = w->o;
  PObj *e = pobj_new(PO_DICT), *cf = pobj_new(PO_DICT), *std = pobj_new(PO_DICT);
  long perms = o->perms ? o->perms : PERM_ALL;
  i32 p = (i32)(0xFFFFF0C0u | ((u32)perms & 0xF3Cu));
  const char *upw = o->user_pw ? o->user_pw : "";
  char opw[40];
  int i;
  if (!e || !cf || !std) { pobj_free(e); pobj_free(cf); pobj_free(std); return NULL; }
  if (o->owner_pw && *o->owner_pw) { strncpy(opw, o->owner_pw, sizeof opw - 1); opw[sizeof opw - 1] = 0; }
  else { u8 r[16]; pdf_random(r, 16, w->d->data, w->d->len > 64 ? 64 : w->d->len); for (i = 0; i < 16; i++) opw[i] = (char)('a' + r[i] % 26); opw[16] = 0; }
  pobj_dict_set(e, "Filter", pobj_name("Standard"));
  pobj_dict_set(std, "AuthEvent", pobj_name("DocOpen"));
  pobj_dict_set(e, "StmF", pobj_name("StdCF"));
  pobj_dict_set(e, "StrF", pobj_name("StdCF"));
  pobj_dict_set(e, "P", pobj_int((long)p));
  if (o->crypt == SAVE_AES256) {
    u8 vs[8], ks[8], h[32], U[48], O[48], UE[32], OE[32], iv[16], perm[16], *c;
    int ul = (int)strlen(upw), ol = (int)strlen(opw);
    if (ul > 127) ul = 127;
    pdf_random(w->key, 32, upw, (size_t)ul);
    w->keylen = 32;
    memset(iv, 0, 16);
    pdf_random(vs, 8, NULL, 0); pdf_random(ks, 8, NULL, 0);
    hash_r6((const u8 *)upw, ul, vs, NULL, h);
    memcpy(U, h, 32); memcpy(U + 32, vs, 8); memcpy(U + 40, ks, 8);
    hash_r6((const u8 *)upw, ul, ks, NULL, h);
    if (!(c = aes_cbc_encrypt_nopad(h, 32, iv, w->key, 32))) { pobj_free(e); pobj_free(cf); pobj_free(std); return NULL; }
    memcpy(UE, c, 32); free(c);
    pdf_random(vs, 8, NULL, 0); pdf_random(ks, 8, NULL, 0);
    hash_r6((const u8 *)opw, ol, vs, U, h);
    memcpy(O, h, 32); memcpy(O + 32, vs, 8); memcpy(O + 40, ks, 8);
    hash_r6((const u8 *)opw, ol, ks, U, h);
    if (!(c = aes_cbc_encrypt_nopad(h, 32, iv, w->key, 32))) { pobj_free(e); pobj_free(cf); pobj_free(std); return NULL; }
    memcpy(OE, c, 32); free(c);
    for (i = 0; i < 4; i++) perm[i] = (u8)((u32)p >> (8 * i));
    perm[4] = perm[5] = perm[6] = perm[7] = 0xFF;
    perm[8] = 'T'; perm[9] = 'a'; perm[10] = 'd'; perm[11] = 'b';
    pdf_random(perm + 12, 4, NULL, 0);
    aes_encrypt_block(w->key, 32, perm, perm);
    pobj_dict_set(e, "V", pobj_int(5)); pobj_dict_set(e, "R", pobj_int(6)); pobj_dict_set(e, "Length", pobj_int(256));
    pobj_dict_set(std, "CFM", pobj_name("AESV3")); pobj_dict_set(std, "Length", pobj_int(32));
    pobj_dict_set(e, "O", bstr(O, 48)); pobj_dict_set(e, "U", bstr(U, 48));
    pobj_dict_set(e, "OE", bstr(OE, 32)); pobj_dict_set(e, "UE", bstr(UE, 32));
    pobj_dict_set(e, "Perms", bstr(perm, 16));
  } else {
    /* AES-128 (V4 R4): algorithms 2, 3 and 5 */
    u8 up[32], op[32], h[16], O[32], U[32], k2[16], t[100];
    Md5 m;
    Rc4 r;
    int j, k;
    padpw(upw, up); padpw(opw, op);
    md5(op, 32, h);
    for (i = 0; i < 50; i++) md5(h, 16, h);
    memcpy(O, up, 32);
    for (i = 0; i < 20; i++) { for (j = 0; j < 16; j++) k2[j] = (u8)(h[j] ^ i); rc4_init(&r, k2, 16); rc4_crypt(&r, O, O, 32); }
    md5_init(&m); md5_update(&m, up, 32); md5_update(&m, O, 32);
    for (i = 0; i < 4; i++) t[i] = (u8)((u32)p >> (8 * i));
    md5_update(&m, t, 4); md5_update(&m, id0, (size_t)idlen);
    md5_final(&m, h);
    for (i = 0; i < 50; i++) md5(h, 16, h);
    memcpy(w->key, h, 16); w->keylen = 16;
    memcpy(t, pad32, 32); k = 32;
    if (idlen > 64) idlen = 64;
    memcpy(t + k, id0, (size_t)idlen); k += idlen;
    md5(t, (size_t)k, U);
    for (i = 0; i < 20; i++) { for (j = 0; j < 16; j++) k2[j] = (u8)(w->key[j] ^ i); rc4_init(&r, k2, 16); rc4_crypt(&r, U, U, 16); }
    memcpy(U + 16, pad32, 16);
    pobj_dict_set(e, "V", pobj_int(4)); pobj_dict_set(e, "R", pobj_int(4)); pobj_dict_set(e, "Length", pobj_int(128));
    pobj_dict_set(std, "CFM", pobj_name("AESV2")); pobj_dict_set(std, "Length", pobj_int(16));
    pobj_dict_set(e, "O", bstr(O, 32)); pobj_dict_set(e, "U", bstr(U, 32));
  }
  pobj_dict_set(cf, "StdCF", std);
  pobj_dict_set(e, "CF", cf);
  w->crypt = o->crypt;
  pdf_random(w->ivseed, 16, w->key, 16);
  return e;
}
/* data encrypted for the object being written: IV, then AES-CBC with PKCS#7 padding; malloc'd */
static u8 *encrypt_bytes(W *w, const u8 *in, size_t n, size_t *outlen)
{
  u8 key[16], *res, *padded, iv[16], t[24];
  const u8 *k = w->key;
  size_t pn = (n / 16 + 1) * 16, i;
  u8 *c;
  if (w->crypt == SAVE_AES128) {
    Md5 m;
    u8 s[9];
    s[0] = (u8)w->cur; s[1] = (u8)(w->cur >> 8); s[2] = (u8)(w->cur >> 16); s[3] = 0; s[4] = 0;
    s[5] = 's'; s[6] = 'A'; s[7] = 'l'; s[8] = 'T';
    md5_init(&m); md5_update(&m, w->key, 16); md5_update(&m, s, 9); md5_final(&m, key);
    k = key;
  }
  /* the IV: MD5 of a random seed and a counter */
  memcpy(t, w->ivseed, 16);
  w->ivn++;
  for (i = 0; i < 4; i++) t[16 + i] = (u8)(w->ivn >> (8 * i));
  for (i = 0; i < 4; i++) t[20 + i] = (u8)((u32)w->cur >> (8 * i));
  md5(t, 24, iv);
  if (!(padded = (u8 *)malloc(pn))) return NULL;
  if (n) memcpy(padded, in, n);
  memset(padded + n, (int)(pn - n), pn - n);
  c = aes_cbc_encrypt_nopad(k, w->crypt == SAVE_AES256 ? 32 : 16, iv, padded, pn);
  free(padded);
  if (!c || !(res = (u8 *)malloc(pn + 16))) { free(c); return NULL; }
  memcpy(res, iv, 16); memcpy(res + 16, c, pn);
  free(c);
  *outlen = pn + 16;
  return res;
}

/* ------------------------------------------------------------------ objects */
static int xgrow(W *w, int num)
{
  if (num >= w->capx) {
    int nc = w->capx ? w->capx * 2 : 512;
    u8 *t; long *f2; int *f3;
    while (nc <= num) nc *= 2;
    t = (u8 *)realloc(w->xtype, (size_t)nc); if (!t) { w->out.oom = 1; return 0; } w->xtype = t;
    f2 = (long *)realloc(w->xf2, (size_t)nc * sizeof(long)); if (!f2) { w->out.oom = 1; return 0; } w->xf2 = f2;
    f3 = (int *)realloc(w->xf3, (size_t)nc * sizeof(int)); if (!f3) { w->out.oom = 1; return 0; } w->xf3 = f3;
    memset(w->xtype + w->capx, 0, (size_t)(nc - w->capx));
    memset(w->xf2 + w->capx, 0, (size_t)(nc - w->capx) * sizeof(long));
    memset(w->xf3 + w->capx, 0, (size_t)(nc - w->capx) * sizeof(int));
    w->capx = nc;
  }
  if (num + 1 > w->noffs) w->noffs = num + 1;
  return 1;
}
static int wmap(W *w, int old)
{
  if (old <= 0 || old >= w->nmap) return 0;
  if (w->o && w->o->alias && old < w->o->nalias && w->o->alias[old] > 0 && w->o->alias[old] != old && w->o->alias[old] < w->nmap) old = w->o->alias[old];
  if (w->map[old] == -1) return 0;
  if (!w->map[old]) {
    if (!pdf_get(w->d, old)) return 0;
    w->map[old] = w->next++;
    if (w->nq >= w->capq) {
      int nc = w->capq ? w->capq * 2 : 256;
      int *q = (int *)realloc(w->queue, (size_t)nc * sizeof(int));
      if (!q) return 0;
      w->queue = q; w->capq = nc;
    }
    w->queue[w->nq++] = old;
  }
  return w->map[old];
}
static int regular(int c) { return c > 32 && c < 127 && !strchr("()<>[]{}/%#", c); }
static void put_name(Buf *b, const char *s, int len)
{
  int i;
  buf_addc(b, '/');
  for (i = 0; i < len; i++) {
    u8 c = (u8)s[i];
    if (regular(c)) buf_addc(b, c); else buf_printf(b, "#%02X", c);
  }
}
static void put_hex(Buf *b, const u8 *s, size_t len)
{
  size_t i;
  buf_addc(b, '<');
  for (i = 0; i < len; i++) buf_printf(b, "%02X", s[i]);
  buf_addc(b, '>');
}
static void put_string(W *w, Buf *b, const char *s, int len)
{
  int i, bin = 0;
  if (w->crypt && !w->nocrypt) {
    size_t n;
    u8 *e = encrypt_bytes(w, (const u8 *)s, (size_t)len, &n);
    if (!e) { b->oom = 1; return; }
    put_hex(b, e, n);
    free(e);
    return;
  }
  for (i = 0; i < len; i++) if ((u8)s[i] < 32 && s[i] != 10 && s[i] != 13 && s[i] != 9) bin++;
  if (bin > len / 8) { put_hex(b, (const u8 *)s, (size_t)len); return; }
  buf_addc(b, '(');
  for (i = 0; i < len; i++) {
    u8 c = (u8)s[i];
    if (c == '(' || c == ')' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
    else if (c == 13) buf_adds(b, "\\r");
    else if (c < 32 && c != 10 && c != 9) buf_printf(b, "\\%03o", c);
    else buf_addc(b, c);
  }
  buf_addc(b, ')');
}
static void put_num(Buf *b, double v)
{
  char t[40];
  int n;
  if (fabs(v) < 1e-9) v = 0;
  if (v == floor(v) && fabs(v) < 2e9) { buf_printf(b, "%ld", (long)v); return; }
  n = snprintf(t, sizeof t, "%.6f", v);
  while (n > 0 && t[n - 1] == '0') t[--n] = 0;
  if (n > 0 && t[n - 1] == '.') t[--n] = 0;
  buf_adds(b, t);
}
/* a reference to a new number already given out (num), as put_obj writes it */
static PObj *newref(int num)
{
  PObj *r = pobj_new(PO_REF);
  if (r) { r->u.ref.num = -num; r->u.ref.gen = 0; }
  return r;
}
static void put_obj(W *w, const PObj *o, int depth)
{
  Buf *b = w->b;
  int i;
  if (!o || depth > 64) { buf_adds(b, "null"); return; }
  switch (o->type) {
  case PO_NULL: buf_adds(b, "null"); break;
  case PO_BOOL: buf_adds(b, o->u.b ? "true" : "false"); break;
  case PO_INT: buf_printf(b, "%ld", o->u.i); break;
  case PO_REAL: put_num(b, o->u.r); break;
  case PO_NAME: put_name(b, o->u.str.s, o->u.str.len); break;
  case PO_STRING: put_string(w, b, o->u.str.s, o->u.str.len); break;
  case PO_ARRAY:
    buf_addc(b, '[');
    for (i = 0; i < o->u.arr.n; i++) { if (i) buf_addc(b, ' '); put_obj(w, o->u.arr.v[i], depth + 1); }
    buf_addc(b, ']');
    break;
  case PO_DICT: case PO_STREAM:
    buf_adds(b, "<<");
    for (i = 0; i < o->u.dict.n; i++) {
      put_name(b, o->u.dict.k[i], (int)strlen(o->u.dict.k[i]));
      buf_addc(b, ' ');
      put_obj(w, o->u.dict.v[i], depth + 1);
      buf_addc(b, '\n');
    }
    buf_adds(b, ">>");
    break;
  case PO_REF: {
    int n = o->u.ref.num < 0 ? -o->u.ref.num : wmap(w, o->u.ref.num);
    if (n) buf_printf(b, "%d 0 R", n); else buf_adds(b, "null");
    break; }
  }
}
/* one object written as "n 0 obj ... endobj" at the current offset */
static void begin_obj(W *w, int num)
{
  if (!xgrow(w, num)) return;
  w->xtype[num] = 1;
  w->xf2[num] = (long)w->out.len;
  buf_printf(&w->out, "%d 0 obj\n", num);
}
/* a stream's dictionary and data, compressed and encrypted as the options say */
static void write_stream(W *w, int num, PObj *o, u8 *data, size_t len)
{
  PObj *dict = pobj_new(PO_DICT);
  int i, xref = pobj_is_name(pobj_dict_get(o, "Type"), "XRef");
  for (i = 0; dict && i < o->u.dict.n; i++) if (strcmp(o->u.dict.k[i], "Length")) pobj_dict_set(dict, o->u.dict.k[i], pobj_copy(o->u.dict.v[i]));
  if (dict && data && w->o && w->o->compress && !pobj_dict_get(o, "Filter") && len > 32) {
    size_t zl;
    u8 *z = flt_deflate(data, len, w->o->level > 0 ? w->o->level : 6, &zl);
    if (z && zl + 20 < len) { free(data); data = z; len = zl; pobj_dict_set(dict, "Filter", pobj_name("FlateDecode")); pobj_dict_del(dict, "DecodeParms"); }
    else free(z);
  }
  if (data && w->crypt && !xref) {
    size_t el;
    u8 *e;
    w->cur = num;
    if ((e = encrypt_bytes(w, data, len, &el))) { free(data); data = e; len = el; }
    else { free(data); data = NULL; len = 0; w->out.oom = 1; }
  }
  if (!dict) { free(data); w->out.oom = 1; return; }
  pobj_dict_set(dict, "Length", pobj_int((long)len));
  begin_obj(w, num);
  w->b = &w->out;
  w->cur = num;
  put_obj(w, dict, 0);
  pobj_free(dict);
  buf_adds(&w->out, "\nstream\n");
  if (data) buf_add(&w->out, data, len);
  buf_adds(&w->out, "\nendstream\nendobj\n");
  free(data);
}
/* the object stream being filled written out */
static void flush_objstm(W *w)
{
  PObj *s;
  int num;
  Buf all;
  if (!w->os_n) return;
  num = w->next++;
  buf_init(&all);
  buf_add(&all, w->osi.data, w->osi.len);
  buf_add(&all, w->os.data, w->os.len);
  s = pobj_new(PO_STREAM);
  if (s && !all.oom) {
    u8 *data;
    size_t len = all.len;
    int i;
    pobj_dict_set(s, "Type", pobj_name("ObjStm"));
    pobj_dict_set(s, "N", pobj_int(w->os_n));
    pobj_dict_set(s, "First", pobj_int((long)w->osi.len));
    data = (u8 *)buf_take(&all);
    if (!xgrow(w, num)) { free(data); pobj_free(s); return; }
    for (i = 0; i < w->os_n; i++) { xgrow(w, w->os_nums[i]); w->xtype[w->os_nums[i]] = 2; w->xf2[w->os_nums[i]] = num; w->xf3[w->os_nums[i]] = i; }
    /* (always compressed: that is what it is for) */
    {
      size_t zl;
      u8 *z = flt_deflate(data, len, w->o && w->o->level > 0 ? w->o->level : 6, &zl);
      if (z) { free(data); data = z; len = zl; pobj_dict_set(s, "Filter", pobj_name("FlateDecode")); }
    }
    write_stream(w, num, s, data, len);
  } else w->out.oom = 1;
  pobj_free(s);
  buf_free(&all);
  w->os.len = 0; w->osi.len = 0; w->os_n = 0;
}
/* an object (not a stream) written: into an object stream when packing, else directly */
static void emit(W *w, int num, const PObj *o)
{
  if (o && o->type == PO_STREAM) {
    size_t len = 0;
    u8 *data = o->sdata || o->soff >= 0 ? pdf_stream_bytes(w->d, (PObj *)o, &len) : NULL;
    write_stream(w, num, (PObj *)o, data, len);
    return;
  }
  w->cur = num;
  if (w->o && w->o->objstm && num != w->enc_num) {
    if (w->os_n >= 100) flush_objstm(w);
    buf_printf(&w->osi, "%d %ld ", num, (long)w->os.len);
    w->os_nums[w->os_n++] = num;
    w->b = &w->os;
    w->nocrypt++;                       /* (the object stream is encrypted as a whole) */
    put_obj(w, o, 0);
    w->nocrypt--;
    buf_addc(&w->os, '\n');
    w->b = &w->out;
    return;
  }
  begin_obj(w, num);
  w->b = &w->out;
  put_obj(w, o, 0);
  buf_adds(&w->out, "\nendobj\n");
}
u8 *pdf_save(PDoc *d, const int *order, int n, size_t *outlen) { return pdf_save_opts(d, order, n, NULL, outlen); }
u8 *pdf_save_opts(PDoc *d, const int *order, int n, const SaveOpts *opts, size_t *outlen)
{
  W w;
  int i, root_new, info_new = 0, k;
  PObj *root = pdf_dget(d, d->trailer, "Root"), *cat, *info_ref = pobj_dict_get(d->trailer, "Info"), *encd = NULL, *id;
  u8 *res = NULL, idb[2][16];
  int idl[2] = { 16, 16 };
  memset(&w, 0, sizeof w);
  w.d = d; w.o = opts;
  if (!root) return NULL;
  if (!order) n = d->npages;
  if (n < 1) return NULL;
  w.nmap = d->nxref + 1;
  buf_init(&w.out); buf_init(&w.os); buf_init(&w.osi);
  w.b = &w.out;
  if (!(w.map = (int *)calloc((size_t)w.nmap, sizeof(int))) || !(w.pages = (PObj **)calloc((size_t)n, sizeof(PObj *))) ||
      !(w.page_new = (int *)calloc((size_t)n, sizeof(int)))) goto done;
  buf_adds(&w.out, "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
  w.next = 1;
  /* the file identifier: the document's own, or a new one */
  id = pdf_dget(d, d->trailer, "ID");
  for (i = 0; i < 2; i++) {
    PObj *e = id && id->type == PO_ARRAY && id->u.arr.n > i ? pdf_resolve(d, id->u.arr.v[i]) : NULL;
    if (e && e->type == PO_STRING && e->u.str.len > 0) { idl[i] = e->u.str.len > 16 ? 16 : e->u.str.len; memcpy(idb[i], e->u.str.s, (size_t)idl[i]); }
    else { pdf_random(idb[i], 16, d->data, d->len > 64 ? 64 : d->len); idl[i] = 16; }
  }
  if (opts && (opts->crypt == SAVE_AES128 || opts->crypt == SAVE_AES256) && !(encd = make_encrypt(&w, idb[0], idl[0]))) goto done;
  /* the pages kept; the others (and the old tree's nodes) left out */
  for (i = 0; i < n; i++) w.pages[i] = order ? (order[i] >= 0 && order[i] < d->npages ? d->pages[order[i]] : NULL) : d->pages[i];
  for (k = 1; k < d->nxref; k++) {
    PObj *o = d->xref[k].obj;
    if (!o) continue;
    if (o->type == PO_DICT && (pobj_is_name(pobj_dict_get(o, "Type"), "Pages") || pobj_is_name(pobj_dict_get(o, "Type"), "Page"))) w.map[k] = -1;
    if (k == d->encrypt_num) w.map[k] = -1;
  }
  /* the kept pages get their numbers first; then the tree's node */
  for (i = 0; i < n; i++) {
    int j;
    if (!w.pages[i]) continue;
    for (j = 1; j < d->nxref; j++) if (d->xref[j].obj == w.pages[i]) break;
    if (j < d->nxref) {
      w.map[j] = w.next++;
      w.page_new[i] = w.map[j];
    } else w.page_new[i] = w.next++;
  }
  w.pages_num = w.next++;
  root_new = w.next++;
  if (encd) w.enc_num = w.next++;
  if (info_ref && info_ref->type == PO_REF) info_new = wmap(&w, info_ref->u.ref.num);
  /* the pages: each a copy with its inherited attributes and the new tree as its parent */
  for (i = 0; i < n; i++) {
    PObj *pg = w.pages[i], *c;
    static const char *const inh[] = { "Resources", "MediaBox", "CropBox", "Rotate" };
    int a;
    if (!pg) continue;
    if (!(c = pobj_copy(pg))) goto done;
    for (a = 0; a < 4; a++) if (!pobj_dict_get(c, inh[a])) { PObj *v = pdf_page_attr(d, pg, inh[a]); if (v) pobj_dict_set(c, inh[a], pobj_copy(v)); }
    pobj_dict_set(c, "Parent", newref(w.pages_num));
    emit(&w, w.page_new[i], c);
    pobj_free(c);
  }
  /* the page tree */
  {
    PObj *t = pobj_new(PO_DICT), *kids = pobj_new(PO_ARRAY);
    if (!t || !kids) { pobj_free(t); pobj_free(kids); goto done; }
    for (i = 0, k = 0; i < n; i++) if (w.pages[i]) { pobj_array_add(kids, newref(w.page_new[i])); k++; }
    pobj_dict_set(t, "Type", pobj_name("Pages"));
    pobj_dict_set(t, "Kids", kids);
    pobj_dict_set(t, "Count", pobj_int(k));
    emit(&w, w.pages_num, t);
    pobj_free(t);
  }
  /* the catalog: the old one with the new tree */
  if (!(cat = pobj_copy(root))) goto done;
  pobj_dict_set(cat, "Pages", newref(w.pages_num));
  emit(&w, root_new, cat);
  pobj_free(cat);
  /* everything they refer to */
  for (i = 0; i < w.nq; i++) {
    int old = w.queue[i];
    emit(&w, w.map[old], pdf_get(d, old));
  }
  flush_objstm(&w);
  if (encd) {
    w.nocrypt++;
    begin_obj(&w, w.enc_num);
    w.b = &w.out;
    put_obj(&w, encd, 0);
    buf_adds(&w.out, "\nendobj\n");
    w.nocrypt--;
  }
  /* the cross-reference table (or stream) and trailer */
  {
    long xref = (long)w.out.len;
    int total = w.next;
    if (opts && opts->objstm) {
      int xnum = w.next++;
      Buf xb;
      PObj *xs = pobj_new(PO_STREAM), *wa = pobj_new(PO_ARRAY), *ida = pobj_new(PO_ARRAY);
      u8 *xd;
      size_t xl;
      total = w.next;
      if (!xs || !wa || !ida || !xgrow(&w, xnum)) { pobj_free(xs); pobj_free(wa); pobj_free(ida); goto done; }
      w.xtype[xnum] = 1; w.xf2[xnum] = xref;
      buf_init(&xb);
      for (i = 0; i < total; i++) {
        int t = i == 0 ? 0 : i < w.noffs ? w.xtype[i] : 0;
        long f2 = i == 0 ? 0 : i < w.noffs ? w.xf2[i] : 0;
        int f3 = i == 0 ? 65535 : t == 2 ? w.xf3[i] : 0;
        buf_addc(&xb, t);
        buf_addc(&xb, (int)((f2 >> 24) & 255)); buf_addc(&xb, (int)((f2 >> 16) & 255)); buf_addc(&xb, (int)((f2 >> 8) & 255)); buf_addc(&xb, (int)(f2 & 255));
        buf_addc(&xb, (f3 >> 8) & 255); buf_addc(&xb, f3 & 255);
      }
      pobj_dict_set(xs, "Type", pobj_name("XRef"));
      pobj_dict_set(xs, "Size", pobj_int(total));
      pobj_array_add(wa, pobj_int(1)); pobj_array_add(wa, pobj_int(4)); pobj_array_add(wa, pobj_int(2));
      pobj_dict_set(xs, "W", wa);
      pobj_dict_set(xs, "Root", newref(root_new));
      if (info_new) pobj_dict_set(xs, "Info", newref(info_new));
      if (encd) pobj_dict_set(xs, "Encrypt", newref(w.enc_num));
      pobj_array_add(ida, bstr(idb[0], idl[0])); pobj_array_add(ida, bstr(idb[1], idl[1]));
      pobj_dict_set(xs, "ID", ida);
      xd = flt_deflate((const u8 *)xb.data, xb.len, 6, &xl);
      if (xd) pobj_dict_set(xs, "Filter", pobj_name("FlateDecode"));
      else { xl = xb.len; xd = (u8 *)buf_take(&xb); if (!xd) xl = 0; }
      w.nocrypt++;
      write_stream(&w, xnum, xs, xd, xl);
      w.nocrypt--;
      pobj_free(xs);
      buf_free(&xb);
      /* (write_stream began the stream's object where startxref points) */
      buf_printf(&w.out, "startxref\n%ld\n%%%%EOF\n", xref);
    } else {
      buf_printf(&w.out, "xref\n0 %d\n0000000000 65535 f \n", total);
      for (i = 1; i < total; i++) {
        long off = i < w.noffs && w.xtype[i] == 1 ? w.xf2[i] : 0;
        if (off) buf_printf(&w.out, "%010ld 00000 n \n", off); else buf_adds(&w.out, "0000000000 00000 f \n");
      }
      buf_printf(&w.out, "trailer\n<</Size %d /Root %d 0 R", total, root_new);
      if (info_new) buf_printf(&w.out, " /Info %d 0 R", info_new);
      if (encd) buf_printf(&w.out, " /Encrypt %d 0 R", w.enc_num);
      buf_adds(&w.out, " /ID ["); put_hex(&w.out, idb[0], (size_t)idl[0]); buf_addc(&w.out, ' '); put_hex(&w.out, idb[1], (size_t)idl[1]); buf_addc(&w.out, ']');
      buf_printf(&w.out, ">>\nstartxref\n%ld\n%%%%EOF\n", xref);
    }
  }
  if (!w.out.oom) { *outlen = w.out.len; res = (u8 *)buf_take(&w.out); }
done:
  buf_free(&w.out); buf_free(&w.os); buf_free(&w.osi);
  pobj_free(encd);
  free(w.map); free(w.queue); free(w.xtype); free(w.xf2); free(w.xf3); free(w.pages); free(w.page_new);
  return res;
}
