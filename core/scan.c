/*
 * scan.c - OCR and cleaning up scans (see scan.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "scan.h"
#include "geom.h"
#include "pagetools.h"
#include "pagecontent.h"
#include "editpage.h"
#include "contentedit.h"
#include "filters.h"
#include "jpegout.h"

#define G 10                                      /* the shape grid */
typedef struct { int y, x0, x1, p; } Run;          /* p: union-find parent, then the component */
typedef struct { int x0, y0, x1, y1, area, r0, nr, used; } Comp;
typedef struct { Run *r; int nr, capr; Comp *c; int nc; int *order; } Ink;
typedef struct { int x0, y0, x1, y1; int comp[6], ncomp; int cx0, cx1; char ch; float dist; } Glyph;
typedef struct { float g[G * G], asp, top, bot, pieces; unsigned char c; } Feat;

/* ------------------------------------------------------------------ connected pieces of ink */
static int uf_find(Run *r, int i) { while (r[i].p != i) { r[i].p = r[r[i].p].p; i = r[i].p; } return i; }
static int ink_find(const u8 *grey, int w, int h, int thr, Ink *ink)
{
  int y, x, prev0 = 0, prev1 = 0, i, k;
  memset(ink, 0, sizeof *ink);
  for (y = 0; y < h; y++) {
    const u8 *row = grey + (size_t)y * (size_t)w;
    int cur0 = ink->nr;
    for (x = 0; x < w; ) {
      int s;
      if (row[x] >= thr) { x++; continue; }
      s = x;
      while (x < w && row[x] < thr) x++;
      if (ink->nr >= ink->capr) {
        int nc = ink->capr ? ink->capr * 2 : 4096;
        Run *t = (Run *)realloc(ink->r, sizeof(Run) * (size_t)nc);
        if (!t) return 0;
        ink->r = t; ink->capr = nc;
      }
      ink->r[ink->nr].y = y; ink->r[ink->nr].x0 = s; ink->r[ink->nr].x1 = x - 1; ink->r[ink->nr].p = ink->nr;
      /* joined to the runs above it that touch it (eight ways) */
      for (k = prev0; k < prev1; k++) {
        Run *a = &ink->r[k];
        if (a->x1 < s - 1) continue;
        if (a->x0 > x) break;
        {
          int ra = uf_find(ink->r, k), rb = uf_find(ink->r, ink->nr);
          if (ra != rb) { if (ra < rb) ink->r[rb].p = ra; else ink->r[ra].p = rb; }
        }
      }
      ink->nr++;
    }
    prev0 = cur0; prev1 = ink->nr;
  }
  /* the components: their boxes and runs (in order) */
  {
    int *id = (int *)malloc(sizeof(int) * (size_t)(ink->nr + 1)), *cnt;
    if (!id) return 0;
    for (i = 0; i < ink->nr; i++) id[i] = -1;
    for (i = 0; i < ink->nr; i++) { int r = uf_find(ink->r, i); if (id[r] < 0) id[r] = ink->nc++; }
    if (!(ink->c = (Comp *)calloc((size_t)(ink->nc + 1), sizeof(Comp))) || !(ink->order = (int *)malloc(sizeof(int) * (size_t)(ink->nr + 1)))) { free(id); return 0; }
    for (i = 0; i < ink->nc; i++) { ink->c[i].x0 = ink->c[i].y0 = 1 << 30; ink->c[i].x1 = ink->c[i].y1 = -1; }
    for (i = 0; i < ink->nr; i++) {
      Run *r = &ink->r[i];
      Comp *c = &ink->c[id[uf_find(ink->r, i)]];
      if (r->x0 < c->x0) c->x0 = r->x0;
      if (r->x1 > c->x1) c->x1 = r->x1;
      if (r->y < c->y0) c->y0 = r->y;
      if (r->y > c->y1) c->y1 = r->y;
      c->area += r->x1 - r->x0 + 1;
      c->nr++;
    }
    cnt = (int *)calloc((size_t)(ink->nc + 1), sizeof(int));
    if (!cnt) { free(id); return 0; }
    for (i = 0, k = 0; i < ink->nc; i++) { ink->c[i].r0 = k; k += ink->c[i].nr; }
    for (i = 0; i < ink->nr; i++) { int c = id[uf_find(ink->r, i)]; ink->order[ink->c[c].r0 + cnt[c]++] = i; }
    free(cnt); free(id);
  }
  return 1;
}
static void ink_free(Ink *k) { free(k->r); free(k->c); free(k->order); memset(k, 0, sizeof *k); }

