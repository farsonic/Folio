/*
 * widgets.c - small pieces Easel's classes share: drawing RGB pixels on any screen (exactly on a
 * true-colour one through cybergraphics, dithered to a cube of pens on one with a palette), the
 * drop-down list window, and the flat scroll bar.
 */
#include <string.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"

/* cybergraphics.library's WritePixelArray, as a vbcc inline call (the library is optional) */
ULONG __cgx_WritePixelArray(__reg("a6") void *, __reg("a0") APTR, __reg("d0") UWORD, __reg("d1") UWORD, __reg("d2") UWORD,
                            __reg("a1") struct RastPort *, __reg("d3") UWORD, __reg("d4") UWORD, __reg("d5") UWORD,
                            __reg("d6") UWORD, __reg("d7") UBYTE) = "\tjsr\t-126(a6)";
#define WritePixelArray(src, sx, sy, mod, rp, dx, dy, w, h, fmt) \
  __cgx_WritePixelArray(CyberGfxBase, (src), (sx), (sy), (mod), (rp), (dx), (dy), (w), (h), (fmt))
#define RECTFMT_RGB 0

/* ------------------------------------------------------------------ pixels */
static struct ColorMap *cube_cm;
static LONG cube[216];
static int ncube, cubelv;
static UBYTE *chunky; static LONG chunky_cap;
static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

