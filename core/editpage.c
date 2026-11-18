/*
 * editpage.c - editing a page's own text and pictures, and its paths' points (see editpage.h).
 *
 * Retyping walks the content twice with core/contentedit.c: first to see every glyph (its box,
 * rendering matrix, font resource name, bytes, colour), then to take out the run being retyped and
 * the rest of its line. The new text and the rest of the line, moved by the difference in width,
 * are then drawn by a stream of their own after the page's content.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "editpage.h"
#include "contentedit.h"
#include "pagecontent.h"
#include "pagetools.h"
#include "font.h"

typedef struct {
  double box[4], trm[6], fill[4], adv;
  char fname[40]; u8 bytes[4]; int nb, nfill, in_form, tmode;
  int bold, serif, fixed; u32 code;
  int remove, run;
} Glyph;
typedef struct { Glyph *g; int n, cap, pass, at; } GList;

static int g_collect(void *ud, const double *box, u32 code, const CEState *s)
{
  GList *l = (GList *)ud;
  Glyph *g;
  if (l->pass == 2) return l->at < l->n && l->g[l->at++].remove;
  if (l->n >= l->cap) {
    int nc = l->cap ? l->cap * 2 : 256;
    Glyph *t = (Glyph *)realloc(l->g, sizeof(Glyph) * (size_t)nc);
    if (!t) return 0;
    l->g = t; l->cap = nc;
  }
  g = &l->g[l->n++];
  memset(g, 0, sizeof *g);
  memcpy(g->box, box, sizeof g->box); memcpy(g->trm, s->trm, sizeof g->trm); memcpy(g->fill, s->fill, sizeof g->fill);
  snprintf(g->fname, sizeof g->fname, "%s", s->fname);
  g->nb = s->nbytes > 4 ? 4 : s->nbytes;
  if (s->bytes && g->nb > 0) memcpy(g->bytes, s->bytes, (size_t)g->nb);
  g->nfill = s->nfill; g->in_form = s->in_form; g->tmode = s->tmode;
  g->code = code;
  if (s->font) { g->bold = s->font->bold; g->serif = s->font->serif; g->fixed = s->font->fixed; }
  g->adv = (s->font ? font_width(s->font, code, g->nb) / 1000.0 : 0.5);
  return 0;
}
static int horizontal(const Glyph *g) { return g->trm[0] > 1e-6 && fabs(g->trm[1]) < 1e-3 * g->trm[0] && fabs(g->trm[2]) < 1e-3 * fabs(g->trm[3]) && g->trm[3] > 1e-6; }
static void put_fill(Buf *b, const Glyph *g)
{
  int i;
  if (g->nfill == 0) { buf_adds(b, "0 g "); return; }
  for (i = 0; i < g->nfill; i++) om_num(b, g->fill[i]);
  buf_adds(b, g->nfill == 1 ? "g " : g->nfill == 3 ? "rg " : "k ");
}
static void put_tm(Buf *b, const double *m, double dx)
{
  om_num(b, m[0]); om_num(b, m[1]); om_num(b, m[2]); om_num(b, m[3]); om_num(b, m[4] + dx); om_num(b, m[5]); buf_adds(b, "Tm ");
}
static int cmp_x(const void *a, const void *b)
{
  const Glyph *x = *(const Glyph *const *)a, *y = *(const Glyph *const *)b;
  return x->trm[4] < y->trm[4] ? -1 : x->trm[4] > y->trm[4];
}
/* utf8 as codes of font f (as long as the run's codes, nb bytes each): 1 if every character has a glyph */
static int encode_in(const PdfFont *f, int nb, const u32 *u, int nu, Buf *codes, double *width)
{
  int i, c;
  *width = 0;
  if (!f || (nb != 1 && nb != 2)) return 0;
  if (nb == 1 && f->kind == PFT_SIMPLE) {
    for (i = 0; i < nu; i++) {
      int best = -1;
      for (c = 0; c < 256 && best < 0; c++) if (f->uni[c] == u[i] && (u[i] == 32 || f->substitute || font_gid(f, (u32)c, 1) > 0)) best = c;
      if (best < 0) return 0;
      buf_addc(codes, best);
      *width += font_width(f, (u32)best, 1) / 1000.0;
    }
    return 1;
  }
  if (nb == 2 && f->kind == PFT_TYPE0 && f->tounicode) {
    /* the CMap read backwards, once for all the characters wanted */
    long *code = (long *)malloc(sizeof(long) * (size_t)(nu ? nu : 1));
    int left = nu, ok = 1;
    u32 k;
    if (!code) return 0;
    for (i = 0; i < nu; i++) code[i] = -1;
    for (k = 0; k < 65536 && left; k++) {
      u32 out[4];
      if (cmap_unicode(f->tounicode, k, 2, out, 4) == 1 && font_gid(f, k, 2) > 0)
        for (i = 0; i < nu; i++) if (code[i] < 0 && out[0] == u[i]) { code[i] = (long)k; left--; }
    }
    for (i = 0; i < nu && ok; i++) {
      if (code[i] < 0) { ok = 0; break; }
      buf_addc(codes, (int)(code[i] >> 8)); buf_addc(codes, (int)(code[i] & 255));
      *width += font_width(f, (u32)code[i], 2) / 1000.0;
    }
    free(code);
    return ok;
  }
  return 0;
}
static int utf8_codes(const char *s, u32 *out, int max)
{
  int n = 0;
  const u8 *p = (const u8 *)s;
  while (*p && n < max) {
    u32 c = *p++;
    if (c >= 0xC0) {
      int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
      c &= 0x3F >> k;
      while (k-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
    }
    out[n++] = c;
  }
  return n;
}
int text_replace(PDoc *d, int page, const double *sel, const char *utf8)
{
  GList l;
  CEOps ops;
  Glyph *first = NULL, *last = NULL, **tail = NULL, **head = NULL;
  int i, nt = 0, nh = 0, how = TE_NONE, nu;
  double base, size, oldw, neww = 0, dx;
  Buf b, codes;
  u32 *u;
  PdfFont *rf = NULL;
  memset(&l, 0, sizeof l); memset(&ops, 0, sizeof ops);
  ops.ud = &l; ops.glyph = g_collect;
  l.pass = 1;
  ce_rewrite_page(d, page, &ops);                     /* (nothing removed: only seen) */
  for (i = 0; i < l.n; i++) {
    Glyph *g = &l.g[i];
    double cx = (g->box[0] + g->box[2]) / 2, cy = (g->box[1] + g->box[3]) / 2;
    if (cx >= sel[0] && cx <= sel[2] && cy >= sel[1] && cy <= sel[3] && horizontal(g)) {
      g->run = g->remove = 1;
      if (!first || g->trm[4] < first->trm[4]) first = g;
      if (!last || g->trm[4] > last->trm[4]) last = g;
    }
  }
  if (!first) { free(l.g); return TE_NONE; }
  base = first->trm[5]; size = first->trm[3];
  oldw = last->trm[4] + last->adv * last->trm[0] - first->trm[4];
  /* the rest of the line: on the same baseline after the run, up to a gap wider than two ems */
  if (!(tail = (Glyph **)malloc(sizeof(Glyph *) * (size_t)l.n))) { free(l.g); return TE_NONE; }
  for (i = 0; i < l.n; i++) {
    Glyph *g = &l.g[i];
    if (!g->run && horizontal(g) && !g->in_form && fabs(g->trm[5] - base) < size * 0.3 && g->trm[4] > last->trm[4]) tail[nt++] = g;
  }
  /* (and its start, written again where it is, so that the line stays together for reading it back) */
  if (!(head = (Glyph **)malloc(sizeof(Glyph *) * (size_t)l.n))) { free(tail); free(l.g); return TE_NONE; }
  for (i = 0; i < l.n; i++) {
    Glyph *g = &l.g[i];
    if (!g->run && horizontal(g) && !g->in_form && fabs(g->trm[5] - base) < size * 0.3 && g->trm[4] < first->trm[4]) head[nh++] = g;
  }
  qsort(head, (size_t)nh, sizeof head[0], cmp_x);
  {
    double start = first->trm[4];
    int keep = nh;
    for (i = nh - 1; i >= 0; i--) {
      if (start - (head[i]->trm[4] + head[i]->adv * head[i]->trm[0]) > 2 * size) break;
      start = head[i]->trm[4];
      keep = i;
    }
    for (i = keep; i < nh; i++) head[i]->remove = 1;
    memmove(head, head + keep, sizeof(Glyph *) * (size_t)(nh - keep));
    nh -= keep;
  }
  qsort(tail, (size_t)nt, sizeof tail[0], cmp_x);
  {
    double end = last->trm[4] + last->adv * last->trm[0];
    for (i = 0; i < nt; i++) {
      if (tail[i]->trm[4] - end > 2 * size) break;
      tail[i]->remove = 1;
      end = tail[i]->trm[4] + tail[i]->adv * tail[i]->trm[0];
    }
    nt = i;
  }
  /* the new text: in the run's font if it can be, else a standard one */
  nu = (int)strlen(utf8);
  if (!(u = (u32 *)malloc(sizeof(u32) * (size_t)(nu + 1)))) { free(tail); free(head); free(l.g); return TE_NONE; }
  nu = utf8_codes(utf8, u, nu);
  buf_init(&b); buf_init(&codes);
  buf_adds(&b, "BT\n");
  for (i = 0; i < nh; i++) {
    Glyph *g = head[i];
    int k;
    buf_printf(&b, "%d Tr ", g->tmode);
    put_fill(&b, g);
    buf_printf(&b, "/%s 1 Tf ", g->fname);
    put_tm(&b, g->trm, 0);
    buf_addc(&b, '<');
    for (k = 0; k < g->nb; k++) buf_printf(&b, "%02X", g->bytes[k]);
    buf_adds(&b, "> Tj\n");
  }
  buf_printf(&b, "%d Tr ", first->tmode);
  put_fill(&b, first);
  {
    /* (the fonts the walk loaded are gone: the run's is loaded again from the page's resources) */
    PObj *res = pdf_page_attr(d, pc_page(d, page), "Resources"), *fd = NULL;
    PObj *fonts = res ? pdf_dget(d, pdf_resolve(d, res), "Font") : NULL;
    if (fonts && fonts->type == PO_DICT && first->fname[0]) fd = pdf_dget(d, fonts, first->fname);
    if (fd && !first->in_form && (rf = font_load(d, fd)) && encode_in(rf, first->nb, u, nu, &codes, &neww)) how = TE_SAME_FONT;
    if (rf) font_free(rf);
  }
  if (how == TE_SAME_FONT) {
    buf_printf(&b, "/%s 1 Tf ", first->fname);
    put_tm(&b, first->trm, 0);
    buf_addc(&b, '<');
    for (i = 0; i < (int)codes.len; i++) buf_printf(&b, "%02X", (u8)codes.data[i]);
    buf_adds(&b, "> Tj\n");
    neww *= first->trm[0];
  } else {
    static const char *const fonts[4] = { "Helvetica", "Helvetica-Bold", "Times-Roman", "Courier" };
    int fk = first->fixed ? 3 : first->serif ? 2 : first->bold ? 1 : 0;
    char *lat = (char *)malloc((size_t)nu + 1), nm[40];
    PObj *res = pc_resources(d, pc_page(d, page));
    int k;
    how = TE_STD_FONT;
    for (k = 0; lat && k < nu; k++) lat[k] = u[k] < 256 ? (char)u[k] : '?';
    if (!lat || !res || !pc_add_resource(d, res, "Font", "FoEd", om_std_font(fonts[fk]), nm, sizeof nm)) { free(lat); how = TE_NONE; goto out; }
    buf_printf(&b, "/%s 1 Tf ", nm);
    put_tm(&b, first->trm, 0);
    om_str(&b, lat, nu); buf_adds(&b, " Tj\n");
    neww = om_text_width(lat, nu, 1, fk) * first->trm[0];
    free(lat);
  }
  /* the rest of the line, moved by the difference */
  dx = neww - oldw;
  for (i = 0; i < nt; i++) {
    Glyph *g = tail[i];
    int k;
    buf_printf(&b, "%d Tr ", g->tmode);
    put_fill(&b, g);
    buf_printf(&b, "/%s 1 Tf ", g->fname);
    put_tm(&b, g->trm, dx);
    buf_addc(&b, '<');
    for (k = 0; k < g->nb; k++) buf_printf(&b, "%02X", g->bytes[k]);
    buf_adds(&b, "> Tj\n");
  }
  buf_adds(&b, "ET\n");
  if (b.oom) { how = TE_NONE; goto out; }
  /* the run and the line's rest taken out, the new text drawn */
  l.pass = 2; l.at = 0;
  ce_rewrite_page(d, page, &ops);
  if (!pc_add_content(d, page, b.data, b.len, 0, NULL)) how = TE_NONE;
  d->changed = 1;
out:
  buf_free(&b); buf_free(&codes);
  free(u); free(tail); free(head); free(l.g);
  return how;
}

/* ------------------------------------------------------------------ pictures */
typedef struct { PageImage *im; int n, cap; int target, pass, at, act, newnum; PDoc *d; } IList;
static int i_walk(void *ud, PObj *img, int inline_img, const double *m, int *newnum)
{
  IList *l = (IList *)ud;
  if (l->pass == 1) {
    PageImage *p;
    if (l->n >= l->cap) {
      int nc = l->cap ? l->cap * 2 : 16;
      PageImage *t = (PageImage *)realloc(l->im, sizeof(PageImage) * (size_t)nc);
      if (!t) return 0;
      l->im = t; l->cap = nc;
    }
    p = &l->im[l->n++];
    memset(p, 0, sizeof *p);
    memcpy(p->m, m, sizeof p->m);
    p->inline_img = inline_img;
    p->num = inline_img ? 0 : img->onum;
    p->w = (int)pdf_dint(l->d, img, "Width", 0);
    p->h = (int)pdf_dint(l->d, img, "Height", 0);
    return 0;
  }
  if (l->at++ != l->target) return 0;
  if (l->act == 2) { *newnum = l->newnum; return 2; }
  return 1;
}
static CEOps *i_ops;
static int i_walk_named(void *ud, PObj *img, int inline_img, const double *m, int *newnum)
{
  IList *l = (IList *)ud;
  int r = i_walk(ud, img, inline_img, m, newnum);
  if (l->pass == 1 && !inline_img && i_ops) snprintf(l->im[l->n - 1].name, sizeof l->im[0].name, "%s", i_ops->xname);
  return r;
}
int page_images(PDoc *d, int page, PageImage **out)
{
  IList l;
  CEOps ops;
  memset(&l, 0, sizeof l); memset(&ops, 0, sizeof ops);
  ops.ud = &l; ops.image = i_walk_named; l.pass = 1; l.d = d;
  i_ops = &ops;
  ce_rewrite_page(d, page, &ops);
  i_ops = NULL;
  *out = l.im;
  return l.n;
}
int page_image_at(PDoc *d, int page, double x, double y)
{
  PageImage *im;
  int n = page_images(d, page, &im), i, hit = -1;
  for (i = 0; i < n; i++) {
    /* the point taken back into the image's unit square */
    double *m = im[i].m, det = m[0] * m[3] - m[1] * m[2], u, v;
    if (fabs(det) < 1e-12) continue;
    u = ((x - m[4]) * m[3] - (y - m[5]) * m[2]) / det;
    v = ((y - m[5]) * m[0] - (x - m[4]) * m[1]) / det;
    if (u >= 0 && u <= 1 && v >= 0 && v <= 1) hit = i;
  }
  free(im);
  return hit;
}
static int image_act(PDoc *d, int page, int index, int act, int newnum)
{
  IList l;
  CEOps ops;
  int r;
  memset(&l, 0, sizeof l); memset(&ops, 0, sizeof ops);
  ops.ud = &l; ops.image = i_walk; l.pass = 2; l.d = d; l.target = index; l.act = act; l.newnum = newnum;
  r = ce_rewrite_page(d, page, &ops);
  if (r) d->changed = 1;
  return r;
}
int image_replace(PDoc *d, int page, int index, const u8 *file, size_t n)
{
  int w, h, num = image_xobject(d, file, n, &w, &h);
  return num && image_act(d, page, index, 2, num);
}
int image_delete(PDoc *d, int page, int index) { return image_act(d, page, index, 1, 0); }
int image_replace_num(PDoc *d, int page, int index, int num) { return num && image_act(d, page, index, 2, num); }
int image_set_matrix(PDoc *d, int page, int index, const double *m)
{
  PageImage *im;
  int n = page_images(d, page, &im), ok = 0;
  if (index >= 0 && index < n && !im[index].inline_img && im[index].num) {
    PObj *res = pc_resources(d, pc_page(d, page));
    char nm[40];
    if (res && pc_add_resource(d, res, "XObject", "FoIm", om_ref(im[index].num), nm, sizeof nm) && image_act(d, page, index, 1, 0)) {
      Buf b;
      int i;
      buf_init(&b);
      buf_adds(&b, "q ");
      for (i = 0; i < 6; i++) om_num(&b, m[i]);
      buf_printf(&b, "cm /%s Do Q\n", nm);
      ok = !b.oom && pc_add_content(d, page, b.data, b.len, 0, NULL);
      buf_free(&b);
    }
  }
  free(im);
  return ok;
}

/* ------------------------------------------------------------------ snapping */
typedef struct { double *p; int n, cap; double cx, cy; } SList;
static void s_add(SList *l, const CEState *s, double x, double y)
{
  double px = s->ctm[0] * x + s->ctm[2] * y + s->ctm[4], py = s->ctm[1] * x + s->ctm[3] * y + s->ctm[5];
  if (l->n >= 40000) return;
  if (l->n * 2 + 2 > l->cap) {
    int nc = l->cap ? l->cap * 2 : 512;
    double *t = (double *)realloc(l->p, sizeof(double) * (size_t)nc);
    if (!t) return;
    l->p = t; l->cap = nc;
  }
  l->p[l->n * 2] = px; l->p[l->n * 2 + 1] = py; l->n++;
}
static int s_op(void *ud, char *kw, PObj **args, int *nargs, const CEState *s)
{
  SList *l = (SList *)ud;
  int n = *nargs;
  if ((!strcmp(kw, "m") || !strcmp(kw, "l")) && n >= 2) { l->cx = pobj_num(args[n - 2]); l->cy = pobj_num(args[n - 1]); s_add(l, s, l->cx, l->cy); }
  else if (!strcmp(kw, "c") && n >= 6) { l->cx = pobj_num(args[n - 2]); l->cy = pobj_num(args[n - 1]); s_add(l, s, l->cx, l->cy); }
  else if ((!strcmp(kw, "v") || !strcmp(kw, "y")) && n >= 4) { l->cx = pobj_num(args[n - 2]); l->cy = pobj_num(args[n - 1]); s_add(l, s, l->cx, l->cy); }
  else if (!strcmp(kw, "re") && n >= 4) {
    double x = pobj_num(args[n - 4]), y = pobj_num(args[n - 3]), w = pobj_num(args[n - 2]), h = pobj_num(args[n - 1]);
    s_add(l, s, x, y); s_add(l, s, x + w, y); s_add(l, s, x, y + h); s_add(l, s, x + w, y + h);
    s_add(l, s, x + w / 2, y); s_add(l, s, x + w / 2, y + h); s_add(l, s, x, y + h / 2); s_add(l, s, x + w, y + h / 2);
  }
  return 0;
}
int page_snap_points(PDoc *d, int page, double **pts)
{
  SList l;
  CEOps ops;
  memset(&l, 0, sizeof l); memset(&ops, 0, sizeof ops);
  ops.ud = &l; ops.op = s_op;
  ce_rewrite_page(d, page, &ops);
  *pts = l.p;
  return l.n;
}
int snap_nearest(const double *pts, int n, double x, double y, double dist, double *sx, double *sy)
{
  int i, best = -1;
  double bd = dist * dist;
  for (i = 0; i < n; i++) {
    double dx = pts[i * 2] - x, dy = pts[i * 2 + 1] - y, dd = dx * dx + dy * dy;
    if (dd <= bd) { bd = dd; best = i; }
  }
  if (best < 0) return 0;
  *sx = pts[best * 2]; *sy = pts[best * 2 + 1];
  return 1;
}