/* ------------------------------------------------------------------ glyph shapes */
static void glyph_box(const Ink *ink, Glyph *g)
{
  int i, j;
  g->x0 = g->y0 = 1 << 30; g->x1 = g->y1 = -1;
  for (i = 0; i < g->ncomp; i++) {
    const Comp *c = &ink->c[g->comp[i]];
    for (j = 0; j < c->nr; j++) {
      const Run *r = &ink->r[ink->order[c->r0 + j]];
      int a = r->x0 > g->cx0 ? r->x0 : g->cx0, b = r->x1 < g->cx1 ? r->x1 : g->cx1;
      if (a > b) continue;
      if (a < g->x0) g->x0 = a;
      if (b > g->x1) g->x1 = b;
      if (r->y < g->y0) g->y0 = r->y;
      if (r->y > g->y1) g->y1 = r->y;
    }
  }
}
static void features(const Ink *ink, const Glyph *g, double base, double xh, Feat *f)
{
  int i, j, x;
  double gw = g->x1 - g->x0 + 1, gh = g->y1 - g->y0 + 1;
  memset(f, 0, sizeof *f);
  for (i = 0; i < g->ncomp; i++) {
    const Comp *c = &ink->c[g->comp[i]];
    for (j = 0; j < c->nr; j++) {
      const Run *r = &ink->r[ink->order[c->r0 + j]];
      int a = r->x0 > g->cx0 ? r->x0 : g->cx0, b = r->x1 < g->cx1 ? r->x1 : g->cx1, gy;
      if (a > b || r->y < g->y0 || r->y > g->y1) continue;
      gy = (int)((r->y - g->y0) * G / gh);
      for (x = a; x <= b; x++) f->g[gy * G + (int)((x - g->x0) * G / gw)] += 1;
    }
  }
  for (i = 0; i < G; i++) for (j = 0; j < G; j++) {
    /* each cell's share of the box's pixels it covers */
    double cw = gw / G, ch = gh / G, area = (cw < 1 ? 1 : cw) * (ch < 1 ? 1 : ch);
    double v = f->g[i * G + j] / area;
    f->g[i * G + j] = (float)(v > 1 ? 1 : v);
  }
  for (i = 0; i < g->ncomp; i++) if (ink->c[g->comp[i]].area >= 2) f->pieces += 1;
  f->asp = (float)(gw / gh);
  f->top = (float)((base - g->y0) / xh);
  f->bot = (float)((base - g->y1) / xh);
}
static float fdist(const Feat *a, const Feat *b, float best)
{
  float d, la, t;
  int i;
  la = (float)log((a->asp + 0.05) / (b->asp + 0.05));
  d = 0.6f * la * la + 1.2f * ((a->top - b->top) * (a->top - b->top) + (a->bot - b->bot) * (a->bot - b->bot)) + (a->pieces != b->pieces ? 0.25f : 0);
  if (d >= best) return d;
  for (i = 0; i < G * G; i++) { t = a->g[i] - b->g[i]; d += t * t * (1.0f / 14); if (!(i & 15) && d >= best) return d; }
  return d;
}

/* ------------------------------------------------------------------ lines and glyphs */
typedef struct { int y0, y1, n, cap; Glyph *g; double base, xh; } Line;
typedef struct { Line *l; int n, cap; } Lines;
static const Comp *sort_c;
static int cmp_y(const void *a, const void *b) { const Comp *x = &sort_c[*(const int *)a], *y = &sort_c[*(const int *)b]; return x->y0 - y->y0 ? x->y0 - y->y0 : x->x0 - y->x0; }
static int cmp_gx(const void *a, const void *b) { const Glyph *x = (const Glyph *)a, *y = (const Glyph *)b; return x->x0 - y->x0; }
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int glyph_add(Line *l, const Glyph *g)
{
  if (l->n >= l->cap) {
    int nc = l->cap ? l->cap * 2 : 32;
    Glyph *t = (Glyph *)realloc(l->g, sizeof(Glyph) * (size_t)nc);
    if (!t) return 0;
    l->g = t; l->cap = nc;
  }
  l->g[l->n++] = *g;
  return 1;
}
static void line_metrics(Line *l)
{
  int *v = (int *)malloc(sizeof(int) * (size_t)(l->n + 1)), i, n = 0;
  if (!v || !l->n) { free(v); l->base = l->y1; l->xh = l->y1 - l->y0 + 1; return; }
  for (i = 0; i < l->n; i++) v[n++] = l->g[i].y1;
  qsort(v, (size_t)n, sizeof(int), cmp_int);
  l->base = v[n / 2] + 0.5;
  for (i = 0, n = 0; i < l->n; i++) if (l->g[i].y1 - l->g[i].y0 + 1 >= 2 && l->g[i].y1 >= l->base - 2 - (l->base - l->g[i].y0) * 0.1) v[n++] = (int)(l->base - l->g[i].y0);
  if (!n) for (i = 0; i < l->n; i++) v[n++] = l->g[i].y1 - l->g[i].y0 + 1;
  qsort(v, (size_t)n, sizeof(int), cmp_int);
  l->xh = v[n * 3 / 10];
  if (l->xh < 3) l->xh = 3;
  free(v);
}
/* the ink sorted into lines of glyphs (dots and accents joined to the letters below them) */
static int find_lines(Ink *ink, int pw, int ph, Lines *out)
{
  int *idx, n = 0, i, k;
  int *hs, nh = 0, medh;
  memset(out, 0, sizeof *out);
  if (!ink->nc) return 1;
  if (!(idx = (int *)malloc(sizeof(int) * (size_t)ink->nc)) || !(hs = (int *)malloc(sizeof(int) * (size_t)ink->nc))) { free(idx); return 0; }
  for (i = 0; i < ink->nc; i++) if (ink->c[i].area >= 2) hs[nh++] = ink->c[i].y1 - ink->c[i].y0 + 1;
  qsort(hs, (size_t)nh, sizeof(int), cmp_int);
  medh = nh ? hs[nh / 2] : 10;
  free(hs);
  for (i = 0; i < ink->nc; i++) {
    Comp *c = &ink->c[i];
    int w = c->x1 - c->x0 + 1, h = c->y1 - c->y0 + 1;
    if (c->area < 2 || h > medh * 5 || w > pw / 3 || (w > medh * 8 && h < 4)) continue;   /* specks, pictures, rules */
    idx[n++] = i;
  }
  sort_c = ink->c;
  qsort(idx, (size_t)n, sizeof(int), cmp_y);
  for (i = 0; i < n; i++) {
    Comp *c = &ink->c[idx[i]];
    int best = -1, h = c->y1 - c->y0 + 1;
    double bo = 0;
    Glyph g;
    for (k = out->n - 1; k >= 0 && k >= out->n - 8; k--) {
      Line *l = &out->l[k];
      int ov = (c->y1 < l->y1 ? c->y1 : l->y1) - (c->y0 > l->y0 ? c->y0 : l->y0) + 1, lh = l->y1 - l->y0 + 1;
      double r = (double)ov / (h < lh ? h : lh);
      if (r > 0.4 && r > bo) { bo = r; best = k; }
    }
    if (best < 0) {
      if (out->n >= out->cap) {
        int nc = out->cap ? out->cap * 2 : 64;
        Line *t = (Line *)realloc(out->l, sizeof(Line) * (size_t)nc);
        if (!t) { free(idx); return 0; }
        out->l = t; out->cap = nc;
      }
      best = out->n++;
      memset(&out->l[best], 0, sizeof(Line));
      out->l[best].y0 = c->y0; out->l[best].y1 = c->y1;
    }
    {
      Line *l = &out->l[best];
      if (c->y0 < l->y0) l->y0 = c->y0;
      if (c->y1 > l->y1) l->y1 = c->y1;
      memset(&g, 0, sizeof g);
      g.comp[0] = idx[i]; g.ncomp = 1; g.cx0 = -1; g.cx1 = 1 << 30;
      g.x0 = c->x0; g.x1 = c->x1; g.y0 = c->y0; g.y1 = c->y1;
      glyph_add(l, &g);
    }
  }
  free(idx);
  /* lines of nothing but small marks (dots over a line with no ascenders) joined to the line below */
  for (k = 0; k < out->n; k++) {
    Line *l = &out->l[k];
    int j, big = 0;
    for (j = 0; j < l->n; j++) if (l->g[j].y1 - l->g[j].y0 + 1 > medh / 2) big = 1;
    if (big) continue;
    for (j = 0; j < out->n; j++) {
      Line *m = &out->l[j];
      if (j != k && m->n && m->y0 > l->y1 && m->y0 - l->y1 < medh) {
        int q;
        for (q = 0; q < l->n; q++) glyph_add(m, &l->g[q]);
        if (l->y0 < m->y0) m->y0 = l->y0;
        l->n = 0;
        break;
      }
    }
  }
  /* within each line, pieces one over another made one glyph */
  for (k = 0; k < out->n; k++) {
    Line *l = &out->l[k];
    int j, m = 0;
    qsort(l->g, (size_t)l->n, sizeof(Glyph), cmp_gx);
    for (j = 0; j < l->n; j++) {
      Glyph *g = &l->g[j];
      int q, merged = 0;
      for (q = m - 1; q >= 0 && q >= m - 3 && !merged; q--) {
        Glyph *p = &l->g[q];
        int ov = (g->x1 < p->x1 ? g->x1 : p->x1) - (g->x0 > p->x0 ? g->x0 : p->x0) + 1;
        int wmin = (g->x1 - g->x0 < p->x1 - p->x0 ? g->x1 - g->x0 : p->x1 - p->x0) + 1;
        int vgap = g->y0 > p->y1 ? g->y0 - p->y1 : p->y0 > g->y1 ? p->y0 - g->y1 : 0;
        if (ov * 10 >= wmin * 5 && vgap > 0 && p->ncomp < 6) {
          p->comp[p->ncomp++] = g->comp[0];
          if (g->x0 < p->x0) p->x0 = g->x0;
          if (g->x1 > p->x1) p->x1 = g->x1;
          if (g->y0 < p->y0) p->y0 = g->y0;
          if (g->y1 > p->y1) p->y1 = g->y1;
          merged = 1;
        }
      }
      if (!merged) l->g[m++] = *g;
    }
    l->n = m;
    qsort(l->g, (size_t)l->n, sizeof(Glyph), cmp_gx);
    line_metrics(l);
  }
  (void)ph;
  return 1;
}
static void lines_free(Lines *ls) { int i; for (i = 0; i < ls->n; i++) free(ls->l[i].g); free(ls->l); memset(ls, 0, sizeof *ls); }

