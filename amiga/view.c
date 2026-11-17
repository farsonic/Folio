/*
 * view.c - the document as PDFCraft shows it: the pages one under another on a grey pasteboard,
 * zoomed, turned and scrolled, with the text selected, the match found and the comment chosen lit
 * up over them, and every tool's work with the mouse. Organizing, it shows the pages as a grid of
 * thumbnails to pick, turn, delete and drag into another order.
 *
 * How it draws: each page is rendered (core/render.c) into an RGB picture at the zoom, kept in a
 * small cache (the last few pages, up to a memory budget) and copied to the window with
 * WritePixelArray on a true-colour screen or dithered to pens on one with a palette (widgets.c).
 * A page not rendered yet shows white; a timer renders the visible ones one at a time while
 * nothing else is happening, and a click or key waiting stops a render half way (it starts again
 * later), so a slow page never holds up scrolling. Rubber bands and the ink being drawn go over
 * the pages in COMPLEMENT, so drawing them again takes them away.
 *
 * Where it draws: into an off-screen bitmap made like the window's (on a graphics card, in its
 * memory), then to the window in one blit, so a redraw never shows the pasteboard first and the
 * pages after. Scrolling moves that bitmap's picture with ScrollRaster (the blitter's work, the
 * Amiga's own or the card's) and draws only the strip that comes into view. Without memory for the
 * bitmap everything is drawn straight into the window, as before.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <graphics/regions.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

char view_uri[512];
char view_info[96];

#define GAP 8                                   /* round and between the pages */
#define CW (THUMB_W + 28)                       /* organizing: a page's cell */
#define CH (THUMB_H + 30)
enum { DR_NONE, DR_PAN, DR_TEXT, DR_RECT, DR_OVAL, DR_LINE, DR_BOX, DR_INK, DR_ORGMOVE, DR_CLICK };

struct Data {
  Object *obj, *vprop, *hprop;
  struct MUI_EventHandlerNode ehn;
  struct MUI_InputHandlerNode ihn;
  int handlers, shown;
  /* the layout: each page's place and size in pixels within the laid-out pages */
  DocView *lv; int lmode, n, cap; double lz; int lrot; LONG lw, lh;
  LONG *px, *py, *pw, *ph;
  LONG total_w, total_h;
  LONG ox, oy;                                  /* where the laid-out pages' top left is in the view */
  int cols;                                     /* organizing: cells across */
  /* the mouse */
  int drag, dpage;
  double ax, ay, bx, by;                        /* the drag's start and end (the page's default user space) */
  LONG mx0, my0, pmx, pmy;                      /* (window: where it began, where it was last) */
  ULONG secs, micros;
  double *ink; int nink, capink;
  int ov_on;
  int drop;                                     /* organizing: where a dragged page would go */
  int anchor;                                   /* text selection: the character it started at */
  int hot_link;
  int lgen;                                     /* counts the layouts made (a scroll can only be blitted within one) */
  /* the back buffer: a bitmap as big as the window up to the view's bottom right corner, so it is
     drawn at window coordinates; its layer clips to the view (base) or, while a scroll draws the
     strip that came into view, to that strip (cur) */
  struct BitMap *bm; struct Layer_Info *li; struct Layer *ly; struct Region *base, *cur;
  LONG bx0, by0, bx1, by1;                      /* the view box it was made for */
  int back_ok;                                  /* it holds what the window shows */
};

static struct MUI_CustomClass *vcls;
static const double zooms[] = { 0.1, 0.125, 0.25, 0.333333, 0.5, 0.666667, 0.75, 1, 1.25, 1.5, 2, 3, 4, 6, 8 };
#define NZOOMS (int)(sizeof zooms / sizeof zooms[0])

/* ------------------------------------------------------------------ the rendered pages */
typedef struct { DocView *v; PObj *pg; double z; int rot; Pixmap pm; int complete; ULONG stamp; long polls; } CEntry;   /* polls: how far an unfinished render got */
#define MAXCE 12
#define CE_BUDGET (7UL << 20)
static CEntry ce[MAXCE];
static ULONG ce_clock;
static void ce_drop(CEntry *e) { free(e->pm.rgb); memset(e, 0, sizeof *e); }
static CEntry *ce_find(DocView *v, int page, double z)
{
  int i;
  PObj *pg = page >= 0 && page < v->pdf->npages ? v->pdf->pages[page] : NULL;
  if (!pg) return NULL;
  for (i = 0; i < MAXCE; i++)
    if (ce[i].v == v && ce[i].pg == pg && ce[i].rot == v->rot && fabs(ce[i].z - z) < 1e-9) { ce[i].stamp = ++ce_clock; return &ce[i]; }
  return NULL;
}
/* room made for a picture of bytes (the least recently shown dropped) and a slot returned */
static CEntry *ce_slot(unsigned long bytes)
{
  for (;;) {
    unsigned long used = 0;
    int i, old = -1, freeslot = -1;
    for (i = 0; i < MAXCE; i++) {
      if (!ce[i].v) { if (freeslot < 0) freeslot = i; continue; }
      used += (unsigned long)ce[i].pm.w * ce[i].pm.h * 3;
      if (old < 0 || ce[i].stamp < ce[old].stamp) old = i;
    }
    if (freeslot >= 0 && (used + bytes <= CE_BUDGET || old < 0)) return &ce[freeslot];
    if (old < 0) return NULL;
    ce_drop(&ce[old]);
  }
}
static void ce_forget(DocView *v, PObj *pg)
{
  int i;
  for (i = 0; i < MAXCE; i++) if (ce[i].v == v && (!pg || ce[i].pg == pg)) ce_drop(&ce[i]);
}

/* a click, key or menu waiting (or the mouse moving with a button held): a render stops for it */
static struct Window *abort_win;
static int input_waiting(void *ud)
{
  struct Node *n;
  int hit = 0;
  (void)ud;
  if (!abort_win || !abort_win->UserPort) return 0;
  Forbid();
  for (n = abort_win->UserPort->mp_MsgList.lh_Head; n->ln_Succ; n = n->ln_Succ) {
    struct IntuiMessage *im = (struct IntuiMessage *)n;
    if (im->Class & (IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_MENUPICK | IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW | IDCMP_MENUVERIFY)) { hit = 1; break; }
    if (im->Class == IDCMP_MOUSEMOVE && (im->Qualifier & (IEQUALIFIER_LEFTBUTTON | IEQUALIFIER_RBUTTON))) { hit = 1; break; }
  }
  Permit();
  return hit;
}

/* (a render's own poll: counted, as the renderer polls every 256 drawing steps, so the count says
   how far it got) */
static long render_polls;
static int render_poll(void *ud) { render_polls++; return input_waiting(ud); }

/* (0.1.1) a page changed (see MUIM_View_PageEdited): each picture of it kept but marked to be drawn
   again (an unfinished new drawing doesn't replace it: polls as far as can be); added, the picture
   at the zoom on screen has the new annotation, the last on the page, drawn over it at once, which
   is quick and comes out as drawing the whole page again would (tests/annotdrawtest.c) */
static void ce_page_edited(DocView *v, PObj *pg, int added)
{
  int i, page;
  for (page = 0; page < v->pdf->npages && v->pdf->pages[page] != pg; page++) ;
  if (page >= v->pdf->npages) { ce_forget(v, pg); return; }
  for (i = 0; i < MAXCE; i++) {
    CEntry *e = &ce[i];
    int w, h, n;
    if (e->v != v || e->pg != pg) continue;
    pdf_page_pixels(v->pdf, page, e->z, e->rot * 90, &w, &h);
    n = annot_count(v->pdf, page);
    if (added && e->complete && e->pm.rgb && e->pm.w == w && e->pm.h == h && n > 0 && e->rot == v->rot && fabs(e->z - v->zoom) < 1e-9) {
      RenderOpts o;
      Pixmap pm;
      memset(&o, 0, sizeof o);
      memset(&pm, 0, sizeof pm);
      o.scale = e->z; o.rotate = e->rot * 90; o.annots = 1; o.paper = 0xFFFFFF;
      o.onto = e->pm.rgb; o.annots_only = 1; o.annot_first = n - 1;
      if (pdf_render(v->pdf, v->rc, page, &o, &pm) && pm.rgb && pm.w == w && pm.h == h) {
        free(e->pm.rgb);
        e->pm = pm;
        continue;
      }
      free(pm.rgb);
    }
    e->complete = 0;
    e->polls = 0x7FFFFFFF;
  }
}

