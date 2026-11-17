/*
 * pdf.c - reading PDF files (see pdf.h).
 *
 * Every number, length and offset in a file is checked before it is used: PDFs are hostile input,
 * and many real ones are a little broken (wrong offsets, wrong stream lengths, missing xref), so
 * the reader repairs what it can rather than giving up.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "pdf.h"
#include "filters.h"
#include "crypto.h"

#define MAXDEPTH 64                       /* nesting of arrays and dictionaries, and of reference chains */
#define MAXSTREAM (96L * 1024 * 1024)     /* the most a stream may decode to */

/* ------------------------------------------------------------------ objects */
PObj *pobj_new(int type)
{
  PObj *o = (PObj *)calloc(1, sizeof *o);
  if (o) o->type = (u8)type;
  return o;
}
void pobj_free(PObj *o)
{
  int i;
  if (!o) return;
  switch (o->type) {
  case PO_NAME: case PO_STRING: free(o->u.str.s); break;
  case PO_ARRAY: for (i = 0; i < o->u.arr.n; i++) pobj_free(o->u.arr.v[i]); free(o->u.arr.v); break;
  case PO_DICT: case PO_STREAM:
    for (i = 0; i < o->u.dict.n; i++) { free(o->u.dict.k[i]); pobj_free(o->u.dict.v[i]); }
    free(o->u.dict.k); free(o->u.dict.v);
    free(o->sdata);
    break;
  }
  free(o);
}
PObj *pobj_int(long v) { PObj *o = pobj_new(PO_INT); if (o) o->u.i = v; return o; }
PObj *pobj_real(double v) { PObj *o = pobj_new(PO_REAL); if (o) o->u.r = v; return o; }
/* a string or name of len bytes (a terminating NUL added, not counted) */
static PObj *pobj_bytes(int type, const char *s, int len)
{
  PObj *o = pobj_new(type);
  if (!o) return NULL;
  if (!(o->u.str.s = (char *)malloc((size_t)len + 1))) { free(o); return NULL; }
  if (len) memcpy(o->u.str.s, s, (size_t)len);
  o->u.str.s[len] = 0;
  o->u.str.len = len;
  return o;
}
PObj *pobj_name(const char *s) { return pobj_bytes(PO_NAME, s, (int)strlen(s)); }
PObj *pobj_string(const char *s, int len) { return pobj_bytes(PO_STRING, s, len); }
PObj *pobj_copy(const PObj *o)
{
  PObj *c;
  int i;
  if (!o) return NULL;
  switch (o->type) {
  case PO_NAME: case PO_STRING: return pobj_bytes(o->type, o->u.str.s, o->u.str.len);
  case PO_ARRAY:
    if (!(c = pobj_new(PO_ARRAY))) return NULL;
    for (i = 0; i < o->u.arr.n; i++) pobj_array_add(c, pobj_copy(o->u.arr.v[i]));
    return c;
  case PO_DICT: case PO_STREAM:
    if (!(c = pobj_new(o->type))) return NULL;
    for (i = 0; i < o->u.dict.n; i++) pobj_dict_set(c, o->u.dict.k[i], pobj_copy(o->u.dict.v[i]));
    c->soff = o->soff; c->slen = o->slen; c->onum = o->onum; c->ogen = o->ogen;
    if (o->sdata && o->slen > 0 && (c->sdata = (u8 *)malloc((size_t)o->slen))) memcpy(c->sdata, o->sdata, (size_t)o->slen);
    return c;
  default:
    if (!(c = pobj_new(o->type))) return NULL;
    c->u = o->u;
    return c;
  }
}
void pobj_array_add(PObj *a, PObj *item)
{
  if (!a || a->type != PO_ARRAY) { pobj_free(item); return; }
  if (a->u.arr.n >= a->u.arr.cap) {
    int nc = a->u.arr.cap ? a->u.arr.cap * 2 : 8;
    PObj **v = (PObj **)realloc(a->u.arr.v, (size_t)nc * sizeof(PObj *));
    if (!v) { pobj_free(item); return; }
    a->u.arr.v = v; a->u.arr.cap = nc;
  }
  a->u.arr.v[a->u.arr.n++] = item ? item : pobj_new(PO_NULL);
}
void pobj_dict_set(PObj *d, const char *key, PObj *v)
{
  int i;
  char *k;
  if (!d || (d->type != PO_DICT && d->type != PO_STREAM)) { pobj_free(v); return; }
  for (i = 0; i < d->u.dict.n; i++)
    if (!strcmp(d->u.dict.k[i], key)) { pobj_free(d->u.dict.v[i]); d->u.dict.v[i] = v ? v : pobj_new(PO_NULL); return; }
  if (d->u.dict.n >= d->u.dict.cap) {
    int nc = d->u.dict.cap ? d->u.dict.cap * 2 : 8;
    char **kk = (char **)realloc(d->u.dict.k, (size_t)nc * sizeof(char *));
    PObj **vv;
    if (!kk) { pobj_free(v); return; }
    d->u.dict.k = kk;
    if (!(vv = (PObj **)realloc(d->u.dict.v, (size_t)nc * sizeof(PObj *)))) { pobj_free(v); return; }
    d->u.dict.v = vv; d->u.dict.cap = nc;
  }
  if (!(k = str_dup(key))) { pobj_free(v); return; }
  d->u.dict.k[d->u.dict.n] = k;
  d->u.dict.v[d->u.dict.n++] = v ? v : pobj_new(PO_NULL);
}
void pobj_dict_del(PObj *d, const char *key)
{
  int i;
  if (!d || (d->type != PO_DICT && d->type != PO_STREAM)) return;
  for (i = 0; i < d->u.dict.n; i++)
    if (!strcmp(d->u.dict.k[i], key)) {
      free(d->u.dict.k[i]); pobj_free(d->u.dict.v[i]);
      memmove(&d->u.dict.k[i], &d->u.dict.k[i + 1], (size_t)(d->u.dict.n - i - 1) * sizeof(char *));
      memmove(&d->u.dict.v[i], &d->u.dict.v[i + 1], (size_t)(d->u.dict.n - i - 1) * sizeof(PObj *));
      d->u.dict.n--;
      return;
    }
}
PObj *pobj_dict_get(const PObj *d, const char *key)
{
  int i;
  if (!d || (d->type != PO_DICT && d->type != PO_STREAM)) return NULL;
  for (i = 0; i < d->u.dict.n; i++) if (!strcmp(d->u.dict.k[i], key)) return d->u.dict.v[i];
  return NULL;
}
int pobj_is_name(const PObj *o, const char *s) { return o && o->type == PO_NAME && !strcmp(o->u.str.s, s); }
double pobj_num(const PObj *o) { return !o ? 0 : o->type == PO_INT ? (double)o->u.i : o->type == PO_REAL ? o->u.r : 0; }
int pobj_isnum(const PObj *o) { return o && (o->type == PO_INT || o->type == PO_REAL); }

