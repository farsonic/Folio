/*
 * cmap.c - CMaps (see cmap.h).
 */
#include <stdlib.h>
#include <string.h>
#include "cmap.h"

typedef struct { u32 lo, hi; u8 nbytes; } Space;
typedef struct { u32 lo, hi; u8 nbytes; u8 kind; u32 val; u32 *str; int nstr; } Map;   /* kind 0 CID, 1 Unicode */
struct CMap {
  Space space[16]; int nspace;
  Map *m; int n, cap;
  int vertical, identity;
};
CMap *cmap_identity(int vertical)
{
  CMap *m = (CMap *)calloc(1, sizeof *m);
  if (!m) return NULL;
  m->identity = 1; m->vertical = vertical;
  m->space[0].lo = 0; m->space[0].hi = 0xFFFF; m->space[0].nbytes = 2; m->nspace = 1;
  return m;
}
void cmap_free(CMap *m)
{
  int i;
  if (!m) return;
  for (i = 0; i < m->n; i++) free(m->m[i].str);
  free(m->m);
  free(m);
}
int cmap_vertical(const CMap *m) { return m && m->vertical; }
static Map *add(CMap *c)
{
  Map *m;
  if (c->n >= c->cap) {
    int nc = c->cap ? c->cap * 2 : 64;
    Map *n2;
    if (nc > 200000) return NULL;
    if (!(n2 = (Map *)realloc(c->m, (size_t)nc * sizeof(Map)))) return NULL;
    c->m = n2; c->cap = nc;
  }
  m = &c->m[c->n++];
  memset(m, 0, sizeof *m);
  return m;
}
static u32 bytes_val(const Token *t) { u32 v = 0; int i; for (i = 0; i < t->len && i < 4; i++) v = (v << 8) | (u8)t->s[i]; return v; }
/* a UTF-16BE string as code points */
static int utf16(const u8 *s, int len, u32 *out, int max)
{
  int i, n = 0;
  if (len == 1) { if (max) out[n++] = s[0]; return n; }
  for (i = 0; i + 1 < len && n < max; i += 2) {
    u32 c = ((u32)s[i] << 8) | s[i + 1];
    if (c >= 0xD800 && c < 0xDC00 && i + 3 < len) {
      u32 lo = ((u32)s[i + 2] << 8) | s[i + 3];
      if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
    }
    out[n++] = c;
  }
  return n;
}
static void set_str(Map *m, const Token *t)
{
  u32 buf[64];
  int n = utf16((const u8 *)t->s, t->len, buf, 64);
  if (n == 1) { m->val = buf[0]; return; }
  if (n > 1 && (m->str = (u32 *)malloc((size_t)n * sizeof(u32)))) { memcpy(m->str, buf, (size_t)n * sizeof(u32)); m->nstr = n; }
}
CMap *cmap_parse(const u8 *data, size_t n)
{
  CMap *c = (CMap *)calloc(1, sizeof *c);
  Lex l;
  Token t, prev[2];
  int np = 0;
  if (!c) return NULL;
  memset(prev, 0, sizeof prev);
  lex_init(&l, data, n, NULL);
  for (;;) {
    int ty = lex_next(&l, &t);
    if (ty == TK_EOF) { tok_free(&t); break; }
    if (ty == TK_KEYWORD) {
      int count = np >= 1 && prev[np - 1].type == TK_INT ? (int)prev[np - 1].i : 0, k;
      if (count < 0 || count > 100000) count = 0;
      if (!strcmp(t.s, "begincodespacerange")) {
        for (k = 0; k < count; k++) {
          Token a, b;
          if (lex_next(&l, &a) != TK_STRING) { tok_free(&a); break; }
          if (lex_next(&l, &b) != TK_STRING) { tok_free(&a); tok_free(&b); break; }
          if (c->nspace < 16 && a.len >= 1 && a.len <= 4) {
            c->space[c->nspace].lo = bytes_val(&a); c->space[c->nspace].hi = bytes_val(&b); c->space[c->nspace].nbytes = (u8)a.len; c->nspace++;
          }
          tok_free(&a); tok_free(&b);
        }
      } else if (!strcmp(t.s, "begincidrange") || !strcmp(t.s, "beginbfrange")) {
        int bf = t.s[5] == 'b';
        for (k = 0; k < count; k++) {
          Token a, b, v;
          Map *m;
          if (lex_next(&l, &a) != TK_STRING) { tok_free(&a); break; }
          if (lex_next(&l, &b) != TK_STRING) { tok_free(&a); tok_free(&b); break; }
          lex_next(&l, &v);
          if ((m = add(c))) {
            m->lo = bytes_val(&a); m->hi = bytes_val(&b); m->nbytes = (u8)(a.len < 1 ? 1 : a.len > 4 ? 4 : a.len); m->kind = (u8)bf;
            if (m->hi < m->lo || m->hi - m->lo > 65536) { c->n--; }
            else if (v.type == TK_INT) m->val = (u32)v.i;
            else if (v.type == TK_STRING) set_str(m, &v);
            else if (v.type == TK_ARRAY_OPEN) {
              /* [<s1> <s2> ...]: one string per code, each its own entry */
              u32 code = m->lo, hi = m->hi;
              int nb = m->nbytes;
              c->n--;
              for (;;) {
                Token e;
                int te = lex_next(&l, &e);
                if (te == TK_ARRAY_CLOSE || te == TK_EOF) { tok_free(&e); break; }
                if (te == TK_STRING && code <= hi && (m = add(c))) { m->lo = m->hi = code; m->nbytes = (u8)nb; m->kind = 1; set_str(m, &e); }
                code++;
                tok_free(&e);
              }
            } else c->n--;
          }
          tok_free(&a); tok_free(&b); tok_free(&v);
        }
      } else if (!strcmp(t.s, "begincidchar") || !strcmp(t.s, "beginbfchar")) {
        int bf = t.s[5] == 'b';
        for (k = 0; k < count; k++) {
          Token a, v;
          Map *m;
          if (lex_next(&l, &a) != TK_STRING) { tok_free(&a); break; }
          lex_next(&l, &v);
          if ((m = add(c))) {
            m->lo = m->hi = bytes_val(&a); m->nbytes = (u8)(a.len < 1 ? 1 : a.len > 4 ? 4 : a.len); m->kind = (u8)bf;
            if (v.type == TK_INT) m->val = (u32)v.i;
            else if (v.type == TK_STRING) set_str(m, &v);
            else if (v.type == TK_NAME) { m->val = 0; }
            else c->n--;
          }
          tok_free(&a); tok_free(&v);
        }
      } else if (!strcmp(t.s, "usecmap")) {
        if (np >= 1 && prev[np - 1].type == TK_NAME && strstr(prev[np - 1].s, "Identity")) {
          c->identity = 1;
          if (!c->nspace) { c->space[0].lo = 0; c->space[0].hi = 0xFFFF; c->space[0].nbytes = 2; c->nspace = 1; }
        }
      } else if (!strcmp(t.s, "def") && np == 2 && prev[0].type == TK_NAME && !strcmp(prev[0].s, "WMode") && prev[1].type == TK_INT) {
        c->vertical = prev[1].i == 1;
      }
    }
    /* the last two tokens are kept (counts before begin..., names before usecmap/def) */
    if (np == 2) { tok_free(&prev[0]); prev[0] = prev[1]; np = 1; }
    prev[np++] = t;
  }
  while (np) tok_free(&prev[--np]);
  if (!c->nspace) {
    /* no codespace: guess from the mappings */
    int i, nb = 0;
    for (i = 0; i < c->n; i++) if (c->m[i].nbytes > nb) nb = c->m[i].nbytes;
    if (!nb) nb = 1;
    c->space[0].lo = 0; c->space[0].hi = nb >= 4 ? 0xFFFFFFFFUL : (1UL << (8 * nb)) - 1; c->space[0].nbytes = (u8)nb; c->nspace = 1;
  }
  return c;
}
int cmap_next(const CMap *m, const u8 *s, int len, u32 *code)
{
  int nb, i;
  u32 v = 0;
  if (len <= 0) { *code = 0; return 1; }
  /* the shortest codespace range the bytes fall in */
  for (nb = 1; nb <= 4 && nb <= len; nb++) {
    v = (v << 8) | s[nb - 1];
    for (i = 0; i < m->nspace; i++) {
      const Space *sp = &m->space[i];
      if (sp->nbytes == nb) {
        /* each byte within the range's bytes (ranges are rectangular, byte by byte) */
        int b, ok = 1;
        for (b = 0; b < nb && ok; b++) {
          u32 sh = (u32)(8 * (nb - 1 - b));
          u32 bv = (v >> sh) & 255, blo = (sp->lo >> sh) & 255, bhi = (sp->hi >> sh) & 255;
          if (bv < blo || bv > bhi) ok = 0;
        }
        if (ok) { *code = v; return nb; }
      }
    }
  }
  /* no range fits: use the first range's length */
  nb = m->nspace ? m->space[0].nbytes : 1;
  if (nb > len) nb = len;
  for (v = 0, i = 0; i < nb; i++) v = (v << 8) | s[i];
  *code = v;
  return nb;
}
int cmap_cid(const CMap *m, u32 code, int nbytes)
{
  int i;
  for (i = m->n - 1; i >= 0; i--) {
    const Map *e = &m->m[i];
    if (e->kind == 0 && e->nbytes == nbytes && code >= e->lo && code <= e->hi) return (int)(e->val + (code - e->lo));
  }
  if (m->identity) return (int)code;
  return 0;
}
int cmap_unicode(const CMap *m, u32 code, int nbytes, u32 *out, int max)
{
  int i;
  if (max <= 0) return 0;
  for (i = m->n - 1; i >= 0; i--) {
    const Map *e = &m->m[i];
    if (e->kind == 1 && code >= e->lo && code <= e->hi && (e->nbytes == nbytes || m->nspace <= 1)) {
      if (e->str) {
        int k, n = e->nstr < max ? e->nstr : max;
        for (k = 0; k < n; k++) out[k] = e->str[k];
        if (code > e->lo && n > 0) out[n - 1] += code - e->lo;   /* (a range over multi-character strings: the last one counts up) */
        return n;
      }
      out[0] = e->val + (code - e->lo);
      return out[0] ? 1 : 0;
    }
  }
  return 0;
}