/* (see easel.h) a screen deeper than 8 bits with cybergraphics to write to it */
int truecolor(struct Screen *scr)
{
  return CyberGfxBase && scr && GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) > 8;
}
LONG rgb_pen(struct Screen *scr, ULONG c)
{
  return ObtainBestPen(scr->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL, ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL,
                       OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
/* the cube of pens pictures are dithered to: 6 levels a channel on an 8-bit screen, 3 or 2 on
   shallower ones, obtained once */
static void get_cube(struct Screen *scr)
{
  LONG depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
  int r, g, b;
  if (ncube && cube_cm == scr->ViewPort.ColorMap) return;
  pens_free();
  cube_cm = scr->ViewPort.ColorMap;
  cubelv = depth >= 8 ? 6 : depth >= 5 ? 3 : 2;
  for (r = 0; r < cubelv; r++) for (g = 0; g < cubelv; g++) for (b = 0; b < cubelv; b++)
    cube[ncube++] = rgb_pen(scr, ((ULONG)(r * 255 / (cubelv - 1)) << 16) | ((ULONG)(g * 255 / (cubelv - 1)) << 8) | (ULONG)(b * 255 / (cubelv - 1)));
}
void pens_free(void)
{
  int i;
  for (i = 0; i < ncube; i++) ReleasePen(cube_cm, cube[i]);
  ncube = 0;
  if (chunky) { FreeVec(chunky); chunky = NULL; chunky_cap = 0; }
}
void draw_rgb(struct RastPort *rp, struct Screen *scr, const UBYTE *rgb, LONG mod, LONG x, LONG y, LONG w, LONG h)
{
  LONG i, j;
  if (w <= 0 || h <= 0) return;
  if (truecolor(scr)) {
    WritePixelArray((APTR)rgb, 0, 0, (UWORD)mod, rp, (UWORD)x, (UWORD)y, (UWORD)w, (UWORD)h, RECTFMT_RGB);
    return;
  }
  get_cube(scr);
  if (w * h > chunky_cap) {
    if (chunky) FreeVec(chunky);
    chunky_cap = w * h;
    if (!(chunky = (UBYTE *)AllocVec((ULONG)chunky_cap, MEMF_ANY))) { chunky_cap = 0; return; }
  }
  for (j = 0; j < h; j++) {
    const UBYTE *s = rgb + j * mod;
    UBYTE *o = chunky + j * w;
    for (i = 0; i < w; i++, s += 3) {
      int t = bayer[(y + j) & 3][(x + i) & 3] * 255 / 16, q[3], k;
      for (k = 0; k < 3; k++) { q[k] = (s[k] * (cubelv - 1) + t) / 255; if (q[k] > cubelv - 1) q[k] = cubelv - 1; }
      o[i] = (UBYTE)cube[(q[0] * cubelv + q[1]) * cubelv + q[2]];
    }
  }
  WriteChunkyPixels(rp, (ULONG)x, (ULONG)y, (ULONG)(x + w - 1), (ULONG)(y + h - 1), chunky, w);
}
void draw_rgb_box(struct RastPort *rp, struct Screen *scr, LONG x0, LONG y0, LONG x1, LONG y1, ULONG c)
{
  if (x1 < x0 || y1 < y0) return;
  if (truecolor(scr)) {
    /* one row, written again for each line */
    UBYTE row[3 * 64];
    LONG w = x1 - x0 + 1, i, y;
    for (i = 0; i < 64; i++) { row[i * 3] = (UBYTE)(c >> 16); row[i * 3 + 1] = (UBYTE)(c >> 8); row[i * 3 + 2] = (UBYTE)c; }
    for (y = y0; y <= y1; y++)
      for (i = 0; i < w; i += 64) WritePixelArray(row, 0, 0, sizeof row, rp, (UWORD)(x0 + i), (UWORD)y, (UWORD)(w - i < 64 ? w - i : 64), 1, RECTFMT_RGB);
    return;
  }
  {
    LONG pen = rgb_pen(scr, c);
    SetAPen(rp, pen);
    RectFill(rp, x0, y0, x1, y1);
    ReleasePen(scr->ViewPort.ColorMap, pen);
  }
}

/* ------------------------------------------------------------------ the drop-down list */
struct PData { Object *owner; ULONG attr; const char *const *labels; int n, hot, cur; LONG w; };
static struct MUI_CustomClass *pcls;
static Object *popwin, *poplist;
static LONG rowh(void) { return theme_font()->tf_YSize + 5; }
static ULONG popdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PData *p;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    p = INST_DATA(cl, obj);
    memset(p, 0, sizeof *p);
    p->hot = -1;
    return (ULONG)obj;
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    MUI_RequestIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    return TRUE;
  case MUIM_Cleanup:
    MUI_RejectIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    theme_cleanup(obj);
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h;
    p = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, msg);
    h = (p->n ? p->n : 1) * rowh() + 4;
    m->MinMaxInfo->MinWidth += p->w; m->MinMaxInfo->DefWidth += p->w; m->MinMaxInfo->MaxWidth += p->w;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    int i;
    DoSuperMethodA(cl, obj, msg);
    p = INST_DATA(cl, obj);
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
    theme_rect(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_BORDER2));
    for (i = 0; i < p->n; i++) {
      LONG y = _mtop(obj) + 2 + i * rowh();
      if (i == p->hot) theme_fill(rp, _mleft(obj) + 1, y, _mright(obj) - 1, y + rowh() - 1, tpen(T_ACCENTDARK));
      if (i == p->cur) theme_fill(rp, _mleft(obj) + 4, y + rowh() / 2 - 2, _mleft(obj) + 7, y + rowh() / 2 + 1, tpen(T_TEXT));
      theme_text(rp, theme_font(), _mleft(obj) + 12, y + 2, p->labels[i], tpen(i == p->hot ? T_WHITE : T_TEXT));
    }
    return 0;
  }
  case MUIM_HandleInput: {
    struct MUIP_HandleInput *m = (struct MUIP_HandleInput *)msg;
    p = INST_DATA(cl, obj);
    if (m->imsg && (m->imsg->Class == IDCMP_MOUSEMOVE || m->imsg->Class == IDCMP_MOUSEBUTTONS)) {
      WORD mx = m->imsg->MouseX, my = m->imsg->MouseY;
      int i = -1;
      if (mx >= _mleft(obj) && mx <= _mright(obj) && my >= _mtop(obj) + 2) i = (int)((my - _mtop(obj) - 2) / rowh());
      if (i >= p->n) i = -1;
      if (m->imsg->Class == IDCMP_MOUSEMOVE && i != p->hot) { p->hot = i; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      if (m->imsg->Class == IDCMP_MOUSEBUTTONS && m->imsg->Code == SELECTUP && i >= 0) {
        DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)_win(obj), 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)p->owner, 3, MUIM_Set, p->attr, (ULONG)i);
      }
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
void popup_list(Object *owner, LONG x, LONG y, LONG w, const char *const *labels, int n, int cur, ULONG attr)
{
  struct PData *p;
  struct Screen *scr = _screen(owner);
  struct RastPort *rp = _rp(owner);
  int i;
  if (!pcls && !(pcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PData), (APTR)popdispatch))) return;
  if (!popwin) {
    poplist = NewObject(pcls->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE);
    popwin = QWindowObject, MUIA_Window_Borderless, TRUE, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
      MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, FALSE, MUIA_Window_Activate, TRUE,
      WindowContents, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, Child, poplist, End, End;
    if (!popwin) { poplist = NULL; return; }
    DoMethod(_app(owner), OM_ADDMEMBER, (ULONG)popwin);
    DoMethod(popwin, MUIM_Notify, MUIA_Window_Activate, FALSE, (ULONG)popwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set(popwin, MUIA_Window_Open, FALSE);
  p = INST_DATA(pcls->mcc_Class, poplist);
  p->owner = owner; p->attr = attr; p->labels = labels; p->n = n; p->cur = cur; p->hot = -1;
  for (i = 0; i < n; i++) { LONG tw = theme_textlen(rp, theme_font(), labels[i]) + 24; if (tw > w) w = tw; }
  p->w = w;
  if (y + n * rowh() + 12 > scr->Height) y = scr->Height - n * rowh() - 12;
  if (y < 0) y = 0;
  if (x + w > scr->Width) x = scr->Width - w;
  if (x < 0) x = 0;
  SetAttrs(popwin, MUIA_Window_LeftEdge, x, MUIA_Window_TopEdge, y, TAG_DONE);
  set(popwin, MUIA_Window_Open, TRUE);
}

/* ------------------------------------------------------------------ the flat scroll bar */
/* takes MUIA_Prop_Entries, _Visible and _First as a Prop does, and notifies MUIA_Prop_First as it is dragged */
struct FData { LONG entries, visible, first; int drag, hot, horiz, shown; LONG grab; struct MUI_EventHandlerNode ehn; };
static LONG f0(struct FData *d, Object *obj) { return (d->horiz ? _mleft(obj) : _mtop(obj)) + 2; }
static LONG flen(struct FData *d, Object *obj) { return (d->horiz ? _mwidth(obj) : _mheight(obj)) - 4; }
static struct MUI_CustomClass *fcls;
/* the thumb's start and end along the bar (window pixels), at least 13 long and kept inside */
static void thumb(struct FData *d, Object *obj, LONG *a, LONG *b)
{
  LONG t = f0(d, obj), len = flen(d, obj), e = d->entries > d->visible ? d->entries : d->visible;
  if (e <= 0) e = 1;
  *a = t + (LONG)((double)len * d->first / e);
  *b = t + (LONG)((double)len * (d->first + d->visible) / e) - 1;
  if (*b < *a + 12) *b = *a + 12;
  if (*b > t + len - 1) { *a -= *b - (t + len - 1); *b = t + len - 1; }
  if (*a < t) *a = t;
}
static void flat_draw(struct FData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG a, b, l = _mleft(obj), r = _mright(obj);
  LONG pen = tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2);
  theme_fill(rp, l, _mtop(obj), r, _mbottom(obj), tpen(T_PANEL));
  if (d->entries <= d->visible) return;
  thumb(d, obj, &a, &b);
  if (d->horiz) {
    LONG t = (_mtop(obj) + _mbottom(obj)) / 2 - 4, u = t + 8;
    theme_fill(rp, a + 1, t, b - 1, u, pen);
    theme_fill(rp, a, t + 1, b, u - 1, pen);
    return;
  }
  theme_fill(rp, l + 2, a + 1, r - 2, b - 1, pen);
  theme_fill(rp, l + 3, a, r - 3, b, pen);
}
static void flat_set_first(struct FData *d, Object *obj, LONG f)
{
  LONG max = d->entries - d->visible;
  if (f > max) f = max;
  if (f < 0) f = 0;
  if (f == d->first) return;
  SetAttrs(obj, MUIA_Prop_First, f, TAG_DONE);          /* through OM_SET, so notifications go */
}
static ULONG fdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct FData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->entries = d->visible = 1;
    d->horiz = (int)GetTagData(MUIA_FlatScroll_Horiz, FALSE, ((struct opSet *)msg)->ops_AttrList);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    int redraw = 0;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) {
      if (t->ti_Tag == MUIA_Prop_Entries && d->entries != (LONG)t->ti_Data) { d->entries = (LONG)t->ti_Data; redraw = 1; }
      else if (t->ti_Tag == MUIA_Prop_Visible && d->visible != (LONG)t->ti_Data) { d->visible = (LONG)t->ti_Data; redraw = 1; }
      else if (t->ti_Tag == MUIA_Prop_First && d->first != (LONG)t->ti_Data) { d->first = (LONG)t->ti_Data; redraw = 1; }
    }
    if (redraw && d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Prop_Entries) { *g->opg_Storage = (ULONG)d->entries; return TRUE; }
    if (g->opg_AttrID == MUIA_Prop_Visible) { *g->opg_Storage = (ULONG)d->visible; return TRUE; }
    if (g->opg_AttrID == MUIA_Prop_First) { *g->opg_Storage = (ULONG)d->first; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    if (((struct FData *)INST_DATA(cl, obj))->horiz) {
      m->MinMaxInfo->MinHeight += 10; m->MinMaxInfo->DefHeight += 10; m->MinMaxInfo->MaxHeight += 10;
      m->MinMaxInfo->MinWidth += 30; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
      return 0;
    }
    m->MinMaxInfo->MinWidth += 10; m->MinMaxInfo->DefWidth += 10; m->MinMaxInfo->MaxWidth += 10;
    m->MinMaxInfo->MinHeight += 30; m->MinMaxInfo->DefHeight += 200; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct FData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) flat_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct FData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct FData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    LONG a, b, mp;
    int inside;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || d->entries <= d->visible) return 0;
    inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
    thumb(d, obj, &a, &b);
    mp = d->horiz ? im->MouseX : im->MouseY;
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (d->drag) {
        LONG len = flen(d, obj);
        flat_set_first(d, obj, (LONG)((double)(mp - d->grab - f0(d, obj)) * d->entries / (len > 0 ? len : 1)));
        return MUI_EventHandlerRC_Eat;
      }
      if ((inside && mp >= a && mp <= b) != d->hot) { d->hot = !d->hot; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP && d->drag) { d->drag = 0; MUI_Redraw(obj, MADF_DRAWOBJECT); return MUI_EventHandlerRC_Eat; }
    if (im->Code != SELECTDOWN || !inside) return 0;
    if (mp < a) flat_set_first(d, obj, d->first - d->visible * 9 / 10);
    else if (mp > b) flat_set_first(d, obj, d->first + d->visible * 9 / 10);
    else { d->drag = 1; d->grab = mp - a; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *flatscroll_class(void)
{
  if (!fcls) fcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct FData), (APTR)fdispatch);
  return fcls;
}
/* at the end, once every window is gone: the list window is the application's and went with it */
void popup_free(void)
{
  popwin = poplist = NULL;
  if (pcls) { MUI_DeleteCustomClass(pcls); pcls = NULL; }
  if (fcls) { MUI_DeleteCustomClass(fcls); fcls = NULL; }
}

/* ------------------------------------------------------------------ text */
/* s (Latin-1) cut short to fit maxw pixels in tf, drawn in pen with its top at y */
void text_fit(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG maxw, LONG pen)
{
  struct TextExtent te;
  ULONG fit;
  if (maxw <= 0) return;
  SetFont(rp, tf);
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  fit = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)maxw, 100);
  SetAPen(rp, pen); SetDrMd(rp, JAM1);
  Move(rp, x, y + tf->tf_Baseline);
  Text(rp, (STRPTR)s, fit);
}
/* a UTF-8 string as Latin-1 (characters it hasn't become ?) */
void to_latin1(const char *u, char *out, size_t cap) { utf8_to_latin1(u, strlen(u), out, cap); }