/* ------------------------------------------------------------------ the layout */
static int ensure_cap(struct Data *d, int n)
{
  if (n <= d->cap) return 1;
  free(d->px); free(d->py); free(d->pw); free(d->ph);
  d->px = (LONG *)malloc((size_t)n * sizeof(LONG)); d->py = (LONG *)malloc((size_t)n * sizeof(LONG));
  d->pw = (LONG *)malloc((size_t)n * sizeof(LONG)); d->ph = (LONG *)malloc((size_t)n * sizeof(LONG));
  if (!d->px || !d->py || !d->pw || !d->ph) { free(d->px); free(d->py); free(d->pw); free(d->ph); d->px = d->py = d->pw = d->ph = NULL; d->cap = 0; return 0; }
  d->cap = n;
  return 1;
}
/* the zoom a fit mode wants for the view's size */
static double fit_zoom(DocView *v, LONG W, LONG H)
{
  int i, w, h;
  double best = 0;
  if (v->fit == FIT_WIDTH) {
    int maxw = 1;
    for (i = 0; i < v->pdf->npages && i < 400; i++) { pdf_page_pixels(v->pdf, i, 1, v->rot * 90, &w, &h); if (w > maxw) maxw = w; }
    best = (double)(W - 2 * GAP) / maxw;
  } else if (v->fit == FIT_PAGE) {
    pdf_page_pixels(v->pdf, v->page, 1, v->rot * 90, &w, &h);
    best = (double)(W - 2 * GAP) / (w > 0 ? w : 1);
    if ((double)(H - 2 * GAP) / (h > 0 ? h : 1) < best) best = (double)(H - 2 * GAP) / (h > 0 ? h : 1);
  }
  if (best < zooms[0]) best = zooms[0];
  if (best > zooms[NZOOMS - 1]) best = zooms[NZOOMS - 1];
  return best;
}
static void layout(struct Data *d, Object *obj)
{
  DocView *v = cur_view();
  LONG W = _mwidth(obj), H = _mheight(obj);
  int i, n;
  if (!v) { d->lv = NULL; d->n = 0; return; }
  if (!v->placed && W > 40 && H > 40) { v->fit = FIT_WIDTH; v->placed = 1; }
  if (v->fit != FIT_NONE && v->mode == VM_READ) v->zoom = fit_zoom(v, W, H);
  n = v->pdf->npages;
  if (d->lv == v && d->lmode == v->mode && d->n == n && d->lz == v->zoom && d->lrot == v->rot && d->lw == W && d->lh == H) return;
  if (!ensure_cap(d, n > 0 ? n : 1)) { d->n = 0; return; }
  d->lgen++;
  d->lv = v; d->lmode = v->mode; d->n = n; d->lz = v->zoom; d->lrot = v->rot; d->lw = W; d->lh = H;
  if (v->mode == VM_ORGANIZE) {
    d->cols = (int)((W - GAP) / CW);
    if (d->cols < 1) d->cols = 1;
    for (i = 0; i < n; i++) {
      int w, h;
      double s;
      pdf_page_pixels(v->pdf, i, 1, 0, &w, &h);
      s = (double)THUMB_W / (w > 0 ? w : 1);
      if ((double)THUMB_H / (h > 0 ? h : 1) < s) s = (double)THUMB_H / (h > 0 ? h : 1);
      d->pw[i] = (LONG)(w * s + 0.5); d->ph[i] = (LONG)(h * s + 0.5);
      d->px[i] = GAP + (i % d->cols) * CW + (CW - d->pw[i]) / 2;
      d->py[i] = GAP + (i / d->cols) * CH + (THUMB_H - d->ph[i]) + 8;
    }
    d->total_w = GAP + d->cols * CW;
    d->total_h = GAP + ((n + d->cols - 1) / d->cols) * CH + GAP;
    return;
  }
  {
    LONG y = GAP, maxw = 0;
    for (i = 0; i < n; i++) {
      int w, h;
      pdf_page_pixels(v->pdf, i, v->zoom, v->rot * 90, &w, &h);
      d->pw[i] = w; d->ph[i] = h; d->py[i] = y;
      y += h + GAP;
      if (w > maxw) maxw = w;
    }
    for (i = 0; i < n; i++) d->px[i] = GAP + (maxw - d->pw[i]) / 2;
    d->total_w = maxw + 2 * GAP;
    d->total_h = y;
  }
}
/* the scroll kept within the pages, and the pages' origin in the view */
static void place(struct Data *d, Object *obj)
{
  DocView *v = cur_view();
  LONG W = _mwidth(obj), H = _mheight(obj);
  if (!v) return;
  if (d->total_w <= W) { v->sx = 0; d->ox = (W - d->total_w) / 2; }
  else { if (v->sx > d->total_w - W) v->sx = d->total_w - W; if (v->sx < 0) v->sx = 0; d->ox = -v->sx; }
  if (d->total_h <= H) { v->sy = 0; d->oy = v->mode == VM_READ ? (H - d->total_h) / 2 : 0; }
  else { if (v->sy > d->total_h - H) v->sy = d->total_h - H; if (v->sy < 0) v->sy = 0; d->oy = -v->sy; }
}
static void update_props(struct Data *d, Object *obj)
{
  DocView *v = cur_view();
  if (!v) {
    if (d->vprop) SetAttrs(d->vprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, 1, MUIA_Prop_Visible, 1, MUIA_Prop_First, 0, TAG_DONE);
    if (d->hprop) SetAttrs(d->hprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, 1, MUIA_Prop_Visible, 1, MUIA_Prop_First, 0, TAG_DONE);
    return;
  }
  if (d->vprop) SetAttrs(d->vprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, d->total_h, MUIA_Prop_Visible, _mheight(obj), MUIA_Prop_First, v->sy, TAG_DONE);
  if (d->hprop) SetAttrs(d->hprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, d->total_w, MUIA_Prop_Visible, _mwidth(obj), MUIA_Prop_First, v->sx, TAG_DONE);
}
/* a page's place in the window */
static LONG page_x(struct Data *d, Object *obj, int i) { return _mleft(obj) + d->ox + d->px[i]; }
static LONG page_y(struct Data *d, Object *obj, int i) { return _mtop(obj) + d->oy + d->py[i]; }
/* the page under a window point, -1 none (slop: pixels round it that still count) */
static int page_at(struct Data *d, Object *obj, LONG mx, LONG my, LONG slop)
{
  int i;
  for (i = 0; i < d->n; i++) {
    LONG x = page_x(d, obj, i), y = page_y(d, obj, i);
    if (mx >= x - slop && mx < x + d->pw[i] + slop && my >= y - slop && my < y + d->ph[i] + slop) return i;
  }
  return -1;
}
static Mat page_mat(DocView *v, int i) { return pdf_page_matrix(v->pdf, i, v->zoom, v->rot * 90); }
/* window point to the page's default user space and back */
static void to_page(struct Data *d, Object *obj, int i, LONG mx, LONG my, double *x, double *y)
{
  Mat m = page_mat(d->lv, i), inv;
  if (!mat_invert(m, &inv)) { *x = *y = 0; return; }
  mat_apply(&inv, mx - page_x(d, obj, i) + 0.5, my - page_y(d, obj, i) + 0.5, x, y);
}
static void to_win(struct Data *d, Object *obj, int i, double x, double y, LONG *mx, LONG *my)
{
  Mat m = page_mat(d->lv, i);
  double a, b;
  mat_apply(&m, x, y, &a, &b);
  *mx = page_x(d, obj, i) + (LONG)floor(a); *my = page_y(d, obj, i) + (LONG)floor(b);
}
/* a page-space rectangle as window pixels (sorted) */
static void rect_win(struct Data *d, Object *obj, int i, double x0, double y0, double x1, double y1, LONG *r)
{
  LONG a, b, c, e, t;
  to_win(d, obj, i, x0, y0, &a, &b);
  to_win(d, obj, i, x1, y1, &c, &e);
  if (a > c) { t = a; a = c; c = t; }
  if (b > e) { t = b; b = e; e = t; }
  r[0] = a; r[1] = b; r[2] = c; r[3] = e;
}

/* ------------------------------------------------------------------ the back buffer */
/* the view whose back buffer is being drawn into (NULL: drawing goes straight to the window) */
static struct Data *g_back;
#define RP(obj) (g_back ? g_back->ly->rp : _rp(obj))
/* the back buffer given back. WaitBlit first: the blitter works on its own and may still be
   copying from it */
static void back_free(struct Data *d)
{
  if (d->ly) { InstallClipRegion(d->ly, NULL); DeleteLayer(0, d->ly); d->ly = NULL; }
  if (d->li) { DisposeLayerInfo(d->li); d->li = NULL; }
  if (d->base) { DisposeRegion(d->base); d->base = NULL; }
  if (d->bm) { WaitBlit(); FreeBitMap(d->bm); d->bm = NULL; }
  d->cur = NULL;
  d->back_ok = 0;
}
/* the back buffer for the view as it is now, made (again) if the view moved or changed size; 0
   without memory (the caller draws into the window). It is a friend of the window's bitmap, so
   ClipBlit copies it fast and WritePixelArray writes it as it writes the window. It has a layer
   because graphics.library clips only through one: drawing past a bare bitmap's edge would write
   over memory. The layer starts at 0,0, so the window's coordinates work in it unchanged */