/* ------------------------------------------------------------------ the templates */
static Feat *tpl; static int ntpl, captpl, tpl_made;
void ocr_forget_templates(void) { free(tpl); tpl = NULL; ntpl = captpl = 0; tpl_made = 0; }
static const char train_chars[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,;:!?'()-/&%$@*+=#[]"
  "\344\366\374\304\326\334\337\351\350\352\340\342\347\361\353\357\356\364\373\371\341\355\363\372\311";
/* the page drawn into grey (0 black .. 255 white), in bands; malloc'd */
static u8 *draw_grey(PDoc *d, RCache *rc, int page, double scale, int *w, int *h, int (*abort)(void *), void *ud)
{
  RenderOpts o;
  u8 *g;
  int y, band = 128;
  pdf_page_pixels(d, page, scale, 0, w, h);
  if (*w < 8 || *h < 8 || (double)*w * *h > 6e7 || !(g = (u8 *)malloc((size_t)*w * (size_t)*h))) return NULL;
  for (y = 0; y < *h; y += band) {
    Pixmap pm;
    int i, n;
    memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
    o.scale = scale; o.x = 0; o.y = y; o.w = *w; o.h = y + band <= *h ? band : *h - y;
    o.paper = 0xFFFFFF; o.abort = abort; o.ud = ud;
    if (!pdf_render(d, rc, page, &o, &pm) || !pm.rgb) { free(pm.rgb); free(g); return NULL; }
    n = pm.w * pm.h;
    for (i = 0; i < n && i < *w * o.h; i++) g[(size_t)y * (size_t)*w + (size_t)i] = (u8)((pm.rgb[i * 3] * 77 + pm.rgb[i * 3 + 1] * 151 + pm.rgb[i * 3 + 2] * 28) >> 8);
    free(pm.rgb);
  }
  return g;
}
static int otsu(const u8 *g, size_t n)
{
  long hist[256], i;
  double sum = 0, sumb = 0, wb = 0, best = -1, tot = (double)n;
  int t = 128;
  memset(hist, 0, sizeof hist);
  for (i = 0; i < (long)n; i++) hist[g[i]]++;
  for (i = 0; i < 256; i++) sum += (double)i * hist[i];
  for (i = 0; i < 256; i++) {
    double wf, mb, mf, between;
    wb += hist[i];
    if (wb == 0) continue;
    wf = tot - wb;
    if (wf == 0) break;
    sumb += (double)i * hist[i];
    mb = sumb / wb; mf = (sum - sumb) / wf;
    between = wb * wf * (mb - mf) * (mb - mf);
    if (between > best) { best = between; t = (int)i; }
  }
  return t + 1;
}
static void tpl_add(const Feat *f)
{
  if (ntpl >= captpl) {
    int nc = captpl ? captpl * 2 : 1024;
    Feat *t = (Feat *)realloc(tpl, sizeof(Feat) * (size_t)nc);
    if (!t) return;
    tpl = t; captpl = nc;
  }
  tpl[ntpl++] = *f;
}
static void make_templates(void)
{
  static const double sizes[3][2] = { { 9, 200 }, { 15, 200 }, { 11, 300 } };
  const int per = 16;
  int s, font, nch = (int)strlen(train_chars);
  tpl_made = 1;
  for (s = 0; s < 3; s++) {
    PDoc *d = doc_new_blank(1100, 60 + (double)(((nch + per - 1) / per) * 4 + 4) * sizes[s][0] * 2.2, 1);
    RCache *rc;
    double scale = sizes[s][1] / 72, y, H;
    double base[64];
    int nl = 0, i;
    u8 *g;
    int w, h;
    double bb[4];
    int rot;
    if (!d) continue;
    pdf_page_box(d, 0, bb, &rot);
    H = bb[3];
    y = H - 40;
    for (font = 0; font < 4; font++) for (i = 0; i < nch; i += per) {
      char line[200], utf[400];
      int k, n = 0, o = 0;
      for (k = i; k < i + per && k < nch; k++) { line[n++] = train_chars[k]; line[n++] = ' '; line[n++] = 'x'; line[n++] = ' '; }
      line[n] = 0;
      for (k = 0; k < n; k++) o += utf8_put(utf + o, (u8)line[k]);
      utf[o] = 0;
      page_add_text(d, 0, 40, y, utf, sizes[s][0], 0, font);
      if (nl < 64) base[nl++] = y;
      y -= sizes[s][0] * 2.2;
    }
    rc = rcache_new(d);
    g = draw_grey(d, rc, 0, scale, &w, &h, NULL, NULL);
    rcache_free(rc);
    if (g) {
      Ink ink;
      Lines ls;
      if (ink_find(g, w, h, 160, &ink) && find_lines(&ink, w, h, &ls)) {
        int li = 0;
        for (font = 0; font < 4; font++) for (i = 0; i < nch; i += per, li++) {
          /* the line drawn at base[li]: its glyphs matched to the characters by where each falls */
          double by = (H - base[li]) * scale, xs[200];
          int k, n = 0, best = -1, q;
          char chars[200];
          double bd = 1e9;
          for (k = i; k < i + per && k < nch; k++) { chars[n++] = train_chars[k]; chars[n++] = ' '; chars[n++] = 'x'; chars[n++] = ' '; }
          for (q = 0; q < ls.n; q++) { double dd = fabs(ls.l[q].base - by); if (ls.l[q].n && dd < bd) { bd = dd; best = q; } }
          if (best < 0 || bd > sizes[s][0] * scale * 1.1) continue;
          xs[0] = 40;
          for (k = 0; k < n; k++) xs[k + 1] = xs[k] + om_text_width(&chars[k], 1, sizes[s][0], font == 3 ? 3 : font);
          {
            Line *l = &ls.l[best];
            int seen_x = 0;
            Glyph *u = (Glyph *)calloc((size_t)n + 1, sizeof(Glyph));
            if (!u) continue;
            /* every piece falling in a character's advance joined into that character's glyph */
            for (q = 0; q < l->n; q++) {
              Glyph *gg = &l->g[q];
              double cx = (gg->x0 + gg->x1 + 1) / 2.0 / scale;
              int j;
              for (k = 0; k < n; k++) if (cx >= xs[k] && cx < xs[k + 1]) break;
              if (k >= n || chars[k] == ' ') continue;
              if (!u[k].ncomp) { u[k] = *gg; continue; }
              for (j = 0; j < gg->ncomp && u[k].ncomp < 6; j++) u[k].comp[u[k].ncomp++] = gg->comp[j];
            }
            for (k = 0; k < n; k++) {
              Feat f;
              if (!u[k].ncomp || chars[k] == ' ') continue;
              if (chars[k] == 'x' && seen_x++) continue;
              u[k].cx0 = -1; u[k].cx1 = 1 << 30;
              glyph_box(&ink, &u[k]);
              features(&ink, &u[k], l->base, l->xh, &f);
              f.c = (unsigned char)chars[k];
              tpl_add(&f);
            }
            free(u);
          }
        }
        lines_free(&ls);
      }
      ink_free(&ink);
      free(g);
    }
    pdf_close(d);
  }
}

/* ------------------------------------------------------------------ recognising */
static float classify(const Ink *ink, Glyph *g, double base, double xh)
{
  Feat f;
  float best = 1e9f;
  int i, bi = -1;
  glyph_box(ink, g);
  if (g->x1 < g->x0) { g->ch = '?'; return 1e9f; }
  features(ink, g, base, xh, &f);
  for (i = 0; i < ntpl; i++) { float d = fdist(&f, &tpl[i], best); if (d < best) { best = d; bi = i; } }
  g->ch = bi >= 0 ? (char)tpl[bi].c : '?';
  g->dist = best;
  return best;
}
/* a glyph wider than a letter split where the ink is thinnest, if the parts match better */
static int split_glyph(const Ink *ink, Glyph *g, double base, double xh, Glyph *out, int max, int depth)
{
  int w = g->x1 - g->x0 + 1, x, bx = -1, i, j;
  long bestcol = 1L << 30;
  Glyph a, b;
  float d0 = g->dist;
  if (depth > 2 || max < 2 || w < xh * 0.9 || d0 < 0.12f) { out[0] = *g; return 1; }
  for (x = g->x0 + (int)(w * 0.25); x <= g->x0 + (int)(w * 0.75); x++) {
    long col = 0;
    for (i = 0; i < g->ncomp; i++) {
      const Comp *c = &ink->c[g->comp[i]];
      for (j = 0; j < c->nr; j++) { const Run *r = &ink->r[ink->order[c->r0 + j]]; if (r->x0 <= x && r->x1 >= x) col++; }
    }
    if (col < bestcol) { bestcol = col; bx = x; }
  }
  if (bx < 0) { out[0] = *g; return 1; }
  a = *g; b = *g;
  a.cx1 = bx - 1; b.cx0 = bx;
  if (a.cx0 > g->x0 && a.cx0 >= 0) ; else a.cx0 = g->cx0;
  classify(ink, &a, base, xh); classify(ink, &b, base, xh);
  if ((a.dist + b.dist) / 2 < d0 * 0.75f) {
    int n = split_glyph(ink, &a, base, xh, out, max - 1, depth + 1);
    n += split_glyph(ink, &b, base, xh, out + n, max - n, depth + 1);
    return n;
  }
  out[0] = *g;
  return 1;
}
static int is_up(int c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7); }
static int is_low(int c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c != 0xF7); }
static int is_dig(int c) { return c >= '0' && c <= '9'; }
/* a word put right by its neighbours: letters in numbers, digits in words, the case of shapes that
   only differ in size */