/* ------------------------------------------------------------------ the lexer */
static int is_ws(int c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
static int is_delim(int c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
void lex_init(Lex *l, const u8 *p, size_t n, PDoc *doc) { l->p = p; l->end = p + n; l->doc = doc; }
void lex_skip_ws(Lex *l)
{
  while (l->p < l->end) {
    if (is_ws(*l->p)) l->p++;
    else if (*l->p == '%') { while (l->p < l->end && *l->p != 10 && *l->p != 13) l->p++; }
    else break;
  }
}
void tok_free(Token *t) { free(t->s); t->s = NULL; }
/* grows a token's text buffer to hold one more byte */
static int tadd(Token *t, int *cap, int c)
{
  if (t->len + 1 >= *cap) {
    int nc = *cap ? *cap * 2 : 32;
    char *s = (char *)realloc(t->s, (size_t)nc);
    if (!s) return 0;
    t->s = s; *cap = nc;
  }
  t->s[t->len++] = (char)c;
  t->s[t->len] = 0;
  return 1;
}
int lex_next(Lex *l, Token *t)
{
  int c, cap = 0;
  memset(t, 0, sizeof *t);
  lex_skip_ws(l);
  if (l->p >= l->end) return t->type = TK_EOF;
  c = *l->p;
  if (c == '[') { l->p++; return t->type = TK_ARRAY_OPEN; }
  if (c == ']') { l->p++; return t->type = TK_ARRAY_CLOSE; }
  if (c == '{') { l->p++; return t->type = TK_BRACE_OPEN; }
  if (c == '}') { l->p++; return t->type = TK_BRACE_CLOSE; }
  if (c == '<' && l->p + 1 < l->end && l->p[1] == '<') { l->p += 2; return t->type = TK_DICT_OPEN; }
  if (c == '>' && l->p + 1 < l->end && l->p[1] == '>') { l->p += 2; return t->type = TK_DICT_CLOSE; }
  if (c == '/') {
    l->p++;
    t->type = TK_NAME;
    if (!tadd(t, &cap, 0)) return t->type = TK_ERROR;
    t->len = 0;
    while (l->p < l->end && !is_ws(*l->p) && !is_delim(*l->p)) {
      int ch = *l->p++;
      if (ch == '#' && l->p + 1 < l->end && hexval(l->p[0]) >= 0 && hexval(l->p[1]) >= 0) { ch = hexval(l->p[0]) * 16 + hexval(l->p[1]); l->p += 2; }
      if (!tadd(t, &cap, ch)) return t->type = TK_ERROR;
    }
    return TK_NAME;
  }
  if (c == '(') {
    int depth = 1;
    l->p++;
    t->type = TK_STRING;
    if (!tadd(t, &cap, 0)) return t->type = TK_ERROR;
    t->len = 0;
    while (l->p < l->end) {
      int ch = *l->p++;
      if (ch == '(') depth++;
      else if (ch == ')') { if (!--depth) break; }
      else if (ch == '\\' && l->p < l->end) {
        ch = *l->p++;
        switch (ch) {
        case 'n': ch = 10; break; case 'r': ch = 13; break; case 't': ch = 9; break;
        case 'b': ch = 8; break; case 'f': ch = 12; break;
        case 13: if (l->p < l->end && *l->p == 10) l->p++;   /* fall through: a line continuation */
        case 10: continue;
        default:
          if (ch >= '0' && ch <= '7') {
            int v = ch - '0', k;
            for (k = 0; k < 2 && l->p < l->end && *l->p >= '0' && *l->p <= '7'; k++) v = v * 8 + (*l->p++ - '0');
            ch = v & 255;
          }
        }
      } else if (ch == 13) {                /* a bare end of line in a string is a line feed */
        if (l->p < l->end && *l->p == 10) l->p++;
        ch = 10;
      }
      if (!tadd(t, &cap, ch)) return t->type = TK_ERROR;
    }
    return TK_STRING;
  }
  if (c == '<') {
    int hi = -1;
    l->p++;
    t->type = TK_HEXSTRING;
    if (!tadd(t, &cap, 0)) return t->type = TK_ERROR;
    t->len = 0;
    while (l->p < l->end && *l->p != '>') {
      int v = hexval(*l->p++);
      if (v < 0) continue;
      if (hi < 0) hi = v;
      else { if (!tadd(t, &cap, hi * 16 + v)) return t->type = TK_ERROR; hi = -1; }
    }
    if (hi >= 0 && !tadd(t, &cap, hi * 16)) return t->type = TK_ERROR;
    if (l->p < l->end) l->p++;
    t->type = TK_STRING;
    return TK_STRING;
  }
  if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
    /* a number: digits, at most one point; extra signs (as some producers write "--5") tolerated */
    int neg = 0, dot = 0, digits = 0;
    double v = 0, scale = 1;
    long iv = 0;
    int big = 0;
    while (l->p < l->end && (*l->p == '+' || *l->p == '-')) { if (*l->p == '-') neg = !neg; l->p++; }
    while (l->p < l->end) {
      int ch = *l->p;
      if (ch >= '0' && ch <= '9') {
        digits++;
        if (dot) { scale /= 10; v += (ch - '0') * scale; }
        else { v = v * 10 + (ch - '0'); if (iv < 100000000000L) iv = iv * 10 + (ch - '0'); else big = 1; }
      } else if (ch == '.' && !dot) dot = 1;
      else break;
      l->p++;
    }
    if (!digits && !dot) { /* a lone sign: treat as 0 */ }
    if (dot || big || iv > 2147483647L) { t->type = TK_REAL; t->r = neg ? -v : v; return TK_REAL; }
    t->type = TK_INT; t->i = neg ? -iv : iv; t->r = (double)t->i;
    return TK_INT;
  }
  if (c == ')' || c == '>') { l->p++; return t->type = TK_ERROR; }
  /* a keyword (operator, true, false, null, R, obj, stream...) */
  t->type = TK_KEYWORD;
  if (!tadd(t, &cap, 0)) return t->type = TK_ERROR;
  t->len = 0;
  while (l->p < l->end && !is_ws(*l->p) && !is_delim(*l->p)) {
    if (!tadd(t, &cap, *l->p++)) return t->type = TK_ERROR;
    if (t->len > 64) break;
  }
  if (!t->len) { l->p++; return t->type = TK_ERROR; }
  return TK_KEYWORD;
}
static PObj *parse_obj(Lex *l, Token *t, int depth, char **kw);
/* "a b R" after an integer a: looks ahead without consuming unless it is a reference */
static PObj *maybe_ref(Lex *l, long a)
{
  const u8 *save = l->p;
  Token t2, t3;
  if (lex_next(l, &t2) == TK_INT && t2.i >= 0) {
    if (lex_next(l, &t3) == TK_KEYWORD && !strcmp(t3.s, "R")) {
      PObj *o = pobj_new(PO_REF);
      tok_free(&t3);
      if (o) { o->u.ref.num = (int)a; o->u.ref.gen = (int)t2.i; }
      return o;
    }
    tok_free(&t3);
  }
  tok_free(&t2);
  l->p = save;
  return NULL;
}
static PObj *parse_obj(Lex *l, Token *t, int depth, char **kw)
{
  PObj *o = NULL;
  if (depth > MAXDEPTH) return NULL;
  switch (t->type) {
  case TK_INT:
    if (t->i >= 0 && (o = maybe_ref(l, t->i))) return o;
    return pobj_int(t->i);
  case TK_REAL: return pobj_real(t->r);
  case TK_NAME: o = pobj_bytes(PO_NAME, t->s, t->len); return o;
  case TK_STRING: return pobj_bytes(PO_STRING, t->s ? t->s : "", t->len);
  case TK_ARRAY_OPEN:
    if (!(o = pobj_new(PO_ARRAY))) return NULL;
    for (;;) {
      Token u;
      PObj *item;
      int ty = lex_next(l, &u);
      if (ty == TK_ARRAY_CLOSE || ty == TK_EOF) { tok_free(&u); break; }
      if (ty == TK_DICT_CLOSE || ty == TK_ERROR) { tok_free(&u); continue; }
      if (ty == TK_KEYWORD) {
        /* true/false/null inside; any other keyword ends a broken array */
        if (!strcmp(u.s, "true") || !strcmp(u.s, "false")) { item = pobj_new(PO_BOOL); if (item) item->u.b = u.s[0] == 't'; }
        else if (!strcmp(u.s, "null")) item = pobj_new(PO_NULL);
        else if (!strcmp(u.s, "endobj") || !strcmp(u.s, "stream") || !strcmp(u.s, "obj")) { l->p -= u.len; tok_free(&u); break; }
        else { tok_free(&u); continue; }
        tok_free(&u);
      } else { item = parse_obj(l, &u, depth + 1, NULL); tok_free(&u); }
      pobj_array_add(o, item);
    }
    return o;
  case TK_DICT_OPEN:
    if (!(o = pobj_new(PO_DICT))) return NULL;
    for (;;) {
      Token k, u;
      int ty = lex_next(l, &k), tv;
      PObj *v;
      if (ty == TK_DICT_CLOSE || ty == TK_EOF) { tok_free(&k); break; }
      if (ty == TK_KEYWORD && (!strcmp(k.s, "endobj") || !strcmp(k.s, "stream"))) { l->p -= k.len; tok_free(&k); break; }
      if (ty != TK_NAME) { tok_free(&k); continue; }
      tv = lex_next(l, &u);
      if (tv == TK_DICT_CLOSE || tv == TK_EOF) { pobj_dict_set(o, k.s, pobj_new(PO_NULL)); tok_free(&k); tok_free(&u); break; }
      if (tv == TK_KEYWORD) {
        if (!strcmp(u.s, "true") || !strcmp(u.s, "false")) { v = pobj_new(PO_BOOL); if (v) v->u.b = u.s[0] == 't'; }
        else if (!strcmp(u.s, "endobj") || !strcmp(u.s, "stream")) { l->p -= u.len; tok_free(&u); tok_free(&k); break; }
        else v = pobj_new(PO_NULL);
      } else v = parse_obj(l, &u, depth + 1, NULL);
      tok_free(&u);
      pobj_dict_set(o, k.s, v);
      tok_free(&k);
    }
    return o;
  case TK_KEYWORD:
    if (!strcmp(t->s, "true") || !strcmp(t->s, "false")) { o = pobj_new(PO_BOOL); if (o) o->u.b = t->s[0] == 't'; return o; }
    if (!strcmp(t->s, "null")) return pobj_new(PO_NULL);
    if (kw) { *kw = t->s; t->s = NULL; }
    return NULL;
  case TK_BRACE_OPEN: {
    /* a PostScript procedure (Type 4 functions): kept as an array of tokens, keywords as names
       marked by a leading '{' or '}' char */
    if (!(o = pobj_new(PO_ARRAY))) return NULL;
    for (;;) {
      Token u;
      int ty = lex_next(l, &u);
      if (ty == TK_BRACE_CLOSE || ty == TK_EOF || ty == TK_ERROR) { tok_free(&u); break; }
      if (ty == TK_KEYWORD) pobj_array_add(o, pobj_bytes(PO_NAME, u.s, u.len)), o->u.arr.v[o->u.arr.n - 1]->type = PO_NAME, o->u.arr.v[o->u.arr.n - 1]->onum = -1;
      else pobj_array_add(o, parse_obj(l, &u, depth + 1, NULL));
      tok_free(&u);
    }
    o->onum = -2;                          /* (marks a procedure) */
    return o;
  }
  default: return NULL;
  }
}
PObj *lex_object(Lex *l, char **kw)
{
  Token t;
  PObj *o;
  if (kw) *kw = NULL;
  for (;;) {
    int ty = lex_next(l, &t);
    if (ty == TK_EOF) return NULL;
    if (ty == TK_ERROR || ty == TK_ARRAY_CLOSE || ty == TK_DICT_CLOSE || ty == TK_BRACE_CLOSE) { tok_free(&t); if (kw) { *kw = str_dup("?"); return NULL; } continue; }
    break;
  }
  o = parse_obj(l, &t, 0, kw);
  tok_free(&t);
  return o;
}

/* ------------------------------------------------------------------ decryption */
static const u8 pad32[32] = { 0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                              0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A };
/* the key for object num, gen (RC4 and AES-128 derive one per object; AES-256 uses the file key) */
static int obj_key(PDoc *d, int num, int gen, int aes, u8 *out)
{
  Md5 m;
  u8 b[5], h[16];
  int n;
  if (d->crypt == 3) { memcpy(out, d->key, 32); return 32; }
  md5_init(&m);
  md5_update(&m, d->key, (size_t)d->keylen);
  b[0] = (u8)num; b[1] = (u8)(num >> 8); b[2] = (u8)(num >> 16); b[3] = (u8)gen; b[4] = (u8)(gen >> 8);
  md5_update(&m, b, 5);
  if (aes) md5_update(&m, (const u8 *)"sAlT", 4);
  md5_final(&m, h);
  n = d->keylen + 5 > 16 ? 16 : d->keylen + 5;
  memcpy(out, h, (size_t)n);
  return n;
}
/* data decrypted with the method m (1 RC4, 2/3 AES); malloc'd, *outlen; NULL on failure */
static u8 *decrypt_bytes(PDoc *d, int m, int num, int gen, const u8 *in, size_t n, size_t *outlen)
{
  u8 k[32];
  int kl = obj_key(d, num, gen, m >= 2, k);
  if (m == 1) {
    Rc4 r;
    u8 *o = (u8 *)malloc(n ? n : 1);
    if (!o) return NULL;
    rc4_init(&r, k, kl);
    rc4_crypt(&r, in, o, n);
    *outlen = n;
    return o;
  }
  if (n < 16) { u8 *o = (u8 *)malloc(1); *outlen = 0; return o; }
  return aes_cbc_decrypt(k, kl, in, n, outlen);
}
/* every string inside o decrypted in place (for indirect object num, gen) */
static void decrypt_obj(PDoc *d, PObj *o, int num, int gen, int depth)
{
  int i;
  if (!o || depth > MAXDEPTH || !d->crypt_strings) return;
  if (o->type == PO_STRING) {
    size_t n;
    u8 *p = decrypt_bytes(d, d->crypt_strings, num, gen, (const u8 *)o->u.str.s, (size_t)o->u.str.len, &n);
    if (p) {
      char *s = (char *)realloc(p, n + 1);
      if (s) { s[n] = 0; free(o->u.str.s); o->u.str.s = s; o->u.str.len = (int)n; } else free(p);
    }
  } else if (o->type == PO_ARRAY) {
    for (i = 0; i < o->u.arr.n; i++) decrypt_obj(d, o->u.arr.v[i], num, gen, depth + 1);
  } else if (o->type == PO_DICT || o->type == PO_STREAM) {
    for (i = 0; i < o->u.dict.n; i++) decrypt_obj(d, o->u.dict.v[i], num, gen, depth + 1);
  }
}

/* ------------------------------------------------------------------ indirect objects */
/* the stream data's real length: /Length if "endstream" follows it, else found by searching */
static long stream_length(PDoc *d, size_t start, PObj *dict)
{
  PObj *lo = pobj_dict_get(dict, "Length");
  long len = -1;
  size_t i;
  if (lo && lo->type == PO_REF) {
    /* (an indirect length: never a stream itself, so no loop) */
    PObj *r = pdf_resolve(d, lo);
    if (pobj_isnum(r)) len = (long)pobj_num(r);
  } else if (pobj_isnum(lo)) len = (long)pobj_num(lo);
  if (len >= 0 && start + (size_t)len <= d->len) {
    size_t e = start + (size_t)len;
    while (e < d->len && is_ws(d->data[e])) e++;
    if (e + 9 <= d->len && !memcmp(d->data + e, "endstream", 9)) return len;
  }
  /* search for endstream (the data never contains it legitimately in a sane file) */
  for (i = start; i + 9 <= d->len; i++)
    if (d->data[i] == 'e' && !memcmp(d->data + i, "endstream", 9)) {
      size_t e = i;
      if (e > start && d->data[e - 1] == 10) e--;
      if (e > start && d->data[e - 1] == 13) e--;
      return (long)(e - start);
    }
  return len >= 0 && start + (size_t)len <= d->len ? len : (long)(d->len - start);
}
/* the object at offset off, which should be "num gen obj ... endobj"; NULL if it isn't there.
   *gotnum gets the number it says it is */
static PObj *parse_at(PDoc *d, size_t off, int want, int *gotnum, int *gotgen)
{
  Lex l;
  Token a, b, c;
  PObj *o;
  char *kw = NULL;
  int ok;
  if (off >= d->len) return NULL;
  lex_init(&l, d->data + off, d->len - off, d);
  ok = lex_next(&l, &a) == TK_INT && lex_next(&l, &b) == TK_INT && lex_next(&l, &c) == TK_KEYWORD && !strcmp(c.s, "obj");
  if (ok) { *gotnum = (int)a.i; *gotgen = (int)b.i; }
  tok_free(&a); tok_free(&b); tok_free(&c);
  if (!ok || (want >= 0 && *gotnum != want)) return NULL;
  o = lex_object(&l, &kw);
  free(kw);
  if (!o) return pobj_new(PO_NULL);
  /* a stream follows? */
  if (o->type == PO_DICT) {
    Lex k = l;
    Token t;
    if (lex_next(&k, &t) == TK_KEYWORD && !strcmp(t.s, "stream")) {
      const u8 *p = k.p;
      size_t start;
      if (p < l.end && *p == 13) p++;
      if (p < l.end && *p == 10) p++;
      start = (size_t)(p - d->data);
      o->type = PO_STREAM;
      o->soff = (long)start;
      o->slen = stream_length(d, start, o);
      o->onum = *gotnum; o->ogen = *gotgen;
    }
    tok_free(&t);
  }
  return o;
}
/* an object stream's decoded contents, kept for the next object from the same stream (each
   document keeps the last NOSTM) */
typedef struct { int num; u8 *data; size_t len; long *offs; int *nums; int n, first; } ObjStm;
#define NOSTM 4
static void ostm_free(ObjStm *c) { free(c->data); free(c->offs); free(c->nums); memset(c, 0, sizeof *c); }
static ObjStm *ostm_get(PDoc *d, int num)
{
  int i;
  PObj *s;
  ObjStm *cache, *c;
  Lex l;
  if (!d->ostm && !(d->ostm = calloc(NOSTM, sizeof(ObjStm)))) return NULL;
  cache = (ObjStm *)d->ostm;
  for (i = 0; i < NOSTM; i++) if (cache[i].data && cache[i].num == num) return &cache[i];
  s = pdf_get(d, num);
  if (!s || s->type != PO_STREAM) return NULL;
  /* the oldest goes */
  ostm_free(&cache[0]);
  memmove(&cache[0], &cache[1], (NOSTM - 1) * sizeof(ObjStm));
  c = &cache[NOSTM - 1];
  memset(c, 0, sizeof *c);
  c->num = num;
  if (!(c->data = pdf_stream_data(d, s, &c->len))) return NULL;
  c->n = (int)pdf_dint(d, s, "N", 0);
  c->first = (int)pdf_dint(d, s, "First", 0);
  if (c->n < 0 || c->n > 1000000 || c->first < 0 || (size_t)c->first > c->len) { c->n = 0; return c; }
  c->offs = (long *)calloc((size_t)c->n + 1, sizeof(long));
  c->nums = (int *)calloc((size_t)c->n + 1, sizeof(int));
  if (!c->offs || !c->nums) { c->n = 0; return c; }
  lex_init(&l, c->data, (size_t)c->first, d);
  for (i = 0; i < c->n; i++) {
    Token a, b;
    if (lex_next(&l, &a) != TK_INT) { tok_free(&a); c->n = i; break; }
    if (lex_next(&l, &b) != TK_INT) { tok_free(&a); tok_free(&b); c->n = i; break; }
    c->nums[i] = (int)a.i; c->offs[i] = b.i;
    tok_free(&a); tok_free(&b);
  }
  return c;
}
static PObj *from_objstm(PDoc *d, int stm, int idx, int num)
{
  ObjStm *c = ostm_get(d, stm);
  Lex l;
  size_t off;
  char *kw = NULL;
  PObj *o;
  int i;
  if (!c || !c->n) return NULL;
  if (idx < 0 || idx >= c->n || c->nums[idx] != num) {
    for (i = 0; i < c->n && c->nums[i] != num; i++) ;
    if (i == c->n) return NULL;
    idx = i;
  }
  off = (size_t)c->first + (size_t)c->offs[idx];
  if (off >= c->len) return NULL;
  lex_init(&l, c->data + off, c->len - off, d);
  o = lex_object(&l, &kw);
  free(kw);
  return o;
}
PObj *pdf_get(PDoc *d, int num)
{
  XEntry *x;
  PObj *o = NULL;
  int gn = -1, gg = 0;
  if (!d || num <= 0 || num >= d->nxref) return NULL;
  x = &d->xref[num];
  if (x->obj) return x->obj;
  if (x->loading || x->type == 0) return NULL;
  x->loading = 1;
  if (x->type == 1) {
    o = parse_at(d, (size_t)x->off, num, &gn, &gg);
    if (o) { if (num != d->encrypt_num) decrypt_obj(d, o, num, gg, 0); }
  } else if (x->type == 2) {
    o = from_objstm(d, (int)x->off, x->gen, num);     /* (strings in object streams are already decrypted with the stream) */
  }
  x->loading = 0;
  x->obj = o;
  return o;
}
PObj *pdf_resolve(PDoc *d, PObj *o)
{
  int n = 0;
  while (o && o->type == PO_REF && n++ < MAXDEPTH) o = pdf_get(d, o->u.ref.num);
  if (o && o->type == PO_REF) return NULL;
  return o;
}
PObj *pdf_dget(PDoc *d, const PObj *dict, const char *key) { return pdf_resolve(d, pobj_dict_get(dict, key)); }
double pdf_dnum(PDoc *d, const PObj *dict, const char *key, double def)
{
  PObj *o = pdf_dget(d, dict, key);
  return pobj_isnum(o) ? pobj_num(o) : def;
}
long pdf_dint(PDoc *d, const PObj *dict, const char *key, long def)
{
  PObj *o = pdf_dget(d, dict, key);
  return pobj_isnum(o) ? (long)pobj_num(o) : def;
}
const char *pdf_dname(PDoc *d, const PObj *dict, const char *key)
{
  PObj *o = pdf_dget(d, dict, key);
  return o && o->type == PO_NAME ? o->u.str.s : NULL;
}
PObj *pdf_dict_of(PDoc *d, PObj *o)
{
  o = pdf_resolve(d, o);
  return o && (o->type == PO_DICT || o->type == PO_STREAM) ? o : NULL;
}

/* ------------------------------------------------------------------ streams */
/* the filter at index i of a stream's /Filter (name or array), and its parameters */
static const char *filter_at(PDoc *d, PObj *s, int i, PObj **parms)
{
  PObj *f = pdf_dget(d, s, "Filter"), *p = pdf_dget(d, s, "DecodeParms");
  if (!p) p = pdf_dget(d, s, "DP");
  if (!f) f = pdf_dget(d, s, "F");
  *parms = NULL;
  if (!f) return NULL;
  if (f->type == PO_NAME) { if (i) return NULL; *parms = p && p->type == PO_DICT ? p : NULL; return f->u.str.s; }
  if (f->type == PO_ARRAY && i < f->u.arr.n) {
    PObj *fi = pdf_resolve(d, f->u.arr.v[i]);
    if (p && p->type == PO_ARRAY && i < p->u.arr.n) { PObj *pi = pdf_resolve(d, p->u.arr.v[i]); *parms = pi && pi->type == PO_DICT ? pi : NULL; }
    return fi && fi->type == PO_NAME ? fi->u.str.s : "";
  }
  return NULL;
}
static int is_image_filter(const char *f)
{
  return !strcmp(f, "DCTDecode") || !strcmp(f, "DCT") || !strcmp(f, "JPXDecode") || !strcmp(f, "JBIG2Decode") ||
         !strcmp(f, "CCITTFaxDecode") || !strcmp(f, "CCF");
}
/* the predictor, if parms asks for one */
static u8 *predict(PDoc *d, u8 *data, size_t *len, PObj *parms)
{
  int pr = (int)pdf_dint(d, parms, "Predictor", 1);
  u8 *o;
  size_t n;
  if (pr < 2) return data;
  o = flt_predict(data, *len, pr, (int)pdf_dint(d, parms, "Colors", 1), (int)pdf_dint(d, parms, "BitsPerComponent", 8),
                  (int)pdf_dint(d, parms, "Columns", 1), &n);
  if (!o) return data;
  free(data);
  *len = n;
  return o;
}
/* a stream's bytes as stored, decrypted (filters still to undo); malloc'd */
u8 *pdf_stream_bytes(PDoc *d, PObj *s, size_t *len)
{
  u8 *data;
  size_t n;
  int i;
  const char *f;
  PObj *parms;
  if (!s || s->type != PO_STREAM) return NULL;
  /* the raw bytes (a copy), decrypted */
  if (s->sdata) {
    n = (size_t)s->slen;
    if (!(data = (u8 *)malloc(n ? n : 1))) return NULL;
    memcpy(data, s->sdata, n);
  } else {
    if (s->soff < 0 || (size_t)s->soff > d->len || s->slen < 0) return NULL;
    n = (size_t)s->slen;
    if ((size_t)s->soff + n > d->len) n = d->len - (size_t)s->soff;
    if (!(data = (u8 *)malloc(n ? n : 1))) return NULL;
    memcpy(data, d->data + s->soff, n);
    if (d->crypt_streams && s->onum != d->encrypt_num && !pobj_is_name(pdf_dget(d, s, "Type"), "XRef")) {
      int identity = 0;
      /* a /Crypt filter naming Identity: left as it is */
      for (i = 0; (f = filter_at(d, s, i, &parms)); i++)
        if (!strcmp(f, "Crypt")) { const char *nm = pdf_dname(d, parms, "Name"); identity = !nm || !strcmp(nm, "Identity"); }
      if (!identity) {
        size_t m;
        u8 *p = decrypt_bytes(d, d->crypt_streams, s->onum, s->ogen, data, n, &m);
        free(data);
        if (!p) return NULL;
        data = p; n = m;
      }
    }
  }
  *len = n;
  return data;
}
u8 *pdf_stream_data_raw(PDoc *d, PObj *s, size_t *len, const char **image_filter, PObj **parms_out)
{
  u8 *data;
  size_t n;
  int i;
  const char *f;
  PObj *parms;
  if (image_filter) *image_filter = NULL;
  if (parms_out) *parms_out = NULL;
  if (!s || s->type != PO_STREAM) return NULL;
  if (!(data = pdf_stream_bytes(d, s, &n))) return NULL;
  for (i = 0; (f = filter_at(d, s, i, &parms)); i++) {
    u8 *o = NULL;
    size_t m = 0;
    if (is_image_filter(f)) {
      if (image_filter) { *image_filter = f; if (parms_out) *parms_out = parms; *len = n; return data; }
      break;
    }
    if (!strcmp(f, "FlateDecode") || !strcmp(f, "Fl")) { o = flt_flate(data, n, MAXSTREAM, &m); if (o) { free(data); data = o; n = m; data = predict(d, data, &n, parms); } }
    else if (!strcmp(f, "LZWDecode") || !strcmp(f, "LZW")) {
      o = flt_lzw(data, n, (int)pdf_dint(d, parms, "EarlyChange", 1), MAXSTREAM, &m);
      if (o) { free(data); data = o; n = m; data = predict(d, data, &n, parms); }
    }
    else if (!strcmp(f, "ASCII85Decode") || !strcmp(f, "A85")) { o = flt_ascii85(data, n, &m); if (o) { free(data); data = o; n = m; } }
    else if (!strcmp(f, "ASCIIHexDecode") || !strcmp(f, "AHx")) { o = flt_asciihex(data, n, &m); if (o) { free(data); data = o; n = m; } }
    else if (!strcmp(f, "RunLengthDecode") || !strcmp(f, "RL")) { o = flt_runlength(data, n, MAXSTREAM, &m); if (o) { free(data); data = o; n = m; } }
    else if (!strcmp(f, "Crypt")) continue;
    else break;                                  /* unknown: leave the data as far as it got */
    if (!o) { free(data); return NULL; }
  }
  *len = n;
  return data;
}
u8 *pdf_stream_data(PDoc *d, PObj *s, size_t *len) { return pdf_stream_data_raw(d, s, len, NULL, NULL); }

/* ------------------------------------------------------------------ the cross-reference table */
static int xref_grow(PDoc *d, int n)
{
  XEntry *x;
  if (n <= d->nxref) return 1;
  if (n > 8000000) return 0;
  if (!(x = (XEntry *)realloc(d->xref, (size_t)n * sizeof(XEntry)))) return 0;
  memset(x + d->nxref, 0, (size_t)(n - d->nxref) * sizeof(XEntry));
  d->xref = x; d->nxref = n;
  return 1;
}
/* set entry num unless a newer section already did (sections are read newest first) */
static void xref_set(PDoc *d, int num, int type, long off, int gen)
{
  if (num <= 0 || !xref_grow(d, num + 1)) return;
  if (d->xref[num].type || d->xref[num].loading) return;    /* (loading: marked free by a newer section) */
  d->xref[num].type = (u8)type; d->xref[num].off = off; d->xref[num].gen = gen;
}
/* a free entry in a newer section hides older ones (marked by loading = 2 until the end) */
static void xref_free(PDoc *d, int num)
{
  if (num <= 0 || !xref_grow(d, num + 1)) return;
  if (!d->xref[num].type) d->xref[num].loading = 2;
}
static long find_startxref(PDoc *d)
{
  size_t i, lo = d->len > 4096 ? d->len - 4096 : 0;
  if (d->len < 9) return -1;
  for (i = d->len - 9; ; i--) {
    if (d->data[i] == 's' && !memcmp(d->data + i, "startxref", 9)) {
      Lex l;
      Token t;
      long v = -1;
      lex_init(&l, d->data + i + 9, d->len - i - 9, d);
      if (lex_next(&l, &t) == TK_INT) v = t.i;
      tok_free(&t);
      return v;
    }
    if (i <= lo) break;
  }
  return -1;
}
/* a classic table at off ("xref" ...); its trailer returned (caller frees), NULL if not one */
static PObj *read_xref_table(PDoc *d, size_t off)
{
  Lex l;
  Token t;
  lex_init(&l, d->data + off, d->len - off, d);
  if (lex_next(&l, &t) != TK_KEYWORD || strcmp(t.s, "xref")) { tok_free(&t); return NULL; }
  tok_free(&t);
  for (;;) {
    Token a, b;
    long start, count, k;
    if (lex_next(&l, &a) != TK_INT) {
      int istrailer = a.type == TK_KEYWORD && !strcmp(a.s, "trailer");
      tok_free(&a);
      if (istrailer) { char *kw = NULL; PObj *tr = lex_object(&l, &kw); free(kw); if (tr && tr->type == PO_DICT) return tr; pobj_free(tr); }
      return NULL;
    }
    if (lex_next(&l, &b) != TK_INT) { tok_free(&a); tok_free(&b); return NULL; }
    start = a.i; count = b.i;
    tok_free(&a); tok_free(&b);
    if (start < 0 || count < 0 || count > 8000000) return NULL;
    for (k = 0; k < count; k++) {
      Token o, g, f;
      int ok = lex_next(&l, &o) == TK_INT;
      ok = lex_next(&l, &g) == TK_INT && ok;
      ok = lex_next(&l, &f) == TK_KEYWORD && ok;
      if (ok) {
        if (f.s[0] == 'n' && o.i > 0) xref_set(d, (int)(start + k), 1, o.i, (int)g.i);
        else if (f.s[0] == 'f') xref_free(d, (int)(start + k));
      }
      tok_free(&o); tok_free(&g); tok_free(&f);
      if (!ok) return NULL;
    }
  }
}
/* a cross-reference stream at off; returns its dictionary (a copy the caller frees), NULL if not one */
static PObj *read_xref_stream(PDoc *d, size_t off)
{
  int gn, gg, w[3], i, j;
  PObj *s = parse_at(d, off, -1, &gn, &gg), *wa, *idx, *tr;
  u8 *data;
  size_t n, pos = 0;
  long size;
  if (!s || s->type != PO_STREAM || !pobj_is_name(pobj_dict_get(s, "Type"), "XRef")) { pobj_free(s); return NULL; }
  wa = pobj_dict_get(s, "W");
  if (!wa || wa->type != PO_ARRAY || wa->u.arr.n < 3) { pobj_free(s); return NULL; }
  for (i = 0; i < 3; i++) { w[i] = (int)pobj_num(wa->u.arr.v[i]); if (w[i] < 0 || w[i] > 8) { pobj_free(s); return NULL; } }
  size = (long)pobj_num(pobj_dict_get(s, "Size"));
  /* its own data (an xref stream is never encrypted; filters are direct objects) */
  if (!(data = pdf_stream_data(d, s, &n))) { pobj_free(s); return NULL; }
  idx = pobj_dict_get(s, "Index");
  for (j = 0; ; j += 2) {
    long start, count, k;
    if (idx && idx->type == PO_ARRAY) {
      if (j + 1 >= idx->u.arr.n) break;
      start = (long)pobj_num(idx->u.arr.v[j]); count = (long)pobj_num(idx->u.arr.v[j + 1]);
    } else { if (j) break; start = 0; count = size; }
    if (start < 0 || count < 0 || count > 8000000) break;
    for (k = 0; k < count; k++) {
      long f[3];
      int c;
      if (pos + (size_t)(w[0] + w[1] + w[2]) > n) break;
      for (c = 0; c < 3; c++) {
        int b;
        f[c] = 0;
        for (b = 0; b < w[c]; b++) f[c] = (f[c] << 8) | data[pos++];
      }
      if (!w[0]) f[0] = 1;
      if (f[0] == 1 && f[1] > 0) xref_set(d, (int)(start + k), 1, f[1], (int)f[2]);
      else if (f[0] == 2) xref_set(d, (int)(start + k), 2, f[1], (int)f[2]);
      else if (f[0] == 0) xref_free(d, (int)(start + k));
    }
  }
  free(data);
  /* the trailer: the stream's dictionary, without the stream */
  tr = pobj_new(PO_DICT);
  for (i = 0; tr && i < s->u.dict.n; i++) pobj_dict_set(tr, s->u.dict.k[i], pobj_copy(s->u.dict.v[i]));
  pobj_free(s);
  return tr;
}
/* the chain of sections from startxref, following /Prev; 0 if the first is unreadable */
static int read_xrefs(PDoc *d)
{
  long off = find_startxref(d);
  long seen[64];
  int nseen = 0, k;
  while (off >= 0 && (size_t)off < d->len && nseen < 64) {
    PObj *tr;
    for (k = 0; k < nseen; k++) if (seen[k] == off) return d->trailer != NULL;
    seen[nseen++] = off;
    tr = read_xref_table(d, (size_t)off);
    if (tr) {
      PObj *xs = pobj_dict_get(tr, "XRefStm");      /* a hybrid file: the stream's entries come before the table's older ones */
      if (pobj_isnum(xs)) { PObj *t2 = read_xref_stream(d, (size_t)pobj_num(xs)); pobj_free(t2); }
    } else tr = read_xref_stream(d, (size_t)off);
    if (!tr) return d->trailer != NULL;
    off = pobj_isnum(pobj_dict_get(tr, "Prev")) ? (long)pobj_num(pobj_dict_get(tr, "Prev")) : -1;
    if (!d->trailer) d->trailer = tr; else pobj_free(tr);
  }
  return d->trailer != NULL;
}
/* the table rebuilt by finding every "n g obj" in the file (the last of each number wins), the
   trailer from the last "trailer" or xref stream with a /Root, else from a /Catalog found */
static void reconstruct(PDoc *d)
{
  size_t i;
  int k, root = 0;
  for (k = 0; k < d->nxref; k++) { pobj_free(d->xref[k].obj); memset(&d->xref[k], 0, sizeof d->xref[k]); }
  pobj_free(d->trailer); d->trailer = NULL;
  d->repaired = 1;
  for (i = 0; i + 3 < d->len; i++) {
    if (d->data[i] == 'o' && d->data[i + 1] == 'b' && d->data[i + 2] == 'j' && (i + 3 >= d->len || is_ws(d->data[i + 3]) || is_delim(d->data[i + 3]))) {
      /* backwards: whitespace, gen digits, whitespace, num digits */
      size_t j = i;
      long gen = 0, num = 0, mul = 1;
      while (j > 0 && is_ws(d->data[j - 1])) j--;
      if (!j || d->data[j - 1] < '0' || d->data[j - 1] > '9') continue;
      while (j > 0 && d->data[j - 1] >= '0' && d->data[j - 1] <= '9' && mul < 1000000) { gen += (d->data[j - 1] - '0') * mul; mul *= 10; j--; }
      if (!j || !is_ws(d->data[j - 1])) continue;
      while (j > 0 && is_ws(d->data[j - 1])) j--;
      mul = 1;
      if (!j || d->data[j - 1] < '0' || d->data[j - 1] > '9') continue;
      while (j > 0 && d->data[j - 1] >= '0' && d->data[j - 1] <= '9' && mul < 100000000L) { num += (d->data[j - 1] - '0') * mul; mul *= 10; j--; }
      if (num <= 0 || num > 8000000 || !xref_grow(d, (int)num + 1)) continue;
      d->xref[num].type = 1; d->xref[num].off = (long)j; d->xref[num].gen = (int)gen;
    } else if (d->data[i] == 't' && !memcmp(d->data + i, "trailer", 7) && i + 7 < d->len) {
      Lex l;
      char *kw = NULL;
      PObj *t;
      lex_init(&l, d->data + i + 7, d->len - i - 7, d);
      t = lex_object(&l, &kw);
      free(kw);
      if (t && t->type == PO_DICT && pobj_dict_get(t, "Root")) { pobj_free(d->trailer); d->trailer = t; } else pobj_free(t);
    }
  }
  /* objects inside object streams, and an xref stream's trailer */
  for (k = 1; k < d->nxref; k++) {
    PObj *o;
    if (d->xref[k].type != 1) continue;
    o = pdf_get(d, k);
    if (!o || o->type != PO_STREAM) continue;
    if (pobj_is_name(pobj_dict_get(o, "Type"), "XRef") && pobj_dict_get(o, "Root") && !d->trailer) {
      PObj *t = pobj_new(PO_DICT);
      int m;
      for (m = 0; t && m < o->u.dict.n; m++) pobj_dict_set(t, o->u.dict.k[m], pobj_copy(o->u.dict.v[m]));
      d->trailer = t;
    } else if (pobj_is_name(pobj_dict_get(o, "Type"), "ObjStm")) {
      ObjStm *c = ostm_get(d, k);
      int m;
      for (m = 0; c && m < c->n; m++) {
        int num = c->nums[m];
        if (num > 0 && num < 8000000 && xref_grow(d, num + 1) && !d->xref[num].type) { d->xref[num].type = 2; d->xref[num].off = k; d->xref[num].gen = m; }
      }
    }
  }
  if (!d->trailer || !pobj_dict_get(d->trailer, "Root")) {
    for (k = 1; k < d->nxref && !root; k++) {
      PObj *o = d->xref[k].type ? pdf_get(d, k) : NULL;
      if (o && o->type == PO_DICT && pobj_is_name(pobj_dict_get(o, "Type"), "Catalog")) root = k;
    }
    if (root) {
      PObj *r = pobj_new(PO_REF);
      if (!d->trailer) d->trailer = pobj_new(PO_DICT);
      if (r) { r->u.ref.num = root; pobj_dict_set(d->trailer, "Root", r); }
    }
  }
}

/* ------------------------------------------------------------------ the security handler */
/* the RC4/AES-128 file key from a user password (algorithm 2) */
static void key_r4(PDoc *d, const u8 *pw, int pwlen, PObj *enc, int n, u8 *key)
{
  Md5 m;
  u8 h[16], p[4];
  PObj *o = pdf_dget(d, enc, "O"), *ida = pdf_dget(d, d->trailer, "ID");
  long P = pdf_dint(d, enc, "P", 0);
  int r = (int)pdf_dint(d, enc, "R", 2), i, plen = pwlen > 32 ? 32 : pwlen;
  PObj *em = pdf_dget(d, enc, "EncryptMetadata");
  md5_init(&m);
  md5_update(&m, pw, (size_t)plen);
  md5_update(&m, pad32, (size_t)(32 - plen));
  if (o && o->type == PO_STRING) md5_update(&m, (const u8 *)o->u.str.s, (size_t)(o->u.str.len > 32 ? 32 : o->u.str.len));
  p[0] = (u8)P; p[1] = (u8)(P >> 8); p[2] = (u8)(P >> 16); p[3] = (u8)(P >> 24);
  md5_update(&m, p, 4);
  if (ida && ida->type == PO_ARRAY && ida->u.arr.n > 0) {
    PObj *id0 = pdf_resolve(d, ida->u.arr.v[0]);
    if (id0 && id0->type == PO_STRING) md5_update(&m, (const u8 *)id0->u.str.s, (size_t)id0->u.str.len);
  }
  if (r >= 4 && em && em->type == PO_BOOL && !em->u.b) md5_update(&m, (const u8 *)"\xff\xff\xff\xff", 4);
  md5_final(&m, h);
  if (r >= 3) for (i = 0; i < 50; i++) md5(h, (size_t)n, h);
  memcpy(key, h, (size_t)n);
}
/* whether key opens the file as the user (algorithms 4 and 5) */
static int check_user_r4(PDoc *d, PObj *enc, const u8 *key, int n)
{
  PObj *u = pdf_dget(d, enc, "U"), *ida = pdf_dget(d, d->trailer, "ID");
  int r = (int)pdf_dint(d, enc, "R", 2), i, k;
  u8 buf[32];
  Rc4 rc;
  if (!u || u->type != PO_STRING || u->u.str.len < 16) return 0;
  if (r == 2) {
    rc4_init(&rc, key, n);
    rc4_crypt(&rc, pad32, buf, 32);
    return !memcmp(buf, u->u.str.s, 32 > u->u.str.len ? (size_t)u->u.str.len : 32);
  } else {
    Md5 m;
    u8 h[16], kk[16];
    md5_init(&m);
    md5_update(&m, pad32, 32);
    if (ida && ida->type == PO_ARRAY && ida->u.arr.n > 0) {
      PObj *id0 = pdf_resolve(d, ida->u.arr.v[0]);
      if (id0 && id0->type == PO_STRING) md5_update(&m, (const u8 *)id0->u.str.s, (size_t)id0->u.str.len);
    }
    md5_final(&m, h);
    for (i = 0; i < 20; i++) {
      for (k = 0; k < n; k++) kk[k] = (u8)(key[k] ^ i);
      rc4_init(&rc, kk, n);
      rc4_crypt(&rc, h, h, 16);
    }
    return !memcmp(h, u->u.str.s, 16);
  }
}
/* the R6 hash (algorithm 2.B); R5 is plain SHA-256 */
static void hash_r6(int r, const u8 *pw, int pwlen, const u8 *salt, const u8 *udata, u8 out[32])
{
  u8 k[64], *k1;
  int klen = 32, round = 0;
  {
    u8 tmp[128 + 8 + 48];
    int n = 0;
    memcpy(tmp, pw, (size_t)pwlen); n = pwlen;
    memcpy(tmp + n, salt, 8); n += 8;
    if (udata) { memcpy(tmp + n, udata, 48); n += 48; }
    sha256(tmp, (size_t)n, k);
  }
  if (r < 6) { memcpy(out, k, 32); return; }
  k1 = (u8 *)malloc((size_t)(pwlen + 64 + 48) * 64);
  if (!k1) { memcpy(out, k, 32); return; }
  for (;;) {
    int len1 = 0, i, sum = 0;
    u8 *enc;
    for (i = 0; i < 64; i++) {
      memcpy(k1 + len1, pw, (size_t)pwlen); len1 += pwlen;
      memcpy(k1 + len1, k, (size_t)klen); len1 += klen;
      if (udata) { memcpy(k1 + len1, udata, 48); len1 += 48; }
    }
    enc = aes_cbc_encrypt_nopad(k, 16, k + 16, k1, (size_t)len1);
    if (!enc) break;
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
/* AES-256 (V5): the file key from a password, as user or owner; 0 if it doesn't open the file */
static int open_v5(PDoc *d, PObj *enc, const u8 *pw, int pwlen)
{
  PObj *u = pdf_dget(d, enc, "U"), *o = pdf_dget(d, enc, "O"), *ue = pdf_dget(d, enc, "UE"), *oe = pdf_dget(d, enc, "OE");
  int r = (int)pdf_dint(d, enc, "R", 6);
  u8 h[32], iv[16];
  if (pwlen > 127) pwlen = 127;
  if (!u || !o || u->type != PO_STRING || o->type != PO_STRING || u->u.str.len < 48 || o->u.str.len < 48) return 0;
  memset(iv, 0, 16);
  /* as the owner */
  hash_r6(r, pw, pwlen, (const u8 *)o->u.str.s + 32, (const u8 *)u->u.str.s, h);
  if (!memcmp(h, o->u.str.s, 32) && oe && oe->type == PO_STRING && oe->u.str.len >= 32) {
    u8 *ct = (u8 *)malloc(48);
    hash_r6(r, pw, pwlen, (const u8 *)o->u.str.s + 40, (const u8 *)u->u.str.s, h);
    if (ct) {
      int b;
      memcpy(ct, iv, 16); memcpy(ct + 16, oe->u.str.s, 32);
      /* (no padding: decrypt the two blocks by hand) */
      for (b = 0; b < 2; b++) {
        u8 blk[16];
        int k;
        aes_ecb_decrypt_block(h, 32, ct + 16 + b * 16, blk);
        for (k = 0; k < 16; k++) d->key[b * 16 + k] = (u8)(blk[k] ^ ct[b * 16 + k]);
      }
      free(ct);
      d->keylen = 32;
      return 1;
    }
  }
  /* as the user */
  hash_r6(r, pw, pwlen, (const u8 *)u->u.str.s + 32, NULL, h);
  if (!memcmp(h, u->u.str.s, 32) && ue && ue->type == PO_STRING && ue->u.str.len >= 32) {
    int b, k;
    u8 prev[16];
    hash_r6(r, pw, pwlen, (const u8 *)u->u.str.s + 40, NULL, h);
    memset(prev, 0, 16);
    for (b = 0; b < 2; b++) {
      u8 blk[16];
      aes_ecb_decrypt_block(h, 32, (const u8 *)ue->u.str.s + b * 16, blk);
      for (k = 0; k < 16; k++) d->key[b * 16 + k] = (u8)(blk[k] ^ prev[k]);
      memcpy(prev, ue->u.str.s + b * 16, 16);
    }
    d->keylen = 32;
    return 1;
  }
  return 0;
}
/* a password tried as the user's, then as the owner's (whose own key decrypts /O to the user's) */
static int try_password(PDoc *d, PObj *enc, const char *pw)
{
  int v = (int)pdf_dint(d, enc, "V", 0), r = (int)pdf_dint(d, enc, "R", 2), n, i, k;
  int pwlen = (int)strlen(pw);
  u8 key[16];
  if (v >= 5) return open_v5(d, enc, (const u8 *)pw, pwlen);
  n = r == 2 ? 5 : (int)(pdf_dint(d, enc, "Length", 40) / 8);
  if (n < 5) n = 5;
  if (n > 16) n = 16;
  key_r4(d, (const u8 *)pw, pwlen, enc, n, key);
  if (check_user_r4(d, enc, key, n)) { memcpy(d->key, key, (size_t)n); d->keylen = n; return 1; }
  /* as the owner (algorithm 7) */
  {
    Md5 m;
    u8 h[16], u[32], kk[16];
    Rc4 rc;
    PObj *o = pdf_dget(d, enc, "O");
    int plen = pwlen > 32 ? 32 : pwlen;
    if (!o || o->type != PO_STRING || o->u.str.len < 32) return 0;
    md5_init(&m);
    md5_update(&m, (const u8 *)pw, (size_t)plen);
    md5_update(&m, pad32, (size_t)(32 - plen));
    md5_final(&m, h);
    if (r >= 3) for (i = 0; i < 50; i++) md5(h, 16, h);
    memcpy(u, o->u.str.s, 32);
    if (r == 2) { rc4_init(&rc, h, n); rc4_crypt(&rc, u, u, 32); }
    else for (i = 19; i >= 0; i--) {
      for (k = 0; k < n; k++) kk[k] = (u8)(h[k] ^ i);
      rc4_init(&rc, kk, n);
      rc4_crypt(&rc, u, u, 32);
    }
    /* u is the padded user password */
    {
      char up[33];
      int ul = 32;
      memcpy(up, u, 32);
      for (k = 0; k <= 32; k++) if (k == 32 || !memcmp(u + k, pad32, (size_t)(32 - k))) { ul = k; break; }
      up[ul] = 0;
      key_r4(d, (const u8 *)up, ul, enc, n, key);
      if (check_user_r4(d, enc, key, n)) { memcpy(d->key, key, (size_t)n); d->keylen = n; return 1; }
    }
  }
  return 0;
}
/* the method a crypt filter name stands for (V4/V5): 1 RC4, 2 AES-128, 3 AES-256, 0 none */
static int cf_method(PDoc *d, PObj *enc, const char *name, int v)
{
  PObj *cf, *f;
  const char *m;
  if (!name || !strcmp(name, "Identity")) return 0;
  cf = pdf_dget(d, enc, "CF");
  f = cf ? pdf_dget(d, cf, name) : NULL;
  m = f ? pdf_dname(d, f, "CFM") : NULL;
  if (!m) return v >= 5 ? 3 : 1;
  if (!strcmp(m, "AESV2")) return 2;
  if (!strcmp(m, "AESV3")) return 3;
  if (!strcmp(m, "V2")) return 1;
  return 0;
}
/* the /Encrypt dictionary read: the methods, and the empty user password tried */
static void setup_crypt(PDoc *d)
{
  PObj *er = pobj_dict_get(d->trailer, "Encrypt"), *enc;
  int v;
  const char *filter;
  if (!er) return;
  if (er->type == PO_REF) d->encrypt_num = er->u.ref.num;
  enc = pdf_resolve(d, er);
  if (!enc || enc->type != PO_DICT) return;
  filter = pdf_dname(d, enc, "Filter");
  if (filter && strcmp(filter, "Standard")) { snprintf(d->err, sizeof d->err, "encrypted with %s, which Folio can't open", filter); d->need_password = -1; return; }
  v = (int)pdf_dint(d, enc, "V", 0);
  d->perms = (int)pdf_dint(d, enc, "P", -1);
  if (v >= 4) {
    d->crypt_streams = cf_method(d, enc, pdf_dname(d, enc, "StmF"), v);
    d->crypt_strings = cf_method(d, enc, pdf_dname(d, enc, "StrF"), v);
    d->crypt = d->crypt_streams ? d->crypt_streams : d->crypt_strings;
    if (v >= 5) d->crypt = 3;
  } else {
    d->crypt = d->crypt_streams = d->crypt_strings = 1;
  }
  if (!d->crypt) return;
  if (!try_password(d, enc, "")) d->need_password = 1;
}
int pdf_password(PDoc *d, const char *pw)
{
  PObj *enc = pdf_dict_of(d, pobj_dict_get(d->trailer, "Encrypt"));
  int k;
  if (!enc || d->need_password < 0) return 0;
  if (!try_password(d, enc, pw)) return 0;
  d->need_password = 0;
  /* objects parsed before the key was known are parsed again */
  for (k = 0; k < d->nxref; k++) if (d->xref[k].obj) { pobj_free(d->xref[k].obj); d->xref[k].obj = NULL; }
  if (d->ostm) { int i; for (i = 0; i < NOSTM; i++) ostm_free(&((ObjStm *)d->ostm)[i]); }
  free(d->pages); d->pages = NULL; d->npages = 0;
  return 1;
}

/* ------------------------------------------------------------------ pages */
/* the page tree walked depth first into d->pages (marking visited nodes against loops) */
static void walk_pages(PDoc *d, PObj *node, u8 *seen, int depth, int *cap)
{
  PObj *kids;
  const char *type;
  int i;
  if (!node || node->type != PO_DICT || depth > MAXDEPTH) return;
  type = pdf_dname(d, node, "Type");
  kids = pdf_dget(d, node, "Kids");
  if ((type && !strcmp(type, "Pages")) || (!type && kids && kids->type == PO_ARRAY)) {
    if (!kids || kids->type != PO_ARRAY) return;
    for (i = 0; i < kids->u.arr.n; i++) {
      PObj *k = kids->u.arr.v[i];
      if (k && k->type == PO_REF) {
        if (k->u.ref.num <= 0 || k->u.ref.num >= d->nxref || seen[k->u.ref.num]) continue;
        seen[k->u.ref.num] = 1;
      }
      walk_pages(d, pdf_resolve(d, k), seen, depth + 1, cap);
    }
    return;
  }
  if (d->npages >= *cap) {
    int nc = *cap ? *cap * 2 : 64;
    PObj **p = (PObj **)realloc(d->pages, (size_t)nc * sizeof(PObj *));
    if (!p) return;
    d->pages = p; *cap = nc;
  }
  d->pages[d->npages++] = node;
}
static void load_pages(PDoc *d)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *pages;
  u8 *seen;
  int cap = 0;
  free(d->pages); d->pages = NULL; d->npages = 0;
  if (!root) return;
  pages = pdf_dget(d, root, "Pages");
  if (!pages || !(seen = (u8 *)calloc((size_t)d->nxref + 1, 1))) return;
  walk_pages(d, pages, seen, 0, &cap);
  free(seen);
}
PObj *pdf_page_attr(PDoc *d, PObj *page, const char *key)
{
  int n = 0;
  while (page && n++ < MAXDEPTH) {
    PObj *v = pdf_dget(d, page, key);
    if (v) return v;
    page = pdf_dget(d, page, "Parent");
  }
  return NULL;
}
/* a rectangle array as x0 < x1, y0 < y1; 0 if it isn't one */
static int get_rect(PObj *a, PDoc *d, double r[4])
{
  int i;
  double t;
  a = pdf_resolve(d, a);
  if (!a || a->type != PO_ARRAY || a->u.arr.n < 4) return 0;
  for (i = 0; i < 4; i++) { PObj *v = pdf_resolve(d, a->u.arr.v[i]); if (!pobj_isnum(v)) return 0; r[i] = pobj_num(v); }
  if (r[0] > r[2]) { t = r[0]; r[0] = r[2]; r[2] = t; }
  if (r[1] > r[3]) { t = r[1]; r[1] = r[3]; r[3] = t; }
  return r[2] - r[0] > 0.01 && r[3] - r[1] > 0.01;
}
void pdf_page_box(PDoc *d, int page, double box[4], int *rotate)
{
  PObj *p = page >= 0 && page < d->npages ? d->pages[page] : NULL;
  double m[4], c[4];
  int rot;
  box[0] = 0; box[1] = 0; box[2] = 612; box[3] = 792;
  if (rotate) *rotate = 0;
  if (!p) return;
  if (get_rect(pdf_page_attr(d, p, "MediaBox"), d, m)) memcpy(box, m, sizeof m);
  if (get_rect(pdf_page_attr(d, p, "CropBox"), d, c)) {
    /* the crop box, kept within the media box */
    double x0 = c[0] > box[0] ? c[0] : box[0], y0 = c[1] > box[1] ? c[1] : box[1];
    double x1 = c[2] < box[2] ? c[2] : box[2], y1 = c[3] < box[3] ? c[3] : box[3];
    if (x1 - x0 > 1 && y1 - y0 > 1) { box[0] = x0; box[1] = y0; box[2] = x1; box[3] = y1; }
  }
  rot = (int)pobj_num(pdf_page_attr(d, p, "Rotate"));
  rot %= 360;
  if (rot < 0) rot += 360;
  rot = (rot + 45) / 90 * 90 % 360;
  if (rotate) *rotate = rot;
}
int pdf_page_index(PDoc *d, PObj *page)
{
  int i;
  for (i = 0; i < d->npages; i++) if (d->pages[i] == page) return i;
  return -1;
}

/* ------------------------------------------------------------------ open and close */
PDoc *pdf_open(const u8 *data, size_t len, char *err, size_t errcap)
{
  PDoc *d;
  size_t i;
  int ok;
  for (i = 0; i < len && i < 1024; i++) if (!memcmp(data + i, "%PDF", 4 < len - i ? 4 : len - i)) break;
  if (len < 8 || i >= 1024 || i + 4 > len) { if (err) snprintf(err, errcap, "not a PDF file"); return NULL; }
  if (!(d = (PDoc *)calloc(1, sizeof *d))) { if (err) snprintf(err, errcap, "out of memory"); return NULL; }
  /* (a header after some junk: offsets count from the %PDF) */
  if (!(d->data = (u8 *)malloc(len - i + 1))) { free(d); if (err) snprintf(err, errcap, "out of memory"); return NULL; }
  memcpy(d->data, data + i, len - i);
  d->len = len - i;
  d->data[d->len] = 0;
  ok = read_xrefs(d);
  if (ok) {
    /* sanity: the catalog must be there and say so */
    PObj *root = pdf_dget(d, d->trailer, "Root");
    if (!root || root->type != PO_DICT) ok = 0;
  }
  if (!ok) reconstruct(d);
  setup_crypt(d);
  if (d->need_password < 0) { if (err) snprintf(err, errcap, "%s", d->err); pdf_close(d); return NULL; }
  if (!d->need_password) load_pages(d);
  if (!d->need_password && !d->npages && !d->repaired) {
    /* a table that looked fine but leads nowhere: rebuilt */
    reconstruct(d);
    setup_crypt(d);
    if (!d->need_password) load_pages(d);
  }
  if (!d->need_password && !d->npages) { if (err) snprintf(err, errcap, "no pages could be found in it"); pdf_close(d); return NULL; }
  return d;
}
/* (pages are loaded once the password is right) */
int pdf_ensure_pages(PDoc *d) { if (!d->npages && !d->need_password) load_pages(d); return d->npages; }
void pdf_close(PDoc *d)
{
  int k;
  if (!d) return;
  for (k = 0; k < d->nxref; k++) pobj_free(d->xref[k].obj);
  free(d->xref);
  pobj_free(d->trailer);
  if (d->ostm) { for (k = 0; k < NOSTM; k++) ostm_free(&((ObjStm *)d->ostm)[k]); free(d->ostm); }
  free(d->pages);
  free(d->data);
  free(d);
}

/* ------------------------------------------------------------------ text strings */
/* PDFDocEncoding's 0x80-0x9F and 0x18-0x1F, which differ from Latin-1 */
static const u16 pdfdoc_hi[32] = {
  0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044, 0x2039, 0x203A, 0x2212, 0x2030, 0x201E, 0x201C, 0x201D, 0x2018,
  0x2019, 0x201A, 0x2122, 0xFB01, 0xFB02, 0x0141, 0x0152, 0x0160, 0x0178, 0x017D, 0x0131, 0x0142, 0x0153, 0x0161, 0x017E, 0xFFFD };
static const u16 pdfdoc_lo[8] = { 0x02D8, 0x02C7, 0x02C6, 0x02D9, 0x02DD, 0x02DB, 0x02DA, 0x02DC };
char *pdf_text_string(const PObj *s)
{
  Buf b;
  int i;
  const u8 *p;
  char u[4];
  if (!s || (s->type != PO_STRING && s->type != PO_NAME)) return str_dup("");
  p = (const u8 *)s->u.str.s;
  buf_init(&b);
  if (s->u.str.len >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
    for (i = 2; i + 1 < s->u.str.len; i += 2) {
      u32 c = ((u32)p[i] << 8) | p[i + 1];
      if (c >= 0xD800 && c < 0xDC00 && i + 3 < s->u.str.len) {
        u32 lo = ((u32)p[i + 2] << 8) | p[i + 3];
        if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
      }
      if (c == 0x1B) {                         /* a language escape: skipped to the next 0x1B */
        i += 2;
        while (i + 1 < s->u.str.len && !(p[i] == 0 && p[i + 1] == 0x1B)) i += 2;
        continue;
      }
      buf_add(&b, u, (size_t)utf8_put(u, c));
    }
  } else if (s->u.str.len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
    buf_add(&b, p + 3, (size_t)(s->u.str.len - 3));
  } else {
    for (i = 0; i < s->u.str.len; i++) {
      u32 c = p[i];
      if (c >= 0x80 && c < 0xA0) c = pdfdoc_hi[c - 0x80];
      else if (c >= 0x18 && c < 0x20) c = pdfdoc_lo[c - 0x18];
      else if (c == 0xAD) c = 0xFFFD;
      buf_add(&b, u, (size_t)utf8_put(u, c));
    }
  }
  if (b.oom) { buf_free(&b); return str_dup(""); }
  if (!b.data) return str_dup("");
  return buf_take(&b);
}
char *pdf_info(PDoc *d, const char *key)
{
  PObj *info = pdf_dget(d, d->trailer, "Info"), *v;
  if (!info) return NULL;
  v = pdf_dget(d, info, key);
  if (!v || (v->type != PO_STRING && v->type != PO_NAME)) return NULL;
  return pdf_text_string(v);
}

/* ------------------------------------------------------------------ destinations and the outline */
/* a name tree's value for key (strings compared as bytes) */
static PObj *name_tree(PDoc *d, PObj *node, const char *key, int klen, int depth)
{
  PObj *names, *kids;
  int i;
  if (!node || depth > MAXDEPTH) return NULL;
  names = pdf_dget(d, node, "Names");
  if (names && names->type == PO_ARRAY)
    for (i = 0; i + 1 < names->u.arr.n; i += 2) {
      PObj *k = pdf_resolve(d, names->u.arr.v[i]);
      if (k && (k->type == PO_STRING || k->type == PO_NAME) && k->u.str.len == klen && !memcmp(k->u.str.s, key, (size_t)klen))
        return pdf_resolve(d, names->u.arr.v[i + 1]);
    }
  kids = pdf_dget(d, node, "Kids");
  if (kids && kids->type == PO_ARRAY)
    for (i = 0; i < kids->u.arr.n; i++) {
      PObj *k = pdf_resolve(d, kids->u.arr.v[i]), *lim, *r;
      if (!k) continue;
      lim = pdf_dget(d, k, "Limits");
      if (lim && lim->type == PO_ARRAY && lim->u.arr.n >= 2) {
        PObj *lo = pdf_resolve(d, lim->u.arr.v[0]), *hi = pdf_resolve(d, lim->u.arr.v[1]);
        if (lo && hi && lo->type == PO_STRING && hi->type == PO_STRING) {
          int c1 = memcmp(key, lo->u.str.s, (size_t)(klen < lo->u.str.len ? klen : lo->u.str.len));
          int c2 = memcmp(key, hi->u.str.s, (size_t)(klen < hi->u.str.len ? klen : hi->u.str.len));
          if (c1 < 0 || (c1 == 0 && klen < lo->u.str.len) || c2 > 0 || (c2 == 0 && klen > hi->u.str.len)) continue;
        }
      }
      if ((r = name_tree(d, k, key, klen, depth + 1))) return r;
    }
  return NULL;
}
int pdf_dest(PDoc *d, PObj *dest, int *page, double *top)
{
  int n = 0;
  *page = -1; *top = -1;
  dest = pdf_resolve(d, dest);
  while (dest && (dest->type == PO_NAME || dest->type == PO_STRING) && n++ < 4) {
    PObj *root = pdf_dget(d, d->trailer, "Root"), *dests = root ? pdf_dget(d, root, "Dests") : NULL, *r = NULL;
    if (dests && dest->type == PO_NAME) r = pdf_dget(d, dests, dest->u.str.s);
    if (!r) {
      PObj *names = root ? pdf_dget(d, root, "Names") : NULL, *dt = names ? pdf_dget(d, names, "Dests") : NULL;
      if (dt) r = name_tree(d, dt, dest->u.str.s, dest->u.str.len, 0);
    }
    if (r && r->type == PO_DICT) r = pdf_dget(d, r, "D");
    dest = r;
  }
  if (!dest || dest->type != PO_ARRAY || dest->u.arr.n < 1) return 0;
  {
    PObj *p = dest->u.arr.v[0];
    if (p && p->type == PO_REF) *page = pdf_page_index(d, pdf_resolve(d, p));
    else if (pobj_isnum(p)) *page = (int)pobj_num(p);
    if (*page < 0 || *page >= d->npages) { *page = -1; return 0; }
    if (dest->u.arr.n >= 4 && pobj_is_name(dest->u.arr.v[1], "XYZ") && pobj_isnum(dest->u.arr.v[3])) *top = pobj_num(dest->u.arr.v[3]);
    else if (dest->u.arr.n >= 3 && (pobj_is_name(dest->u.arr.v[1], "FitH") || pobj_is_name(dest->u.arr.v[1], "FitBH")) && pobj_isnum(dest->u.arr.v[2])) *top = pobj_num(dest->u.arr.v[2]);
    else if (dest->u.arr.n >= 6 && pobj_is_name(dest->u.arr.v[1], "FitR")) *top = pobj_num(dest->u.arr.v[5]);
  }
  return 1;
}
/* an outline item and its siblings (each with its children), visited marks against loops */
static POutline *outline_items(PDoc *d, PObj *first, u8 *seen, int depth, int *budget)
{
  POutline *head = NULL, **tail = &head;
  PObj *ref = first;
  while (ref && depth < MAXDEPTH && (*budget)-- > 0) {
    PObj *it;
    POutline *o;
    PObj *dest, *a;
    if (ref->type == PO_REF) {
      if (ref->u.ref.num <= 0 || ref->u.ref.num >= d->nxref || seen[ref->u.ref.num]) break;
      seen[ref->u.ref.num] = 1;
    }
    it = pdf_resolve(d, ref);
    if (!it || it->type != PO_DICT) break;
    if (!(o = (POutline *)calloc(1, sizeof *o))) break;
    o->title = pdf_text_string(pdf_dget(d, it, "Title"));
    o->page = -1; o->top = -1;
    o->open = pdf_dint(d, it, "Count", 0) > 0;
    dest = pdf_dget(d, it, "Dest");
    a = pdf_dget(d, it, "A");
    if (dest) pdf_dest(d, dest, &o->page, &o->top);
    else if (a) {
      const char *s = pdf_dname(d, a, "S");
      if (s && !strcmp(s, "GoTo")) pdf_dest(d, pdf_dget(d, a, "D"), &o->page, &o->top);
      else if (s && !strcmp(s, "URI")) { PObj *u = pdf_dget(d, a, "URI"); if (u && u->type == PO_STRING) o->uri = str_ndup(u->u.str.s, (size_t)u->u.str.len); }
    }
    o->child = outline_items(d, pobj_dict_get(it, "First"), seen, depth + 1, budget);
    *tail = o; tail = &o->next;
    ref = pobj_dict_get(it, "Next");
  }
  return head;
}
POutline *pdf_outline(PDoc *d)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *ol;
  u8 *seen;
  POutline *o;
  int budget = 20000;
  if (!root || !(ol = pdf_dget(d, root, "Outlines"))) return NULL;
  if (!(seen = (u8 *)calloc((size_t)d->nxref + 1, 1))) return NULL;
  o = outline_items(d, pobj_dict_get(ol, "First"), seen, 0, &budget);
  free(seen);
  return o;
}
void pdf_outline_free(POutline *o)
{
  while (o) {
    POutline *n = o->next;
    pdf_outline_free(o->child);
    free(o->title); free(o->uri); free(o);
    o = n;
  }
}

int pdf_new_object(PDoc *d, PObj *o)
{
  int n = d->nxref < 1 ? 1 : d->nxref;
  if (!o || !xref_grow(d, n + 1)) { pobj_free(o); return 0; }
  d->xref[n].type = 1; d->xref[n].off = -1; d->xref[n].obj = o;
  if (o->type == PO_STREAM) { o->onum = -1; }
  return n;
}