static int back_ready(struct Data *d, Object *obj)
{
  struct BitMap *wbm = _rp(obj)->BitMap;
  struct Rectangle r;
  if (d->ly && d->bx0 == _mleft(obj) && d->by0 == _mtop(obj) && d->bx1 == _mright(obj) && d->by1 == _mbottom(obj)) return 1;
  back_free(d);
  if (_mwidth(obj) <= 0 || _mheight(obj) <= 0) return 0;
  if (!(d->bm = AllocBitMap((ULONG)_mright(obj) + 1, (ULONG)_mbottom(obj) + 1, GetBitMapAttr(wbm, BMA_DEPTH), 0, wbm)) ||
      !(d->li = NewLayerInfo()) ||
      !(d->ly = CreateUpfrontLayer(d->li, d->bm, 0, 0, _mright(obj), _mbottom(obj), LAYERSIMPLE, NULL)) ||
      !(d->base = NewRegion())) { back_free(d); return 0; }
  r.MinX = (WORD)_mleft(obj); r.MinY = (WORD)_mtop(obj); r.MaxX = (WORD)_mright(obj); r.MaxY = (WORD)_mbottom(obj);
  if (!OrRectRegion(d->base, &r)) { back_free(d); return 0; }
  InstallClipRegion(d->ly, d->base);
  d->cur = d->base;
  d->bx0 = _mleft(obj); d->by0 = _mtop(obj); d->bx1 = _mright(obj); d->by1 = _mbottom(obj);
  return 1;
}
/* the part x0,y0-x1,y1 of the back buffer (window coordinates, kept to the view) copied to the
   window: one blit */
static void back_show(struct Data *d, Object *obj, LONG x0, LONG y0, LONG x1, LONG y1)
{
  if (x0 < _mleft(obj)) x0 = _mleft(obj);
  if (y0 < _mtop(obj)) y0 = _mtop(obj);
  if (x1 > _mright(obj)) x1 = _mright(obj);
  if (y1 > _mbottom(obj)) y1 = _mbottom(obj);
  if (x1 < x0 || y1 < y0) return;
  ClipBlit(d->ly->rp, x0, y0, _rp(obj), x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0xC0);
}
/* drawing clipped to a box as well: through MUI on the window, through the layer's region on the
   back buffer (the box within what is clipped to already); clip_pop puts it back */
static APTR clip_push(Object *obj, LONG x, LONG y, LONG w, LONG h)
{
  struct Region *r;
  struct Rectangle b;
  if (!g_back) return (APTR)MUI_AddClipping(muiRenderInfo(obj), x, y, w, h);
  if (!(r = NewRegion())) return NULL;
  b.MinX = (WORD)x; b.MinY = (WORD)y; b.MaxX = (WORD)(x + w - 1); b.MaxY = (WORD)(y + h - 1);
  OrRectRegion(r, &b);
  AndRegionRegion(g_back->cur, r);
  InstallClipRegion(g_back->ly, r);
  return (APTR)r;
}
static void clip_pop(Object *obj, APTR h)
{
  if (!g_back) { MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)h); return; }
  if (!h) return;
  InstallClipRegion(g_back->ly, g_back->cur);
  DisposeRegion((struct Region *)h);
}

