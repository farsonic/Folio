/*
 * textpage.c - a page's text (see textpage.h).
 *
 * Characters are gathered into lines in the order the page draws them (most PDFs draw their text
 * in reading order, column by column): a character joins the line before it when it sits on the
 * same baseline, runs the same way and doesn't jump far ahead or back. Within a line they are put
 * in order along it; a gap wider than a fraction of the font size becomes a space.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "textpage.h"

void textpage_init(TextPage *t) { memset(t, 0, sizeof *t); }
void textpage_free(TextPage *t) { free(t->c); free(t->o); memset(t, 0, sizeof *t); }
void textpage_add(TextPage *t, u32 u, double x0, double y0, double x1, double y1, double size, int dir)
{
  TChar *c;
  if (t->n >= t->cap) {
    int nc = t->cap ? t->cap * 2 : 512;
    TChar *n2;
    if (nc > 4000000) return;
    if (!(n2 = (TChar *)realloc(t->c, (size_t)nc * sizeof(TChar)))) return;
    t->c = n2; t->cap = nc;
  }
  c = &t->c[t->n++];
  c->u = u; c->x0 = (float)x0; c->y0 = (float)y0; c->x1 = (float)x1; c->y1 = (float)y1; c->size = (float)size; c->dir = (u8)dir;
  t->finished = 0;
}
/* a character's position along its line (a) and across it (b, the line's centre), whatever the direction */
static void axes(const TChar *c, double *a0, double *a1, double *b)
{
  switch (c->dir) {
  case 1: *a0 = c->y0; *a1 = c->y1; *b = -(c->x0 + c->x1) / 2; break;
  case 2: *a0 = -c->x1; *a1 = -c->x0; *b = -(c->y0 + c->y1) / 2; break;
  case 3: *a0 = -c->y1; *a1 = -c->y0; *b = (c->x0 + c->x1) / 2; break;
  default: *a0 = c->x0; *a1 = c->x1; *b = (c->y0 + c->y1) / 2; break;
  }
}
typedef struct { int first, n; double b, size; u8 dir; double last_a1; } Line;
static int by_along(const void *x, const void *y)
{
  const TChar *p = (const TChar *)x, *q = (const TChar *)y;
  double a0, a1, b, c0, c1, e;
  axes(p, &a0, &a1, &b); axes(q, &c0, &c1, &e);
  return a0 < c0 ? -1 : a0 > c0;
}
static int is_space(u32 u) { return u == 32 || u == 9 || u == 10 || u == 13 || u == 0xA0; }
static void put(TextPage *t, TChar c, int *cap)
{
  if (t->no >= *cap) {
    int nc = *cap ? *cap * 2 : 1024;
    TChar *n2 = (TChar *)realloc(t->o, (size_t)nc * sizeof(TChar));
    if (!n2) return;
    t->o = n2; *cap = nc;
  }
  t->o[t->no++] = c;
}
void textpage_finish(TextPage *t)
{
  Line *lines = NULL;
  int nl = 0, capl = 0, i, cap = 0;
  TChar *sorted;
  if (t->finished) return;
  free(t->o); t->o = NULL; t->no = 0;
  t->finished = 1;
  if (!t->n) return;
  /* lines in drawing order */
  for (i = 0; i < t->n; i++) {
    TChar *c = &t->c[i];
    double a0, a1, b, h = c->size > 0.1 ? c->size : 1;
    Line *L = nl ? &lines[nl - 1] : NULL;
    axes(c, &a0, &a1, &b);
    if (L && L->dir == c->dir && fabs(b - L->b) < 0.5 * (h > L->size ? h : L->size) && a0 > L->last_a1 - 1.5 * h && a0 < L->last_a1 + 6 * h) {
      L->n++;
      if (a1 > L->last_a1) L->last_a1 = a1;
      continue;
    }
    if (nl >= capl) {
      int nc = capl ? capl * 2 : 64;
      Line *n2 = (Line *)realloc(lines, (size_t)nc * sizeof(Line));
      if (!n2) break;
      lines = n2; capl = nc;
    }
    L = &lines[nl++];
    L->first = i; L->n = 1; L->b = b; L->size = h; L->dir = c->dir; L->last_a1 = a1;
  }
  if (!(sorted = (TChar *)malloc((size_t)t->n * sizeof(TChar)))) { free(lines); return; }
  memcpy(sorted, t->c, (size_t)t->n * sizeof(TChar));
  for (i = 0; i < nl; i++) {
    Line *L = &lines[i];
    int k;
    double prev_a1 = 0;
    qsort(sorted + L->first, (size_t)L->n, sizeof(TChar), by_along);
    for (k = 0; k < L->n; k++) {
      TChar c = sorted[L->first + k];
      double a0, a1, b;
      axes(&c, &a0, &a1, &b);
      if (k && !is_space(c.u) && t->no && !is_space(t->o[t->no - 1].u) && a0 - prev_a1 > 0.18 * (c.size > 0.1 ? c.size : 1)) {
        TChar sp = c;
        sp.u = 32;
        if (c.dir == 0) { sp.x0 = t->o[t->no - 1].x1; sp.x1 = c.x0; }
        put(t, sp, &cap);
      }
      /* (overprinted duplicates, as some producers draw bold text twice, dropped) */
      if (k && t->no && t->o[t->no - 1].u == c.u && fabs(t->o[t->no - 1].x0 - c.x0) < 0.1 * c.size && fabs(t->o[t->no - 1].y0 - c.y0) < 0.1 * c.size) continue;
      if (c.u) put(t, c, &cap);
      if (a1 > prev_a1 || !k) prev_a1 = a1;
    }
    if (i + 1 < nl && t->no) {
      TChar nlc = t->o[t->no - 1];
      nlc.u = 10; nlc.x0 = nlc.x1;
      put(t, nlc, &cap);
    }
  }
  free(sorted);
  free(lines);
}
char *textpage_text(TextPage *t, int a, int b)
{
  Buf o;
  int i;
  char u[4];
  textpage_finish(t);
  buf_init(&o);
  if (a < 0) a = 0;
  if (b >= t->no) b = t->no - 1;
  for (i = a; i <= b; i++) buf_add(&o, u, (size_t)utf8_put(u, t->o[i].u));
  if (o.oom || !o.data) { buf_free(&o); return str_dup(""); }
  return buf_take(&o);
}
/* case folded for matching (ASCII, Latin-1, a few ligatures left as they are) */
static u32 fold(u32 c)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  if (c == 0xA0 || c == 9 || c == 10 || c == 13) return 32;
  if (c == 0x2019 || c == 0x2018) return '\'';
  if (c == 0x201C || c == 0x201D) return '"';
  if (c == 0x2010 || c == 0x2011 || c == 0x2013) return '-';
  return c;
}
static int needle_cps(const char *needle, u32 *out, int max)
{
  int n = 0, used;
  size_t len = strlen(needle), off = 0;
  while (off < len && n < max) { u32 c = utf8_get(needle + off, len - off, &used); off += (size_t)used; c = fold(c); if (c == 32 && n && out[n - 1] == 32) continue; out[n++] = c; }
  return n;
}
/* whether the needle matches at o[i]; *end gets the last index */
static int match_at(TextPage *t, const u32 *nd, int nn, int i, int *end)
{
  int k = 0;
  while (k < nn) {
    u32 c;
    if (i >= t->no) return 0;
    c = fold(t->o[i].u);
    if (nd[k] == 32) {
      if (c != 32) return 0;
      while (i + 1 < t->no && fold(t->o[i + 1].u) == 32) i++;
    } else if (c == 0xFB01 && nd[k] == 'f' && k + 1 < nn && nd[k + 1] == 'i') k++;        /* ligatures */
    else if (c == 0xFB02 && nd[k] == 'f' && k + 1 < nn && nd[k + 1] == 'l') k++;
    else if (c != nd[k]) return 0;
    k++;
    if (k < nn) i++;
  }
  *end = i;
  return 1;
}
int textpage_find(TextPage *t, const char *needle, int from, int *a, int *b)
{
  u32 nd[256];
  int nn, i;
  textpage_finish(t);
  nn = needle_cps(needle, nd, 256);
  if (!nn) return 0;
  if (from < 0) from = 0;
  for (i = from; i < t->no; i++) if (match_at(t, nd, nn, i, b)) { *a = i; return 1; }
  return 0;
}
int textpage_find_back(TextPage *t, const char *needle, int from, int *a, int *b)
{
  u32 nd[256];
  int nn, i;
  textpage_finish(t);
  nn = needle_cps(needle, nd, 256);
  if (!nn) return 0;
  if (from > t->no) from = t->no;
  for (i = from - 1; i >= 0; i--) if (match_at(t, nd, nn, i, b)) { *a = i; return 1; }
  return 0;
}
int textpage_hit(TextPage *t, double x, double y, double slop)
{
  int i, best = -1;
  double bd = slop;
  textpage_finish(t);
  for (i = 0; i < t->no; i++) {
    TChar *c = &t->o[i];
    double dx = x < c->x0 ? c->x0 - x : x > c->x1 ? x - c->x1 : 0, dy = y < c->y0 ? c->y0 - y : y > c->y1 ? y - c->y1 : 0;
    double dd = dx + dy;
    if (c->u == 10) continue;
    if (dd < bd || (dd == 0 && best < 0)) { bd = dd; best = i; if (dd == 0) break; }
  }
  return best;
}