static void fix_word(unsigned char *s, int n)
{
  int i, up = 0, low = 0, dig = 0;
  if (n > 1 && s[0] == 'l' && strchr("bcdfghjkmnpqrstvwxz", s[1])) s[0] = 'I';   /* ("ln...", "lt...": a capital I) */
  for (i = 0; i < n; i++) { if (is_up(s[i])) up++; else if (is_low(s[i])) low++; else if (is_dig(s[i])) dig++; }
  for (i = 0; i < n; i++) {
    unsigned char c = s[i];
    if (dig > up + low) {
      if (c == 'O' || c == 'o') s[i] = '0';
      else if (c == 'l' || c == 'I' || c == '|') s[i] = '1';
      else if (c == 'S' && n > 1) s[i] = '5';
      else if (c == 'B' && n > 1) s[i] = '8';
    } else if (up + low > 0) {
      if (c == '0') s[i] = up > low ? 'O' : 'o';
      else if (c == '1' && i > 0) s[i] = up > low ? 'I' : 'l';
      else if (c == '5' && low > dig) s[i] = 's';
      if (low > 0 && i > 0 && c == 'I' && is_low(s[i - 1])) s[i] = 'l';
      if (up > low + 1 || (up && !low && n > 1)) {
        static const char amb[] = "cosuvwxz";
        if (strchr(amb, c) && c) s[i] = (unsigned char)(c - 32);
      }
      if (i > 0 && is_low(s[i - 1]) && low >= up) {
        /* (a capital after a small letter is rare: shapes that differ only in size are taken small) */
        static const char amb[] = "COSUVWXZ";
        if (c && strchr(amb, c)) s[i] = (unsigned char)(c + 32);
        else if (c == 'I') s[i] = 'l';
      }
    }
  }
}
static int word_add(OcrResult *r, const OcrWord *w)
{
  if (r->n >= r->cap) {
    int nc = r->cap ? r->cap * 2 : 256;
    OcrWord *t = (OcrWord *)realloc(r->w, sizeof(OcrWord) * (size_t)nc);
    if (!t) return 0;
    r->w = t; r->cap = nc;
  }
  r->w[r->n++] = *w;
  return 1;
}
int ocr_page(PDoc *d, RCache *rc, int page, int dpi, int (*abort)(void *), void *ud, OcrResult *res)
{
  double scale;
  u8 *g;
  int w, h, thr, li;
  Ink ink;
  Lines ls;
  Mat m, inv;
  memset(res, 0, sizeof *res);
  if (!tpl_made) make_templates();
  if (!ntpl) return 0;
  if (dpi <= 0) dpi = 200;
  scale = dpi / 72.0;
  if (!(g = draw_grey(d, rc, page, scale, &w, &h, abort, ud))) return 0;
  thr = otsu(g, (size_t)w * (size_t)h);
  if (thr > 200) thr = 200;
  if (thr < 60) thr = 128;
  if (!ink_find(g, w, h, thr, &ink)) { free(g); ink_free(&ink); return 0; }
  free(g);
  if (!find_lines(&ink, w, h, &ls)) { ink_free(&ink); return 0; }
  m = pdf_page_matrix(d, page, scale, 0);
  mat_invert(m, &inv);
  for (li = 0; li < ls.n; li++) {
    Line *l = &ls.l[li];
    Glyph *out;
    int i, no = 0;
    if (l->n < 1 || l->xh < 3) continue;
    if (abort && abort(ud)) break;
    if (!(out = (Glyph *)malloc(sizeof(Glyph) * (size_t)(l->n * 3 + 1)))) break;
    for (i = 0; i < l->n; i++) {
      classify(&ink, &l->g[i], l->base, l->xh);
      no += split_glyph(&ink, &l->g[i], l->base, l->xh, out + no, 3, 0);
    }
    /* words: glyphs closer than the gap a space makes */
    {
      int s = 0;
      while (s < no) {
        int e = s + 1;
        OcrWord wd;
        double conf = 0, x0, y0, x1, y1;
        int k, n = 0;
        while (e < no && out[e].x0 - out[e - 1].x1 - 1 < l->xh * 0.42) e++;
        memset(&wd, 0, sizeof wd);
        x0 = out[s].x0; x1 = out[e - 1].x1; y0 = 1e9; y1 = -1e9;
        for (k = s; k < e && n < 62; k++) {
          unsigned char c = (unsigned char)out[k].ch;
          /* two straight quotes side by side are a double one */
          if (c == '\'' && k + 1 < e && out[k + 1].ch == '\'' && out[k + 1].x0 - out[k].x1 < l->xh * 0.3) { wd.text[n++] = '"'; k++; }
          else wd.text[n++] = (char)c;
          conf += out[k].dist;
          if (out[k].y0 < y0) y0 = out[k].y0;
          if (out[k].y1 > y1) y1 = out[k].y1;
        }
        wd.text[n] = 0;
        fix_word((unsigned char *)wd.text, n);
        wd.conf = (float)exp(-(conf / (e - s)) * 4);
        /* its box in page units: from the line's baseline up an x-height and a half */
        {
          double ax, ay, bx, by;
          double top = l->base - l->xh * 1.45, bot = l->base + l->xh * 0.45;
          if (y0 < top) top = y0;
          if (y1 > bot) bot = y1;
          mat_apply(&inv, x0, bot, &ax, &ay); mat_apply(&inv, x1 + 1, top, &bx, &by);
          wd.box[0] = ax < bx ? ax : bx; wd.box[2] = ax < bx ? bx : ax;
          wd.box[1] = ay < by ? ay : by; wd.box[3] = ay < by ? by : ay;
        }
        if (n) word_add(res, &wd);
        s = e;
      }
    }
    free(out);
    res->lines++;
    if (res->n) res->w[res->n - 1].text[63] = 0;
    /* (a line's end: marked by a word with an empty text? no: words carry their line in the order) */
  }
  lines_free(&ls);
  ink_free(&ink);
  return 1;
}
void ocr_free(OcrResult *r) { free(r->w); memset(r, 0, sizeof *r); }
char *ocr_text(const OcrResult *r)
{
  Buf b;
  int i;
  buf_init(&b);
  for (i = 0; i < r->n; i++) {
    if (i) {
      const OcrWord *p = &r->w[i - 1], *q = &r->w[i];
      double ph = p->box[3] - p->box[1];
      buf_addc(&b, fabs((q->box[1] + q->box[3]) - (p->box[1] + p->box[3])) / 2 > ph * 0.5 || q->box[0] < p->box[0] ? '\n' : ' ');
    }
    buf_adds(&b, r->w[i].text);
  }
  buf_addc(&b, 0);
  return b.oom ? NULL : (char *)buf_take(&b);
}
int ocr_add_text_layer(PDoc *d, int page, const OcrResult *r)
{
  PObj *res = pc_resources(d, pc_page(d, page));
  char nm[40];
  Buf b;
  int i, ok;
  if (!res || !r->n) return 0;
  pc_remove_marked(d, page, "OCR");
  if (!pc_add_resource(d, res, "Font", "FoOCR", om_std_font("Helvetica"), nm, sizeof nm)) return 0;
  buf_init(&b);
  buf_adds(&b, "BT 3 Tr\n");
  for (i = 0; i < r->n; i++) {
    const OcrWord *w = &r->w[i];
    int n = (int)strlen(w->text);
    double hgt = w->box[3] - w->box[1], size = hgt / 1.35, tw = om_text_width(w->text, n, size, 0), tz;
    if (n < 1 || size <= 0) continue;
    tz = tw > 0 ? 100 * (w->box[2] - w->box[0]) / tw : 100;
    buf_printf(&b, "/%s ", nm); om_num(&b, size); buf_adds(&b, "Tf "); om_num(&b, tz); buf_adds(&b, "Tz 1 0 0 1 ");
    om_num(&b, w->box[0]); om_num(&b, w->box[1] + hgt * 0.25); buf_adds(&b, "Tm ");
    om_str(&b, w->text, n); buf_adds(&b, " Tj\n");
  }
  buf_adds(&b, "ET\n");
  ok = !b.oom && pc_add_content(d, page, b.data, b.len, 0, "OCR");
  buf_free(&b);
  if (ok) d->changed = 1;
  return ok;
}