/* ------------------------------------------------------------------ drawing */
static ULONG board(void) { return theme_is_dark() ? 0x303030 : 0xE4E4E4; }
static int clipr(Object *obj, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  if (*x0 < _mleft(obj)) *x0 = _mleft(obj);
  if (*y0 < _mtop(obj)) *y0 = _mtop(obj);
  if (*x1 > _mright(obj)) *x1 = _mright(obj);
  if (*y1 > _mbottom(obj)) *y1 = _mbottom(obj);
  return *x1 >= *x0 && *y1 >= *y0;
}
static void fill_board(Object *obj, LONG x0, LONG y0, LONG x1, LONG y1)
{
  if (!clipr(obj, &x0, &y0, &x1, &y1)) return;
  draw_rgb_box(RP(obj), _screen(obj), x0, y0, x1, y1, board());
}
/* COMPLEMENT boxes over the characters a..b of a page's text */
static void text_boxes(struct Data *d, Object *obj, int i, TextPage *t, int a, int b, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
  struct RastPort *rp = RP(obj);
  int k;
  if (!t || a < 0) return;
  if (b >= t->no) b = t->no - 1;
  SetDrMd(rp, COMPLEMENT);
  SetAPen(rp, 1);
  for (k = a; k <= b; k++) {
    TChar *c = &t->o[k];
    LONG r[4];
    if (c->u == 10) continue;
    rect_win(d, obj, i, c->x0, c->y0, c->x1, c->y1, r);
    if (r[2] > r[0]) r[2]--;
    if (r[0] < cx0) r[0] = cx0;
    if (r[1] < cy0) r[1] = cy0;
    if (r[2] > cx1) r[2] = cx1;
    if (r[3] > cy1) r[3] = cy1;
    if (r[2] >= r[0] && r[3] >= r[1]) RectFill(rp, r[0], r[1], r[2], r[3]);
  }
  SetDrMd(rp, JAM1);
}
/* what is lit over a page: the selection, the match, the chosen comment (inside the clip box) */
static void overlays(struct Data *d, Object *obj, int i, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
  DocView *v = d->lv;
  struct RastPort *rp = RP(obj);
  if (v->mode != VM_READ) return;
  if (v->sel_page == i && v->sel_a >= 0) text_boxes(d, obj, i, page_text(v, i), v->sel_a, v->sel_b, cx0, cy0, cx1, cy1);
  if (v->find_page == i && v->find_a >= 0) text_boxes(d, obj, i, page_text(v, i), v->find_a, v->find_b, cx0, cy0, cx1, cy1);
  if (v->an_page == i && v->an_index >= 0) {
    PAnnot *an;
    int n = pdf_annots(v->pdf, i, &an), k;
    for (k = 0; k < n; k++) if (an[k].index == v->an_index) {
      LONG r[4];
      APTR clip = clip_push(obj, cx0, cy0, cx1 - cx0 + 1, cy1 - cy0 + 1);
      rect_win(d, obj, i, an[k].rect[0], an[k].rect[1], an[k].rect[2], an[k].rect[3], r);
      SetDrMd(rp, COMPLEMENT); SetAPen(rp, 1);
      SetDrPt(rp, 0xCCCC);
      Move(rp, r[0] - 2, r[1] - 2); Draw(rp, r[2] + 2, r[1] - 2); Draw(rp, r[2] + 2, r[3] + 2); Draw(rp, r[0] - 2, r[3] + 2); Draw(rp, r[0] - 2, r[1] - 1);
      SetDrPt(rp, 0xFFFF);
      SetDrMd(rp, JAM1);
      clip_pop(obj, clip);
    }
    pdf_annots_free(an, n);
  }
}
/* one page (in read mode) drawn within the window box cx0..cx1, cy0..cy1 */
static void draw_page(struct Data *d, Object *obj, int i, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
  DocView *v = d->lv;
  LONG x = page_x(d, obj, i), y = page_y(d, obj, i), x0 = x, y0 = y, x1 = x + d->pw[i] - 1, y1 = y + d->ph[i] - 1;
  CEntry *e = ce_find(v, i, v->zoom);
  if (x0 < cx0) x0 = cx0;
  if (y0 < cy0) y0 = cy0;
  if (x1 > cx1) x1 = cx1;
  if (y1 > cy1) y1 = cy1;
  if (!clipr(obj, &x0, &y0, &x1, &y1)) return;
  if (e && e->pm.rgb && e->pm.w == d->pw[i] && e->pm.h == d->ph[i])
    draw_rgb(RP(obj), _screen(obj), e->pm.rgb + ((y0 - y) * (long)e->pm.w + (x0 - x)) * 3, e->pm.w * 3, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  else draw_rgb_box(RP(obj), _screen(obj), x0, y0, x1, y1, 0xFFFFFF);
  overlays(d, obj, i, x0, y0, x1, y1);
}
static ULONG ce_shadow(void) { return theme_is_dark() ? 0x1C1C1C : 0xB8B8B8; }
/* the read mode's view within a window box: the pasteboard round the pages, the pages, their shadows */
static void draw_read(struct Data *d, Object *obj, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
  LONG cur = cy0;
  int i;
  for (i = 0; i < d->n; i++) {
    LONG x = page_x(d, obj, i), y = page_y(d, obj, i), w = d->pw[i], h = d->ph[i];
    if (y + h + 1 < cy0) continue;
    if (y > cy1) break;
    if (y > cur) fill_board(obj, cx0, cur, cx1, y - 1);
    fill_board(obj, cx0, y, x - 1, y + h);
    fill_board(obj, x + w + 1, y, cx1, y + h);
    /* the shadow: right and below */
    { LONG a = x + w, b = y + 2, c = x + w, e = y + h; if (clipr(obj, &a, &b, &c, &e) && a >= cx0 && a <= cx1) draw_rgb_box(RP(obj), _screen(obj), a, b > cy0 ? b : cy0, c, e < cy1 ? e : cy1, ce_shadow()); }
    { LONG a = x + 2, b = y + h, c = x + w, e = y + h; if (b >= cy0 && b <= cy1) { if (a < cx0) a = cx0; if (c > cx1) c = cx1; if (clipr(obj, &a, &b, &c, &e)) draw_rgb_box(RP(obj), _screen(obj), a, b, c, e, ce_shadow()); } }
    fill_board(obj, x, y + h, x + 1, y + h);
    fill_board(obj, x + w, y, x + w, y + 1);
    draw_page(d, obj, i, cx0, cy0, cx1, cy1);
    cur = y + h + 1;
  }
  if (cur <= cy1) fill_board(obj, cx0, cur, cx1, cy1);
}
/* organizing: each page's cell, its thumbnail and number, the picked ones lit */
static void draw_cell(struct Data *d, Object *obj, int i)
{
  DocView *v = d->lv;
  struct RastPort *rp = RP(obj);
  LONG cx = _mleft(obj) + d->ox + GAP + (i % d->cols) * CW, cy = _mtop(obj) + d->oy + GAP + (i / d->cols) * CH;
  LONG x0 = cx + 2, y0 = cy + 2, x1 = cx + CW - 3, y1 = cy + CH - 3;
  LONG tx = page_x(d, obj, i), ty = page_y(d, obj, i);
  Thumb *t = thumb_get(v, i, 0);
  char num[16];
  APTR clip;
  int picked = v->picked && v->picked[i];
  if (y1 < _mtop(obj) || y0 > _mbottom(obj)) return;
  clip = clip_push(obj, _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  draw_rgb_box(rp, _screen(obj), cx, cy, cx + CW - 1, cy + CH - 1, board());
  if (picked) { theme_fill(rp, x0, y0, x1, y1, tpen(T_SELECTION)); theme_rect(rp, x0, y0, x1, y1, tpen(T_ACCENT)); theme_rect(rp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, tpen(T_ACCENT)); }
  if (t && t->pm.rgb) {
    LONG w = t->pm.w, h = t->pm.h, dx = cx + (CW - w) / 2, dy = ty + d->ph[i] - h;
    LONG a = dx, b = dy, c = dx + w - 1, e = dy + h - 1;
    if (clipr(obj, &a, &b, &c, &e)) draw_rgb(rp, _screen(obj), t->pm.rgb + ((b - dy) * (long)w + (a - dx)) * 3, w * 3, a, b, c - a + 1, e - b + 1);
    theme_rect(rp, dx - 1, dy - 1, dx + w, dy + h, tpen(T_BORDER));
  } else {
    draw_rgb_box(rp, _screen(obj), tx, ty, tx + d->pw[i] - 1, ty + d->ph[i] - 1, 0xFFFFFF);
    theme_rect(rp, tx - 1, ty - 1, tx + d->pw[i], ty + d->ph[i], tpen(T_BORDER));
  }
  sprintf(num, "%d", i + 1);
  theme_text(rp, picked ? theme_bold() : theme_font(), cx + (CW - theme_textlen(rp, theme_font(), num)) / 2, cy + CH - theme_font()->tf_YSize - 5, num, tpen(T_TEXT));
  clip_pop(obj, clip);
}
static void draw_org(struct Data *d, Object *obj)
{
  int i;
  LONG right = _mleft(obj) + d->ox + GAP + d->cols * CW, bottom = _mtop(obj) + d->oy + GAP + ((d->n + d->cols - 1) / d->cols) * CH;
  fill_board(obj, _mleft(obj), _mtop(obj), _mright(obj), _mtop(obj) + d->oy + GAP - 1);
  fill_board(obj, _mleft(obj), _mtop(obj), _mleft(obj) + d->ox + GAP - 1, _mbottom(obj));
  fill_board(obj, right, _mtop(obj), _mright(obj), _mbottom(obj));
  fill_board(obj, _mleft(obj), bottom, _mright(obj), _mbottom(obj));
  for (i = 0; i < d->n; i++) draw_cell(d, obj, i);
  /* empty cells after the last page */
  for (i = d->n; i % d->cols; i++) {
    LONG cx = _mleft(obj) + d->ox + GAP + (i % d->cols) * CW, cy = _mtop(obj) + d->oy + GAP + (i / d->cols) * CH;
    fill_board(obj, cx, cy, cx + CW - 1, cy + CH - 1);
  }
}
/* the overlay (COMPLEMENT): rubber bands, ink, the drop marker */
static void ov_draw(struct Data *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  APTR clip;
  LONG a, b, c, e;
  int i;
  if (!d->shown || !d->lv || d->drag == DR_NONE) return;
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  SetDrMd(rp, COMPLEMENT); SetAPen(rp, 1); SetDrPt(rp, 0xFFFF);
  switch (d->drag) {
  case DR_RECT: case DR_BOX: case DR_OVAL: {
    LONG r[4];
    rect_win(d, obj, d->dpage, d->ax, d->ay, d->bx, d->by, r);
    if (d->drag == DR_OVAL && r[2] - r[0] > 2 && r[3] - r[1] > 2) DrawEllipse(rp, (r[0] + r[2]) / 2, (r[1] + r[3]) / 2, (r[2] - r[0]) / 2, (r[3] - r[1]) / 2);
    else {
      if (d->drag == DR_BOX) SetDrPt(rp, 0xF0F0);
      Move(rp, r[0], r[1]); Draw(rp, r[2], r[1]);
      if (r[3] > r[1]) { Move(rp, r[2], r[1] + 1); Draw(rp, r[2], r[3]); if (r[2] > r[0]) { Move(rp, r[2] - 1, r[3]); Draw(rp, r[0], r[3]); } if (r[3] > r[1] + 1) { Move(rp, r[0], r[3] - 1); Draw(rp, r[0], r[1] + 1); } }
      SetDrPt(rp, 0xFFFF);
    }
    break; }
  case DR_LINE:
    to_win(d, obj, d->dpage, d->ax, d->ay, &a, &b); to_win(d, obj, d->dpage, d->bx, d->by, &c, &e);
    Move(rp, a, b); Draw(rp, c, e);
    break;
  case DR_INK:
    for (i = 1; i < d->nink; i++) {
      to_win(d, obj, d->dpage, d->ink[i * 2 - 2], d->ink[i * 2 - 1], &a, &b); to_win(d, obj, d->dpage, d->ink[i * 2], d->ink[i * 2 + 1], &c, &e);
      Move(rp, a, b); Draw(rp, c, e);
    }
    break;
  case DR_ORGMOVE:
    if (d->drop >= 0) {
      LONG cx = _mleft(obj) + d->ox + GAP + (d->drop % d->cols) * CW, cy = _mtop(obj) + d->oy + GAP + (d->drop / d->cols) * CH;
      RectFill(rp, cx, cy + 4, cx + 2, cy + CH - 6);
    }
    break;
  }
  SetDrMd(rp, JAM1);
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
static void ov_hide(struct Data *d, Object *obj) { if (d->ov_on) { ov_draw(d, obj); d->ov_on = 0; } }
static void ov_show(struct Data *d, Object *obj) { if (!d->ov_on) { ov_draw(d, obj); d->ov_on = 1; } }
/* the page in view: the one crossing a line a third of the way down */
static void track_page(struct Data *d, Object *obj)
{
  DocView *v = d->lv;
  LONG line = _mtop(obj) + _mheight(obj) / 3;
  int i, p = v->page;
  if (v->mode != VM_READ || !d->n) return;
  for (i = 0; i < d->n; i++) if (page_y(d, obj, i) + d->ph[i] + GAP > line) { p = i; break; }
  if (p != v->page) { v->page = p; SetAttrs(obj, MUIA_View_Page, TRUE, TAG_DONE); }
}
static void draw_all(struct Data *d, Object *obj)
{
  if (!d->shown) return;
  if (!cur_view()) {
    d->lv = NULL;
    theme_fill(_rp(obj), _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_CANVAS));
    update_props(d, obj);
    return;
  }
  layout(d, obj);
  place(d, obj);
  d->ov_on = 0;
  /* into the back buffer and then to the window in one blit, or (no memory) straight to the window */
  if (back_ready(d, obj)) g_back = d;
  if (d->lv->mode == VM_ORGANIZE) draw_org(d, obj);
  else draw_read(d, obj, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
  if (g_back) {
    g_back = NULL;
    back_show(d, obj, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
    d->back_ok = 1;
  }
  ov_show(d, obj);
  update_props(d, obj);
  track_page(d, obj);
}
/* one page's area drawn again (its picture or its overlays changed) */
static void redraw_page(struct Data *d, Object *obj, int i)
{
  int back;
  if (!d->shown || !d->lv || i < 0 || i >= d->n) return;
  ov_hide(d, obj);
  /* (through the back buffer when it holds the view, so it stays the same as the window) */
  back = d->back_ok && back_ready(d, obj);
  if (back) g_back = d;
  if (d->lv->mode == VM_ORGANIZE) draw_cell(d, obj, i);
  else draw_page(d, obj, i, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
  if (back) {
    LONG x = page_x(d, obj, i), y = page_y(d, obj, i);
    g_back = NULL;
    if (d->lv->mode == VM_ORGANIZE) {
      x = _mleft(obj) + d->ox + GAP + (i % d->cols) * CW; y = _mtop(obj) + d->oy + GAP + (i / d->cols) * CH;
      back_show(d, obj, x, y, x + CW - 1, y + CH - 1);
    } else back_show(d, obj, x, y, x + d->pw[i] - 1, y + d->ph[i] - 1);
  }
  ov_show(d, obj);
}

/* ------------------------------------------------------------------ rendering while idle */
/* the next page that wants rendering: the one in view first, then the others visible; organizing,
   the next thumbnail */
static int tick(struct Data *d, Object *obj)
{
  DocView *v = d->lv;
  int i, best = -1;
  if (!v || !d->shown || d->drag != DR_NONE) return 0;
  abort_win = _window(obj);
  if (v->mode == VM_ORGANIZE) {
    for (i = 0; i < d->n; i++) {
      LONG y = page_y(d, obj, i);
      Thumb *t;
      if (y + d->ph[i] < _mtop(obj) || y - CH > _mbottom(obj)) continue;
      t = thumb_get(v, i, 0);
      if (t && t->pm.rgb) continue;
      if (input_waiting(NULL)) return 0;
      thumb_get(v, i, 1);
      redraw_page(d, obj, i);
      return 1;
    }
    return 0;
  }
  {
    int first = -1, last = -1;
    for (i = 0; i < d->n; i++) {
      LONG y = page_y(d, obj, i);
      CEntry *e;
      if (y + d->ph[i] < _mtop(obj) || y > _mbottom(obj)) continue;
      if (first < 0) first = i;
      last = i;
      e = ce_find(v, i, v->zoom);
      if (e && e->complete) continue;
      if (best < 0 || i == v->page) best = i;
    }
    /* the visible pages done: the next page down, then the one above, rendered ahead while nothing
       else is happening, so scrolling to them shows them at once rather than white (the cache keeps
       them if there is room beside the visible ones) */
    if (best < 0 && last >= 0) {
      int k, cand[2];
      cand[0] = last + 1; cand[1] = first - 1;
      unsigned long vis = 0;
      for (k = first; k <= last; k++) vis += (unsigned long)d->pw[k] * d->ph[k] * 3;
      for (k = 0; k < 2 && best < 0; k++) {
        CEntry *e;
        if (cand[k] < 0 || cand[k] >= d->n) continue;
        /* (only with room beside the visible pages: pushing one of them out would mean rendering it
           again, and then this one, for ever) */
        if (vis + (unsigned long)d->pw[cand[k]] * d->ph[cand[k]] * 3 > CE_BUDGET) continue;
        e = ce_find(v, cand[k], v->zoom);
        if (!e || !e->complete) best = cand[k];
      }
    }
  }
  if (best < 0) return 0;
  {
    RenderOpts o;
    Pixmap pm;
    CEntry *e;
    int ok;
    memset(&o, 0, sizeof o);
    memset(&pm, 0, sizeof pm);
    o.scale = v->zoom; o.rotate = v->rot * 90; o.annots = 1; o.paper = 0xFFFFFF;
    o.abort = render_poll;
    if (input_waiting(NULL)) return 0;
    LOG("render page %d at %ld%%", best + 1, (long)(v->zoom * 100));
    render_polls = 0;
    ok = pdf_render(v->pdf, v->rc, best, &o, &pm);
    LOG("  %s", ok ? "done" : "stopped");
    if (!pm.rgb) return 0;
    /* stopped short of where an earlier try got: that one stays on screen (showing less, then more
       again, is what made pages flicker while they were being rendered) */
    if (!ok && (e = ce_find(v, best, v->zoom)) && e->pm.rgb && e->polls >= render_polls) { free(pm.rgb); return 1; }
    if ((e = ce_find(v, best, v->zoom))) ce_drop(e);
    if (!(e = ce_slot((unsigned long)pm.w * pm.h * 3))) { free(pm.rgb); return 0; }
    e->v = v; e->pg = v->pdf->pages[best]; e->z = v->zoom; e->rot = v->rot; e->pm = pm; e->complete = ok; e->stamp = ++ce_clock; e->polls = render_polls;
    redraw_page(d, obj, best);
    return 1;
  }
}

/* ------------------------------------------------------------------ zoom and scroll */
static void zoom_to(struct Data *d, Object *obj, double z, int fit)
{
  DocView *v = cur_view();
  double rel = 0;
  int p;
  if (!v) return;
  layout(d, obj); place(d, obj);
  p = v->page;
  /* keep the same part of the page in view */
  if (d->n && v->mode == VM_READ && p < d->n && d->ph[p] > 0) rel = (double)(v->sy - d->py[p]) / d->ph[p];
  v->fit = fit;
  if (fit == FIT_NONE) {
    if (z < zooms[0]) z = zooms[0];
    if (z > zooms[NZOOMS - 1]) z = zooms[NZOOMS - 1];
    v->zoom = z;
  }
  layout(d, obj);
  if (v->mode == VM_READ && p < d->n) {
    v->sy = d->py[p] + (LONG)(rel * d->ph[p]);
    if (fit == FIT_PAGE) v->sy = d->py[p] - GAP;
  }
  draw_all(d, obj);
  SetAttrs(obj, MUIA_View_Zoomed, TRUE, TAG_DONE);
}
static double zoom_step(double z, int dir)
{
  int i;
  if (dir > 0) { for (i = 0; i < NZOOMS; i++) if (zooms[i] > z * 1.001) return zooms[i]; return zooms[NZOOMS - 1]; }
  for (i = NZOOMS - 1; i >= 0; i--) if (zooms[i] < z * 0.999) return zooms[i];
  return zooms[0];
}
/* (with DEBUG=: how many scrolls were blitted and how many drawn in full, for comparing) */
static long n_blit, n_full;
/* the view scrolled with the blitter: the back buffer's picture moved by what the pages moved
   (ScrollRaster: the Amiga's blitter, or the graphics card's under Picasso96 or CyberGraphX), only
   the strips that came into view drawn, clipped to them, and the buffer blitted to the window. 0 if
   it can't be (no buffer, another layout, organizing, a jump of most of the view): the caller then
   draws everything */
static int scroll_blit(struct Data *d, Object *obj, LONG oldox, LONG oldoy, int oldgen)
{
  LONG dx = d->ox - oldox, dy = d->oy - oldoy, l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj);
  struct Region *strips;
  struct Rectangle box[2];
  int nb = 0, k;
  if (!d->back_ok || !d->lv || d->lv->mode != VM_READ || d->lgen != oldgen || !back_ready(d, obj)) return 0;
  if (dx == 0 && dy == 0) return 1;
  if (dx >= _mwidth(obj) * 3 / 4 || -dx >= _mwidth(obj) * 3 / 4 || dy >= _mheight(obj) * 3 / 4 || -dy >= _mheight(obj) * 3 / 4) return 0;
  if (!(strips = NewRegion())) return 0;
  ov_hide(d, obj);
  /* ScrollRaster's dx, dy > 0 move the picture left and up: the pages moved by -dx, -dy */
  InstallClipRegion(d->ly, NULL);       /* (the whole layer is in view: nothing for the scroll to clip) */
  ScrollRaster(d->ly->rp, -dx, -dy, l, t, r, b);
  if (dy < 0) { box[nb].MinX = (WORD)l; box[nb].MaxX = (WORD)r; box[nb].MinY = (WORD)(b + dy + 1); box[nb].MaxY = (WORD)b; nb++; }
  if (dy > 0) { box[nb].MinX = (WORD)l; box[nb].MaxX = (WORD)r; box[nb].MinY = (WORD)t; box[nb].MaxY = (WORD)(t + dy - 1); nb++; }
  if (dx < 0) { box[nb].MinX = (WORD)(r + dx + 1); box[nb].MaxX = (WORD)r; box[nb].MinY = (WORD)t; box[nb].MaxY = (WORD)b; nb++; }
  if (dx > 0 && nb < 2) { box[nb].MinX = (WORD)l; box[nb].MaxX = (WORD)(l + dx - 1); box[nb].MinY = (WORD)t; box[nb].MaxY = (WORD)b; nb++; }
  for (k = 0; k < nb; k++) OrRectRegion(strips, &box[k]);
  AndRegionRegion(d->base, strips);
  d->cur = strips;
  InstallClipRegion(d->ly, strips);
  g_back = d;
  for (k = 0; k < nb; k++) draw_read(d, obj, box[k].MinX, box[k].MinY, box[k].MaxX, box[k].MaxY);
  g_back = NULL;
  d->cur = d->base;
  InstallClipRegion(d->ly, d->base);
  DisposeRegion(strips);
  back_show(d, obj, l, t, r, b);
  ov_show(d, obj);
  update_props(d, obj);
  track_page(d, obj);
  if (++n_blit % 50 == 0) LOG("scrolling: %ld blitted, %ld drawn in full", n_blit, n_full);
  return 1;
}
static void scroll_to(struct Data *d, Object *obj, LONG sx, LONG sy)
{
  DocView *v = cur_view();
  LONG oldox = d->ox, oldoy = d->oy;
  int oldgen = d->lgen;
  if (!v) return;
  if (sx == v->sx && sy == v->sy) return;
  v->sx = sx; v->sy = sy;
  if (d->shown && d->lv == v) {
    layout(d, obj);
    place(d, obj);
    if (scroll_blit(d, obj, oldox, oldoy, oldgen)) return;
  }
  n_full++;
  draw_all(d, obj);
}
static void go_page(struct Data *d, Object *obj, int p, double top)
{
  DocView *v = cur_view();
  if (!v || p < 0 || p >= v->pdf->npages) return;
  layout(d, obj);
  v->page = p;
  if (v->mode == VM_ORGANIZE) {
    v->sy = GAP + (p / d->cols) * CH - GAP;
  } else if (top >= 0) {
    Mat m = page_mat(v, p);
    double a, b;
    mat_apply(&m, 0, top, &a, &b);
    v->sy = d->py[p] + (LONG)b - GAP;
  } else v->sy = d->py[p] - GAP;
  if (v->fit == FIT_PAGE) { v->zoom = fit_zoom(v, _mwidth(obj), _mheight(obj)); layout(d, obj); v->sy = d->py[p] - GAP; }
  draw_all(d, obj);
  v->page = p;
  SetAttrs(obj, MUIA_View_Page, TRUE, TAG_DONE);
}

/* ------------------------------------------------------------------ the mouse */
static void tell(Object *obj, ULONG attr) { SetAttrs(obj, attr, TRUE, TAG_DONE); }
/* the link under a window point on page i: 1 if there is one (followed) */
static int follow_link(struct Data *d, Object *obj, int i, double x, double y, int go)
{
  PLink *l;
  int n = pdf_links(d->lv->pdf, i, &l), k, found = 0;
  for (k = n - 1; k >= 0 && !found; k--) {
    if (x < l[k].rect[0] || x > l[k].rect[2] || y < l[k].rect[1] || y > l[k].rect[3]) continue;
    found = 1;
    if (!go) {
      if (l[k].uri) snprintf(view_info, sizeof view_info, "%s", l[k].uri);
      else snprintf(view_info, sizeof view_info, tr("Go to page %d"), l[k].page + 1);
      break;
    }
    if (l[k].uri) { strncpy(view_uri, l[k].uri, sizeof view_uri - 1); view_uri[sizeof view_uri - 1] = 0; tell(obj, MUIA_View_Link); }
    else if (l[k].page >= 0) go_page(d, obj, l[k].page, l[k].top);
  }
  pdf_links_free(l, n);
  return found;
}
/* the selection's extent drawn again where it changed */
/* characters a..b of a page's text turned over (COMPLEMENT) on the window and, when it holds the
   view, the back buffer, so the two stay alike */
static void sel_flip(struct Data *d, Object *obj, int page, int a, int b)
{
  TextPage *t = page_text(d->lv, page);
  APTR clip;
  if (a > b || !t) return;
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  text_boxes(d, obj, page, t, a, b, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
  if (d->back_ok && back_ready(d, obj)) {
    g_back = d;
    text_boxes(d, obj, page, t, a, b, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
    g_back = NULL;
  }
}
static void sel_set(struct Data *d, Object *obj, int page, int a, int b)
{
  DocView *v = d->lv;
  int old = v->sel_page, oa = v->sel_a, ob = v->sel_b;
  if (a > b) { int t = a; a = b; b = t; }
  /* (0.1.1) the selection on the same page growing or shrinking as the mouse moves: only the
     characters that came in or went out turned over, instead of the whole page drawn again at
     every move */
  if (old == page && oa >= 0 && d->shown && d->lv && v->mode == VM_READ && !d->ov_on) {
    v->sel_a = a; v->sel_b = b;
    if (b < oa || a > ob) { sel_flip(d, obj, page, oa, ob); sel_flip(d, obj, page, a, b); }
    else {
      sel_flip(d, obj, page, a < oa ? a : oa, (a < oa ? oa : a) - 1);
      sel_flip(d, obj, page, (b < ob ? b : ob) + 1, b < ob ? ob : b);
    }
    return;
  }
  v->sel_page = page; v->sel_a = a; v->sel_b = b;
  if (old >= 0 && old != page) redraw_page(d, obj, old);
  redraw_page(d, obj, page);
}
static void sel_clear(struct Data *d, Object *obj)
{
  DocView *v = d->lv;
  int old = v->sel_page;
  if (old < 0) return;
  v->sel_page = -1; v->sel_a = v->sel_b = -1;
  redraw_page(d, obj, old);
}
static void choose_annot(struct Data *d, Object *obj, int page, int index)
{
  DocView *v = d->lv;
  int old = v->an_page;
  if (old == page && v->an_index == index) return;
  v->an_page = page; v->an_index = index;
  if (old >= 0) redraw_page(d, obj, old);
  if (page >= 0 && page != old) redraw_page(d, obj, page);
  tell(obj, MUIA_View_Picked);
}
static int is_markup_tool(int t) { return t == TL_HIGHLIGHT || t == TL_UNDERLINE || t == TL_STRIKE; }
static void press(struct Data *d, Object *obj, LONG mx, LONG my, UWORD qual, ULONG secs, ULONG micros)
{
  DocView *v = d->lv;
  int i, dbl = DoubleClick(d->secs, d->micros, secs, micros) && abs(mx - d->mx0) < 4 && abs(my - d->my0) < 4;
  double x, y;
  d->secs = secs; d->micros = micros;
  d->mx0 = d->pmx = mx; d->my0 = d->pmy = my;
  if (!v) return;
  if (v->mode == VM_ORGANIZE) {
    LONG lx = mx - _mleft(obj) - d->ox - GAP, ly = my - _mtop(obj) - d->oy - GAP;
    int col = lx >= 0 ? (int)(lx / CW) : -1, row = ly >= 0 ? (int)(ly / CH) : -1, k = row * d->cols + col;
    if (col < 0 || col >= d->cols || row < 0 || k >= d->n) {
      if (v->picked) { memset(v->picked, 0, (size_t)d->n); draw_org(d, obj); tell(obj, MUIA_View_OrgPick); }
      d->drag = DR_PAN;
      return;
    }
    if (dbl) {
      /* a double click opens the page to read */
      v->mode = VM_READ; v->page = k;
      d->drag = DR_NONE;
      DoMethod(obj, MUIM_View_Refresh);
      go_page(d, obj, k, -1);
      tell(obj, MUIA_View_OrgPick);
      return;
    }
    if (v->picked) {
      if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) v->picked[k] = !v->picked[k];
      else if (!v->picked[k]) { memset(v->picked, 0, (size_t)d->n); v->picked[k] = 1; }
      draw_org(d, obj);
      tell(obj, MUIA_View_OrgPick);
    }
    v->page = k;
    d->dpage = k; d->drop = -1;
    d->drag = DR_ORGMOVE;
    return;
  }
  if (tool == TL_HAND || (qual & IEQUALIFIER_MIDBUTTON)) { d->drag = DR_PAN; return; }
  i = page_at(d, obj, mx, my, tool == TL_SELECT ? 0 : 4);
  if (i < 0) { if (v->sel_page >= 0) sel_clear(d, obj); if (v->an_page >= 0) choose_annot(d, obj, -1, -1); d->drag = DR_PAN; return; }
  to_page(d, obj, i, mx, my, &x, &y);
  d->dpage = i; d->ax = d->bx = x; d->ay = d->by = y;
  switch (tool) {
  case TL_SELECT: {
    int an = annot_at(v->pdf, i, x, y), c;
    TextPage *t;
    if (an >= 0) {
      choose_annot(d, obj, i, an);
      if (v->sel_page >= 0) sel_clear(d, obj);
      if (dbl) tell(obj, MUIA_View_EditNote);
      d->drag = DR_CLICK;
      return;
    }
    if (v->an_page >= 0) choose_annot(d, obj, -1, -1);
    t = page_text(v, i);
    c = t ? textpage_hit(t, x, y, 2) : -1;
    if (c >= 0 && dbl) {
      /* a double click picks the word */
      int a = c, b = c;
      while (a > 0 && t->o[a - 1].u > 32) a--;
      while (b + 1 < t->no && t->o[b + 1].u > 32) b++;
      sel_set(d, obj, i, a, b);
      tell(obj, MUIA_View_Picked);
      d->drag = DR_NONE;
      return;
    }
    if (c >= 0) {
      d->anchor = c;
      sel_set(d, obj, i, c, c);
      v->sel_a = -1;                            /* (nothing until the pointer moves) */
      redraw_page(d, obj, i);
      d->drag = DR_TEXT;
      return;
    }
    if (v->sel_page >= 0) sel_clear(d, obj);
    d->drag = DR_CLICK;
    return; }
  case TL_HIGHLIGHT: case TL_UNDERLINE: case TL_STRIKE: {
    TextPage *t = page_text(v, i);
    int c = t ? textpage_hit(t, x, y, 6) : -1;
    if (c < 0) { d->drag = DR_PAN; return; }
    d->anchor = c;
    sel_set(d, obj, i, c, c);
    d->drag = DR_TEXT;
    return; }
  case TL_NOTE: d->drag = DR_CLICK; return;
  case TL_TEXTBOX: d->drag = DR_BOX; break;
  case TL_RECT: d->drag = DR_RECT; break;
  case TL_OVAL: d->drag = DR_OVAL; break;
  case TL_LINE: d->drag = DR_LINE; break;
  case TL_INK:
    d->nink = 0;
    if (d->capink < 64) { free(d->ink); d->ink = (double *)malloc(64 * 2 * sizeof(double)); d->capink = d->ink ? 64 : 0; }
    if (d->ink) { d->ink[0] = x; d->ink[1] = y; d->nink = 1; }
    d->drag = DR_INK;
    break;
  default: d->drag = DR_PAN; return;
  }
  ov_show(d, obj);
}
static void motion(struct Data *d, Object *obj, LONG mx, LONG my)
{
  DocView *v = d->lv;
  double x, y;
  if (!v) return;
  switch (d->drag) {
  case DR_PAN: {
    LONG dx = mx - d->pmx, dy = my - d->pmy;
    d->pmx = mx; d->pmy = my;
    if (dx || dy) scroll_to(d, obj, v->sx - dx, v->sy - dy);
    return; }
  case DR_TEXT: {
    TextPage *t = page_text(v, d->dpage);
    int c;
    if (!t) return;
    to_page(d, obj, d->dpage, mx, my, &x, &y);
    c = textpage_hit(t, x, y, 10000);
    if (c >= 0 && (c != v->sel_b || v->sel_a < 0 || c < d->anchor)) sel_set(d, obj, d->dpage, d->anchor, c);
    /* near the top or bottom: scroll */
    if (my > _mbottom(obj) - 4) scroll_to(d, obj, v->sx, v->sy + 16);
    else if (my < _mtop(obj) + 4) scroll_to(d, obj, v->sx, v->sy - 16);
    return; }
  case DR_RECT: case DR_OVAL: case DR_LINE: case DR_BOX:
    ov_hide(d, obj);
    to_page(d, obj, d->dpage, mx, my, &d->bx, &d->by);
    ov_show(d, obj);
    return;
  case DR_INK:
    to_page(d, obj, d->dpage, mx, my, &x, &y);
    if (d->nink && fabs(x - d->ink[d->nink * 2 - 2]) + fabs(y - d->ink[d->nink * 2 - 1]) < 1.5 / v->zoom) return;
    if (d->nink >= d->capink) {
      int nc = d->capink * 2;
      double *n2 = (double *)realloc(d->ink, (size_t)nc * 2 * sizeof(double));
      if (!n2 || nc > 20000) return;
      d->ink = n2; d->capink = nc;
    }
    d->ink[d->nink * 2] = x; d->ink[d->nink * 2 + 1] = y;
    d->nink++;
    if (d->ov_on && d->nink >= 2) {
      /* just the new segment added to the overlay */
      struct RastPort *rp = _rp(obj);
      LONG a, b, c, e;
      APTR clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
      to_win(d, obj, d->dpage, d->ink[d->nink * 2 - 4], d->ink[d->nink * 2 - 3], &a, &b);
      to_win(d, obj, d->dpage, x, y, &c, &e);
      SetDrMd(rp, COMPLEMENT); SetAPen(rp, 1);
      Move(rp, a, b); Draw(rp, c, e);
      SetDrMd(rp, JAM1);
      MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
    }
    return;
  case DR_ORGMOVE: {
    LONG lx = mx - _mleft(obj) - d->ox - GAP + CW / 2, ly = my - _mtop(obj) - d->oy - GAP;
    int col = lx >= 0 ? (int)(lx / CW) : 0, row = ly >= 0 ? (int)(ly / CH) : 0, k;
    if (abs(mx - d->mx0) < 6 && abs(my - d->my0) < 6) return;
    if (col > d->cols) col = d->cols;
    k = row * d->cols + col;
    if (col == d->cols) k = row * d->cols + d->cols;
    if (k > d->n) k = d->n;
    if (k != d->drop) { ov_hide(d, obj); d->drop = k; ov_show(d, obj); }
    if (my > _mbottom(obj) - 6) { ov_hide(d, obj); scroll_to(d, obj, v->sx, v->sy + 12); }
    else if (my < _mtop(obj) + 6) { ov_hide(d, obj); scroll_to(d, obj, v->sx, v->sy - 12); }
    return; }
  }
}
static void release(struct Data *d, Object *obj, LONG mx, LONG my)
{
  DocView *v = d->lv;
  int drag = d->drag, moved = abs(mx - d->mx0) > 3 || abs(my - d->my0) > 3;
  ov_hide(d, obj);
  d->drag = DR_NONE;
  if (!v) return;
  switch (drag) {
  case DR_PAN: case DR_CLICK:
    if (!moved && v->mode == VM_READ) {
      int i = page_at(d, obj, mx, my, 0);
      double x, y;
      if (i >= 0) {
        to_page(d, obj, i, mx, my, &x, &y);
        if (tool == TL_NOTE && drag == DR_CLICK) { edit_note(v, i, x, y); return; }
        if (tool == TL_SELECT || tool == TL_HAND) follow_link(d, obj, i, x, y, 1);
      }
    }
    return;
  case DR_TEXT:
    if (v->sel_a < 0) {
      /* a click on text without a drag: a link there is followed */
      sel_clear(d, obj);
      if (!moved && tool == TL_SELECT) {
        int i = page_at(d, obj, mx, my, 0);
        double x, y;
        if (i >= 0) { to_page(d, obj, i, mx, my, &x, &y); follow_link(d, obj, i, x, y, 1); }
      }
      return;
    }
    if (is_markup_tool(tool)) {
      double *q;
      int n = selection_quads(v, &q);
      int page = v->sel_page;
      if (n > 0) {
        v->sel_page = -1;
        edit_markup(v, page, tool == TL_HIGHLIGHT ? AN_HIGHLIGHT : tool == TL_UNDERLINE ? AN_UNDERLINE : AN_STRIKE, q, n);
      }
      free(q);
      return;
    }
    tell(obj, MUIA_View_Picked);
    return;
  case DR_RECT: case DR_OVAL: case DR_LINE: case DR_BOX: {
    double c[4];
    c[0] = d->ax; c[1] = d->ay; c[2] = d->bx; c[3] = d->by;
    if (!moved) {
      if (drag != DR_BOX) return;
      /* a click: a text box of a usual size there */
      c[2] = c[0] + 180; c[3] = c[1] - opt.text_size * 2.4;
    }
    if (drag == DR_BOX) { double r[4]; r[0] = c[0] < c[2] ? c[0] : c[2]; r[2] = c[0] < c[2] ? c[2] : c[0]; r[1] = c[1] < c[3] ? c[1] : c[3]; r[3] = c[1] < c[3] ? c[3] : c[1]; edit_textbox(v, d->dpage, r); }
    else edit_shape(v, d->dpage, drag == DR_RECT ? AN_SQUARE : drag == DR_OVAL ? AN_CIRCLE : AN_LINE, c);
    return; }
  case DR_INK:
    if (d->nink >= 1) edit_ink(v, d->dpage, d->ink, d->nink);
    else redraw_page(d, obj, d->dpage);
    return;
  case DR_ORGMOVE:
    if (d->drop >= 0 && moved) {
      int to = d->drop > d->dpage ? d->drop - 1 : d->drop;
      if (to != d->dpage) edit_move(v, d->dpage, to);
    }
    d->drop = -1;
    return;
  }
}

/* ------------------------------------------------------------------ keys */
static int keys(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  DocView *v = cur_view();
  UWORD code = im->Code, q = im->Qualifier;
  int shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
  Object *active = (Object *)NULL;
  LONG H = _mheight(obj), step = 40;
  if (!v || (code & IECODE_UP_PREFIX)) return 0;
  get(_win(obj), MUIA_Window_ActiveObject, &active);
  if (active && active != obj) return 0;          /* (a text field is being typed in) */
  switch (code) {
  case 0x4C: scroll_to(d, obj, v->sx, v->sy - (shift ? H - 20 : step)); return 1;           /* up */
  case 0x4D: scroll_to(d, obj, v->sx, v->sy + (shift ? H - 20 : step)); return 1;           /* down */
  case 0x4F: scroll_to(d, obj, v->sx - (shift ? _mwidth(obj) - 20 : step), v->sy); return 1; /* left */
  case 0x4E: scroll_to(d, obj, v->sx + (shift ? _mwidth(obj) - 20 : step), v->sy); return 1; /* right */
  case 0x7A: scroll_to(d, obj, v->sx, v->sy - 3 * step); return 1;                          /* the wheel */
  case 0x7B: scroll_to(d, obj, v->sx, v->sy + 3 * step); return 1;
  case 0x48: if (v->mode == VM_READ && v->page > 0) go_page(d, obj, v->page - 1, -1); return 1;    /* page up */
  case 0x49: if (v->mode == VM_READ && v->page + 1 < v->pdf->npages) go_page(d, obj, v->page + 1, -1); return 1;  /* page down */
  case 0x40: scroll_to(d, obj, v->sx, v->sy + (shift ? -(H - 20) : H - 20)); return 1;       /* space */
  case 0x46: if (v->an_page >= 0) { edit_delete_annot(v); return 1; } return 0;                /* Del */
  case 0x45:                                                                                   /* Esc */
    if (v->sel_page >= 0) sel_clear(d, obj);
    if (v->an_page >= 0) choose_annot(d, obj, -1, -1);
    if (tool != TL_SELECT) set_tool(TL_SELECT);
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ events */
#define EV (IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE)
static ULONG handle(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct Data *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  LONG mx, my;
  int in;
  if (!im || !d->shown) return 0;
  if (im->Class == IDCMP_RAWKEY) {
    LONG x = im->MouseX, y = im->MouseY;
    /* (the wheel only over the view) */
    if ((im->Code == 0x7A || im->Code == 0x7B) && !(x >= _mleft(obj) && x <= _mright(obj) && y >= _mtop(obj) && y <= _mbottom(obj))) return 0;
    return keys(d, obj, im) ? MUI_EventHandlerRC_Eat : 0;
  }
  mx = im->MouseX; my = im->MouseY;
  in = mx >= _mleft(obj) && mx <= _mright(obj) && my >= _mtop(obj) && my <= _mbottom(obj);
  if (im->Class == IDCMP_MOUSEMOVE) {
    if (d->drag) { motion(d, obj, mx, my); return MUI_EventHandlerRC_Eat; }
    if (in && d->lv && d->lv->mode == VM_READ) {
      int i = page_at(d, obj, mx, my, 0);
      char old[96];
      strcpy(old, view_info);
      view_info[0] = 0;
      if (i >= 0 && (tool == TL_SELECT || tool == TL_HAND)) {
        double x, y;
        to_page(d, obj, i, mx, my, &x, &y);
        follow_link(d, obj, i, x, y, 0);
      }
      if (strcmp(old, view_info)) DoMethod(statusbar, MUIM_Status_Refresh);
    }
    return 0;
  }
  if (im->Class == IDCMP_MOUSEBUTTONS) {
    if (im->Code == SELECTDOWN && in) {
      set(_win(obj), MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_None);
      press(d, obj, mx, my, im->Qualifier, im->Seconds, im->Micros);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code == SELECTUP && d->drag) { release(d, obj, mx, my); return MUI_EventHandlerRC_Eat; }
  }
  return 0;
}

/* ------------------------------------------------------------------ the dispatcher */
static void mset(struct Data *d, Object *obj, struct opSet *m)
{
  struct TagItem *tags = m->ops_AttrList, *t;
  DocView *v = cur_view();
  while ((t = NextTagItem(&tags))) {
    switch (t->ti_Tag) {
    case MUIA_View_VProp: d->vprop = (Object *)t->ti_Data; break;
    case MUIA_View_HProp: d->hprop = (Object *)t->ti_Data; break;
    case MUIA_View_Top: if (v && d->shown) scroll_to(d, obj, v->sx, (LONG)t->ti_Data); break;
    case MUIA_View_Left: if (v && d->shown) scroll_to(d, obj, (LONG)t->ti_Data, v->sy); break;
    }
  }
}
static ULONG dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct Data *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->obj = obj; d->drop = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = EV;
    d->ihn.ihn_Object = obj; d->ihn.ihn_Flags = MUIIHNF_TIMER; d->ihn.ihn_Method = MUIM_View_Tick;
    d->ihn.ihn_stuff.ihn_timer.ihn_millis = 60;
    mset(d, obj, (struct opSet *)msg);
    return (ULONG)obj;
  case OM_DISPOSE: {
    int i;
    d = INST_DATA(cl, obj);
    free(d->px); free(d->py); free(d->pw); free(d->ph); free(d->ink);
    for (i = 0; i < MAXCE; i++) ce_drop(&ce[i]);
    back_free(d);
    break; }
  case OM_SET:
    mset(INST_DATA(cl, obj), obj, (struct opSet *)msg);
    break;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID >= MUIA_View_Changed && g->opg_AttrID <= MUIA_View_EditNote) { *g->opg_Storage = TRUE; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 120; m->MinMaxInfo->DefWidth += 480; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 80; m->MinMaxInfo->DefHeight += 400; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    if (d->handlers) return TRUE;
    theme_setup(obj);
    theme_handler(obj, &d->ehn);
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->ihn);
    d->handlers = 1;
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    if (d->handlers) {
      DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->ihn);
      theme_cleanup(obj);
      back_free(d);
      pens_free();
      d->handlers = 0;
    }
    break;
  case MUIM_Show: ((struct Data *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: d = INST_DATA(cl, obj); d->shown = 0; d->ov_on = 0; d->drag = DR_NONE; abort_win = NULL; back_free(d); break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) draw_all(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_HandleEvent:
    return handle(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_View_Tick:
    tick(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_View_Refresh:
    d = INST_DATA(cl, obj);
    d->drag = DR_NONE; d->ov_on = 0;
    d->lv = NULL;                                 /* (laid out again) */
    if (d->shown) draw_all(d, obj);
    return 0;
  case MUIM_View_Redraw:
    d = INST_DATA(cl, obj);
    if (d->shown) draw_all(d, obj);
    return 0;
  case MUIM_View_Zoom: {
    struct MUIP_View_Zoom *m = (struct MUIP_View_Zoom *)msg;
    DocView *v = cur_view();
    d = INST_DATA(cl, obj);
    if (!v || !d->shown) return 0;
    if (m->how == 1 || m->how == -1) zoom_to(d, obj, zoom_step(v->zoom, (int)m->how), FIT_NONE);
    else if (m->how == 2) zoom_to(d, obj, 0, FIT_WIDTH);
    else if (m->how == 3) zoom_to(d, obj, 0, FIT_PAGE);
    else zoom_to(d, obj, m->permille / 1000.0, FIT_NONE);
    return 0;
  }
  case MUIM_View_GoPage: {
    struct MUIP_View_GoPage *m = (struct MUIP_View_GoPage *)msg;
    d = INST_DATA(cl, obj);
    if (d->shown) go_page(d, obj, (int)m->page, m->top < 0 ? -1 : (double)m->top);
    else if (cur_view()) cur_view()->page = (int)m->page;
    return 0;
  }
  case MUIM_View_Tool:
    d = INST_DATA(cl, obj);
    ov_hide(d, obj);
    d->drag = DR_NONE;
    return 0;
  case MUIM_View_PageEdited: {
    struct MUIP_View_PageEdited *m = (struct MUIP_View_PageEdited *)msg;
    int i;
    d = INST_DATA(cl, obj);
    ce_page_edited(m->v, m->pg, (int)m->added);
    /* just that page drawn again (from its picture, kept or added to) */
    if (d->shown && d->lv == m->v) for (i = 0; i < d->n; i++) if (m->v->pdf->pages[i] == m->pg) { redraw_page(d, obj, i); break; }
    return 0;
  }
  case MUIM_View_Forget: {
    struct MUIP_View_Forget *m = (struct MUIP_View_Forget *)msg;
    ce_forget(m->v, m->pg);
    return 0;
  }
  case MUIM_View_ShowMatch: {
    DocView *v = cur_view();
    d = INST_DATA(cl, obj);
    if (!v || v->find_page < 0 || !d->shown) return 0;
    layout(d, obj); place(d, obj);
    {
      TextPage *t = page_text(v, v->find_page);
      LONG r[4];
      int p = v->find_page;
      if (t && v->find_a >= 0 && v->find_a < t->no) {
        rect_win(d, obj, p, t->o[v->find_a].x0, t->o[v->find_a].y0, t->o[v->find_a].x1, t->o[v->find_a].y1, r);
        /* scrolled so the match is in view, a third of the way down */
        if (r[1] < _mtop(obj) + 10 || r[3] > _mbottom(obj) - 10 || r[0] < _mleft(obj) || r[2] > _mright(obj)) {
          v->sy += r[1] - (_mtop(obj) + _mheight(obj) / 3);
          if (r[0] < _mleft(obj) || r[2] > _mright(obj)) v->sx += r[0] - (_mleft(obj) + _mwidth(obj) / 3);
        }
      }
      draw_all(d, obj);
      (void)p;
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *view_class(void)
{
  if (!vcls) vcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct Data), (APTR)dispatch);
  return vcls;
}
void view_class_free(void)
{
  int i;
  for (i = 0; i < MAXCE; i++) ce_drop(&ce[i]);
  if (vcls) { MUI_DeleteCustomClass(vcls); vcls = NULL; }
}