/* ------------------------------------------------------------------ cleaning scans */
double scan_skew(const u8 *grey, int w, int h)
{
  Ink ink;
  int thr = otsu(grey, (size_t)w * (size_t)h), i, n = 0, a, nb;
  double *px, *py, best = 0, bs = -1, step;
  long *hist;
  if (!ink_find(grey, w, h, thr, &ink)) { ink_free(&ink); return 0; }
  px = (double *)malloc(sizeof(double) * (size_t)(ink.nc + 1)); py = (double *)malloc(sizeof(double) * (size_t)(ink.nc + 1));
  nb = h * 2 + 4;
  hist = (long *)malloc(sizeof(long) * (size_t)nb);
  if (!px || !py || !hist) { free(px); free(py); free(hist); ink_free(&ink); return 0; }
  for (i = 0; i < ink.nc; i++) {
    Comp *c = &ink.c[i];
    int ch = c->y1 - c->y0 + 1, cw = c->x1 - c->x0 + 1;
    if (c->area < 6 || ch > h / 20 || cw > w / 20) continue;
    px[n] = (c->x0 + c->x1) / 2.0 - w / 2.0; py[n] = c->y1 + 0.5; n++;
  }
  /* the angle whose rows of glyph bottoms line up best: coarse, then fine */
  for (step = 0.25; step >= 0.02; step /= 5) {
    double lo = step == 0.25 ? -10 : best - step * 5, hi = step == 0.25 ? 10 : best + step * 5, ang;
    for (ang = lo; ang <= hi + 1e-9; ang += step) {
      double t = tan(ang * 3.14159265358979 / 180), s = 0;
      memset(hist, 0, sizeof(long) * (size_t)nb);
      for (i = 0; i < n; i++) { int b = (int)(py[i] + px[i] * t + h / 2.0); if (b >= 0 && b < nb) hist[b]++; }
      for (a = 0; a < nb; a++) s += (double)hist[a] * hist[a];
      if (s > bs) { bs = s; best = ang; }
    }
  }
  free(px); free(py); free(hist);
  ink_free(&ink);
  return best;
}
/* the picture turned by ang degrees about its middle (white where nothing comes in), bilinear */
static void rotate_grey(const u8 *in, u8 *out, int w, int h, int comps, double ang)
{
  double t = ang * 3.14159265358979 / 180, c = cos(t), s = sin(t), cx = w / 2.0, cy = h / 2.0;
  int x, y, k;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
    /* where the pixel comes from */
    double dx = x + 0.5 - cx, dy = y + 0.5 - cy, sx = c * dx - s * dy + cx - 0.5, sy = s * dx + c * dy + cy - 0.5;
    int ix = (int)floor(sx), iy = (int)floor(sy);
    double fx = sx - ix, fy = sy - iy;
    for (k = 0; k < comps; k++) {
      double v00, v10, v01, v11;
#define PX(X, Y) ((X) >= 0 && (X) < w && (Y) >= 0 && (Y) < h ? in[((size_t)(Y) * (size_t)w + (size_t)(X)) * (size_t)comps + (size_t)k] : 255)
      v00 = PX(ix, iy); v10 = PX(ix + 1, iy); v01 = PX(ix, iy + 1); v11 = PX(ix + 1, iy + 1);
#undef PX
      out[((size_t)y * (size_t)w + (size_t)x) * (size_t)comps + (size_t)k] = (u8)(v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy) + v01 * (1 - fx) * fy + v11 * fx * fy + 0.5);
    }
  }
}
int scan_clean_page(PDoc *d, int page, const ScanOpts *o, double *angle)
{
  PageImage *im;
  int n = page_images(d, page, &im), i, best = -1, w, h, comps, bpc, as_rgb, ok = 0;
  double bb[4], parea, ba = 0;
  int rot;
  u8 *s, *gr = NULL;
  PObj *img;
  if (angle) *angle = 0;
  pdf_page_box(d, page, bb, &rot);
  parea = (bb[2] - bb[0]) * (bb[3] - bb[1]);
  for (i = 0; i < n; i++) {
    double a = fabs(im[i].m[0] * im[i].m[3] - im[i].m[1] * im[i].m[2]);
    if (!im[i].inline_img && im[i].num && a > ba) { ba = a; best = i; }
  }
  if (best < 0 || ba < parea * 0.4) { free(im); return 0; }
  img = pdf_get(d, im[best].num);
  if (!img || !(s = ce_image_samples(d, img, &w, &h, &comps, &bpc, &as_rgb)) || w < 16 || h < 16) { free(im); return 0; }
  if (as_rgb) comps = 3;
  if (bpc != 8 || (comps != 1 && comps != 3)) {
    /* (1-bit scans and others: as grey) */
    if (bpc == 1 && comps == 1) {
      u8 *g2 = (u8 *)malloc((size_t)w * (size_t)h);
      size_t rb = ((size_t)w + 7) / 8;
      int x, y;
      if (g2) for (y = 0; y < h; y++) for (x = 0; x < w; x++) g2[(size_t)y * (size_t)w + (size_t)x] = (s[(size_t)y * rb + (size_t)(x >> 3)] >> (7 - (x & 7))) & 1 ? 255 : 0;
      free(s); s = g2; comps = 1;
      if (!s) { free(im); return 0; }
    } else { free(s); free(im); return 0; }
  }
  /* the grey used for measuring (and the result when it is grey) */
  if (!(gr = (u8 *)malloc((size_t)w * (size_t)h))) { free(s); free(im); return 0; }
  for (i = 0; i < w * h; i++) gr[i] = comps == 1 ? s[i] : (u8)((s[i * 3] * 77 + s[i * 3 + 1] * 151 + s[i * 3 + 2] * 28) >> 8);
  if (o->camera) {
    /* the light: the brightest in blocks, smoothed, divided out */
    int bs = (w > h ? w : h) / 24 + 8, bw = (w + bs - 1) / bs, bh = (h + bs - 1) / bs, bx, x, y;
    int *mx = (int *)calloc((size_t)(bw * bh), sizeof(int));
    if (mx) {
      for (y = 0; y < h; y++) for (x = 0; x < w; x++) { int v = gr[(size_t)y * (size_t)w + (size_t)x], *m = &mx[(y / bs) * bw + x / bs]; if (v > *m) *m = v; }
      for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        double fx = (x + 0.5) / bs - 0.5, fy = (y + 0.5) / bs - 0.5, l;
        int x0, y0, x1, y1;
        if (fx < 0) fx = 0;
        if (fy < 0) fy = 0;
        x0 = (int)fx; y0 = (int)fy; x1 = x0 + 1 < bw ? x0 + 1 : x0; y1 = y0 + 1 < bh ? y0 + 1 : y0;
        fx -= x0; fy -= y0;
        l = mx[y0 * bw + x0] * (1 - fx) * (1 - fy) + mx[y0 * bw + x1] * fx * (1 - fy) + mx[y1 * bw + x0] * (1 - fx) * fy + mx[y1 * bw + x1] * fx * fy;
        if (l < 32) l = 32;
        for (bx = 0; bx < comps; bx++) {
          size_t at = ((size_t)y * (size_t)w + (size_t)x) * (size_t)comps + (size_t)bx;
          double v = s[at] * 255.0 / l;
          s[at] = (u8)(v > 255 ? 255 : v);
        }
      }
      free(mx);
      for (i = 0; i < w * h; i++) gr[i] = comps == 1 ? s[i] : (u8)((s[i * 3] * 77 + s[i * 3 + 1] * 151 + s[i * 3 + 2] * 28) >> 8);
      ok = 1;
    }
  }
  if (o->whiten || o->camera) {
    /* levels: the paper's grey (most of the page) to white, the darkest percent to black */
    long hist[256], acc = 0, tot = (long)w * h;
    int lo = 0, hi = 255, v;
    memset(hist, 0, sizeof hist);
    for (i = 0; i < w * h; i++) hist[gr[i]]++;
    for (v = 0; v < 256; v++) { acc += hist[v]; if (acc >= tot / 100) { lo = v; break; } }
    for (acc = 0, v = 255; v >= 0; v--) { acc += hist[v]; if (acc >= tot / 3) { hi = v; break; } }
    if (hi - lo > 20) {
      for (i = 0; i < w * h * comps; i++) { int q = (s[i] - lo) * 255 / (hi - lo); s[i] = (u8)(q < 0 ? 0 : q > 255 ? 255 : q); }
      for (i = 0; i < w * h; i++) { int q = (gr[i] - lo) * 255 / (hi - lo); gr[i] = (u8)(q < 0 ? 0 : q > 255 ? 255 : q); }
      ok = 1;
    }
  }
  if (o->deskew) {
    double a = scan_skew(gr, w, h);
    if (angle) *angle = a;
    if (fabs(a) >= 0.1) {
      u8 *t = (u8 *)malloc((size_t)w * (size_t)h * (size_t)comps), *t2 = (u8 *)malloc((size_t)w * (size_t)h);
      if (t && t2) {
        rotate_grey(s, t, w, h, comps, -a); free(s); s = t;
        rotate_grey(gr, t2, w, h, 1, -a); free(gr); gr = t2;
        ok = 1;
      } else { free(t); free(t2); }
    }
  }
  {
    /* black and white: wanted, or the scan nearly is */
    long mid = 0;
    int thr = otsu(gr, (size_t)w * (size_t)h), bit;
    for (i = 0; i < w * h; i++) if (gr[i] > 48 && gr[i] < 208) mid++;
    bit = o->bitonal == 1 || (o->bitonal == 2 && mid < (long)w * h / 25);
    if (o->despeckle) {
      Ink ink;
      int dpi = (int)(w / (fabs(im[best].m[0]) + fabs(im[best].m[1])) * 72 + 0.5), lim = dpi > 0 ? dpi * dpi / 30000 + 1 : 3, k, j;
      if (lim > 12) lim = 12;
      if (ink_find(gr, w, h, thr, &ink)) for (k = 0; k < ink.nc; k++) if (ink.c[k].area <= lim) {
        for (j = 0; j < ink.c[k].nr; j++) {
          Run *r = &ink.r[ink.order[ink.c[k].r0 + j]];
          int x;
          for (x = r->x0; x <= r->x1; x++) {
            size_t at = (size_t)r->y * (size_t)w + (size_t)x;
            int q;
            gr[at] = 255;
            for (q = 0; q < comps; q++) s[at * (size_t)comps + (size_t)q] = 255;
          }
        }
        ok = 1;
      }
      ink_free(&ink);
    }
    if (bit) {
      u8 *b1 = (u8 *)malloc((size_t)w * (size_t)h), *z;
      size_t zl;
      if (b1) {
        for (i = 0; i < w * h; i++) b1[i] = gr[i] < thr;
        if ((z = flt_ccitt_g4_encode(b1, w, h, &zl))) {
          PObj *dict = pobj_new(PO_DICT), *dp = pobj_new(PO_DICT);
          int num;
          pobj_dict_set(dict, "Type", pobj_name("XObject"));
          pobj_dict_set(dict, "Subtype", pobj_name("Image"));
          pobj_dict_set(dict, "Width", pobj_int(w));
          pobj_dict_set(dict, "Height", pobj_int(h));
          pobj_dict_set(dict, "BitsPerComponent", pobj_int(1));
          pobj_dict_set(dict, "ColorSpace", pobj_name("DeviceGray"));
          pobj_dict_set(dict, "Filter", pobj_name("CCITTFaxDecode"));
          pobj_dict_set(dp, "K", pobj_int(-1)); pobj_dict_set(dp, "Columns", pobj_int(w)); pobj_dict_set(dp, "Rows", pobj_int(h));
          pobj_dict_set(dict, "DecodeParms", dp);
          num = om_stream(d, dict, z, zl);
          free(z);
          if (num) {
            ok = image_replace_num(d, page, best, num);
            free(b1); free(gr); free(s); free(im);
            return ok;
          }
        }
        free(b1);
      }
    }
  }
  if (ok) {
    /* grey or colour again: JPEG (colour) or Flate (grey) */
    PObj *dict = pobj_new(PO_DICT);
    u8 *z = NULL;
    size_t zl = 0;
    int num = 0;
    pobj_dict_set(dict, "Type", pobj_name("XObject"));
    pobj_dict_set(dict, "Subtype", pobj_name("Image"));
    pobj_dict_set(dict, "Width", pobj_int(w));
    pobj_dict_set(dict, "Height", pobj_int(h));
    pobj_dict_set(dict, "BitsPerComponent", pobj_int(8));
    pobj_dict_set(dict, "ColorSpace", pobj_name(comps == 3 ? "DeviceRGB" : "DeviceGray"));
    if (comps == 3 && (z = jpeg_write_rgb(s, w, h, o->quality > 0 ? o->quality : 75, &zl))) pobj_dict_set(dict, "Filter", pobj_name("DCTDecode"));
    else if ((z = flt_deflate(comps == 3 ? s : gr, (size_t)w * (size_t)h * (size_t)comps, 6, &zl))) pobj_dict_set(dict, "Filter", pobj_name("FlateDecode"));
    if (z) num = om_stream(d, dict, z, zl); else pobj_free(dict);
    free(z);
    ok = num && image_replace_num(d, page, best, num);
  }
  free(gr); free(s); free(im);
  return ok;
}
