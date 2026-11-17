/*
 * bars.c - the strips round Folio's page view, as PDFCraft has them:
 *   strip    a row (or column) of tool buttons: icons, icons with a word, text that changes (the
 *            page count, the zoom), separators, a gap that pushes what follows to the right, and a
 *            colour swatch. Each item's id is a command, sent to main.c's loop when clicked;
 *            whether it is lit or dimmed main.c says (strip_state);
 *   tabs     Home, Tools, then a tab for each open document with its cross;
 *   status   the page and its size, the zoom, what the pointer is over.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

static int fh(void) { return theme_font()->tf_YSize; }
static struct MUI_CustomClass *stripcls, *tabcls, *stcls;
static int inside(Object *obj, LONG x, LONG y) { return x >= _mleft(obj) && x <= _mright(obj) && y >= _mtop(obj) && y <= _mbottom(obj); }

/* ================================================================== a strip of tool buttons */
#define MAXITEMS 48
struct SData {
  struct MUI_EventHandlerNode ehn;
  const StripItem *items; int n;
  int vertical, size, shown;
  int hot, down;                       /* the item under the pointer, the one pressed (-1 none) */
  ULONG pick, chosen;
  LONG a0[MAXITEMS], a1[MAXITEMS];     /* each item's extent along the strip (window pixels) */
};
/* text's width in the interface font (translated), measured without a window */
static LONG tw(const char *s)
{
  struct RastPort rp;
  InitRastPort(&rp);
  SetFont(&rp, theme_font());
  s = tr(s);
  return TextLength(&rp, (STRPTR)s, (ULONG)strlen(s));
}
static LONG item_len(struct SData *d, Object *obj, const StripItem *it)
{
  LONG s = d->size + 8;
  (void)obj;
  switch (it->kind) {
  case SI_SEP: return 9;
  case SI_GAP: return 0;
  case SI_LABEL: return d->vertical ? s : s + tw(it->label) + 4;
  case SI_TEXT: { LONG w = tw(strip_text(it->id)) + 12; return w < 48 ? 48 : w; }
  case SI_COLOR: return s + 6;
  }
  return s;
}
/* where every item goes: gaps share what is left over */
static void layout(struct SData *d, Object *obj)
{
  LONG total = 0, left, pos, gaps = 0;
  int i;
  for (i = 0; i < d->n; i++) { total += item_len(d, obj, &d->items[i]); if (d->items[i].kind == SI_GAP) gaps++; }
  left = (d->vertical ? _mheight(obj) : _mwidth(obj)) - 4 - total;
  if (left < 0) left = 0;
  pos = (d->vertical ? _mtop(obj) : _mleft(obj)) + 2;
  for (i = 0; i < d->n; i++) {
    LONG l = item_len(d, obj, &d->items[i]);
    if (d->items[i].kind == SI_GAP && gaps) { l = left / gaps; left -= l; gaps--; }
    d->a0[i] = pos; d->a1[i] = pos + l - 1;
    pos += l;
  }
}
static int hit(struct SData *d, Object *obj, LONG x, LONG y)
{
  LONG a = d->vertical ? y : x;
  int i;
  if (!inside(obj, x, y)) return -1;
  for (i = 0; i < d->n; i++) {
    int k = d->items[i].kind;
    if (k == SI_SEP || k == SI_GAP) continue;
    if (a >= d->a0[i] && a <= d->a1[i]) return (strip_state(d->items[i].id) & SS_DISABLED) ? -1 : i;
  }
  return -1;
}
static void draw_item(struct SData *d, Object *obj, int i)
{
  struct RastPort *rp = _rp(obj);
  const StripItem *it = &d->items[i];
  ULONG st = strip_state(it->id);
  LONG x0, y0, x1, y1, ix, iy;
  int dim = (st & SS_DISABLED) != 0;
  if (d->vertical) { x0 = _mleft(obj) + 2; x1 = _mright(obj) - 2; y0 = d->a0[i]; y1 = d->a1[i]; }
  else { x0 = d->a0[i]; x1 = d->a1[i]; y0 = _mtop(obj) + 2; y1 = _mbottom(obj) - 2; }
  theme_fill(rp, x0, y0, x1, y1, tpen(T_RIBBON));
  if (it->kind == SI_SEP) {
    SetAPen(rp, tpen(T_BORDER));
    if (d->vertical) { Move(rp, x0 + 3, (y0 + y1) / 2); Draw(rp, x1 - 3, (y0 + y1) / 2); }
    else { Move(rp, (x0 + x1) / 2, y0 + 3); Draw(rp, (x0 + x1) / 2, y1 - 3); }
    return;
  }
  if (it->kind == SI_GAP) return;
  if (!dim && (i == d->down && i == d->hot)) theme_fill(rp, x0, y0, x1, y1, tpen(T_PRESSED));
  else if (st & SS_CHECKED) { theme_fill(rp, x0, y0, x1, y1, tpen(T_CHECKED)); theme_rect(rp, x0, y0, x1, y1, tpen(T_ACCENT)); }
  else if (!dim && i == d->hot) theme_fill(rp, x0, y0, x1, y1, tpen(T_HOVER));
  ix = x0 + 4; iy = y0 + (y1 - y0 + 1 - d->size) / 2;
  switch (it->kind) {
  case SI_ICON: theme_icon(rp, it->icon, x0 + (x1 - x0 + 1 - d->size) / 2, iy, d->size, dim); break;
  case SI_LABEL:
    if (d->vertical) { theme_icon(rp, it->icon, x0 + (x1 - x0 + 1 - d->size) / 2, iy, d->size, dim); break; }
    theme_icon(rp, it->icon, ix, iy, d->size, dim);
    theme_text(rp, theme_font(), ix + d->size + 4, y0 + (y1 - y0 + 1 - fh()) / 2, it->label, tpen(dim ? T_DISABLED : T_TEXT));
    break;
  case SI_TEXT: {
    const char *s = strip_text(it->id);
    LONG w = theme_textlen(rp, theme_font(), s);
    theme_text(rp, theme_font(), x0 + (x1 - x0 + 1 - w) / 2, y0 + (y1 - y0 + 1 - fh()) / 2, s, tpen(dim ? T_DISABLED : T_TEXT));
    break; }
  case SI_COLOR: {
    ULONG c = strip_color(it->id);
    LONG s = d->size;
    draw_rgb_box(rp, _screen(obj), ix, iy, ix + s - 1, iy + s - 1, c);
    theme_rect(rp, ix, iy, ix + s - 1, iy + s - 1, tpen(T_BORDER));
    /* the little arrow saying it opens a choice */
    SetAPen(rp, tpen(dim ? T_DISABLED : T_ICON));
    Move(rp, ix + s + 2, iy + s / 2 - 1); Draw(rp, ix + s + 6, iy + s / 2 - 1);
    Move(rp, ix + s + 3, iy + s / 2); Draw(rp, ix + s + 5, iy + s / 2);
    WritePixel(rp, ix + s + 4, iy + s / 2 + 1);
    break; }
  }
}
static void strip_draw(struct SData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  int i;
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_RIBBON));
  layout(d, obj);
  for (i = 0; i < d->n; i++) draw_item(d, obj, i);
  SetAPen(rp, tpen(T_BORDER));
  if (d->vertical) { Move(rp, _mright(obj), _mtop(obj)); Draw(rp, _mright(obj), _mbottom(obj)); }
  else { Move(rp, _mleft(obj), _mbottom(obj)); Draw(rp, _mright(obj), _mbottom(obj)); }
}
static void set_hot(struct SData *d, Object *obj, int k)
{
  int old = d->hot;
  if (k == old) return;
  d->hot = k;
  if (!d->shown) return;
  if (old >= 0 && old < d->n) draw_item(d, obj, old);
  if (k >= 0) draw_item(d, obj, k);
}
static ULONG stripdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct SData *d;
  switch (msg->MethodID) {
  case OM_NEW: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->items = (const StripItem *)GetTagData(MUIA_Strip_Items, 0, tags);
    d->vertical = (int)GetTagData(MUIA_Strip_Vertical, 0, tags);
    d->size = (int)GetTagData(MUIA_Strip_Size, 16, tags);
    while (d->items && d->items[d->n].kind != SI_END && d->n < MAXITEMS) d->n++;
    d->hot = d->down = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  }
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_Popup_Chosen, ((struct opSet *)msg)->ops_AttrList);
    if (t) ((struct SData *)INST_DATA(cl, obj))->chosen = t->ti_Data;
    break;
  }
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Strip_Pick) { *((struct opGet *)msg)->opg_Storage = ((struct SData *)INST_DATA(cl, obj))->pick; return TRUE; }
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Popup_Chosen) { *((struct opGet *)msg)->opg_Storage = ((struct SData *)INST_DATA(cl, obj))->chosen; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG t;
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    t = d->size + 10;
    if (d->vertical) {
      m->MinMaxInfo->MinWidth += t; m->MinMaxInfo->DefWidth += t; m->MinMaxInfo->MaxWidth += t;
      m->MinMaxInfo->MinHeight += 40; m->MinMaxInfo->DefHeight += 200; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    } else {
      int i, gap = 0;
      LONG w = 4;
      if (t < fh() + 10) t = fh() + 10;
      for (i = 0; i < d->n; i++) { if (d->items[i].kind == SI_GAP) gap = 1; w += item_len(d, obj, &d->items[i]); }
      /* without a gap, exactly as wide as its items */
      if (gap) { m->MinMaxInfo->MinWidth += 40; m->MinMaxInfo->DefWidth += w; m->MinMaxInfo->MaxWidth += MUI_MAXMAX; }
      else { m->MinMaxInfo->MinWidth += w; m->MinMaxInfo->DefWidth += w; m->MinMaxInfo->MaxWidth += w; }
      m->MinMaxInfo->MinHeight += t; m->MinMaxInfo->DefHeight += t; m->MinMaxInfo->MaxHeight += t;
    }
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct SData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct SData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: d = INST_DATA(cl, obj); d->shown = 0; d->hot = d->down = -1; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) strip_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Strip_Refresh:
    if (((struct SData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int k;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    k = hit(d, obj, im->MouseX, im->MouseY);
    if (im->Class == IDCMP_MOUSEMOVE) { if (k != d->hot) set_hot(d, obj, k); return 0; }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTDOWN && k >= 0) { d->down = k; draw_item(d, obj, k); return MUI_EventHandlerRC_Eat; }
    if (im->Code == SELECTUP && d->down >= 0) {
      int was = d->down;
      d->down = -1;
      draw_item(d, obj, was);
      if (k == was) { d->pick = d->items[k].id; SetAttrs(obj, MUIA_Strip_Pick, d->pick, TAG_DONE); }
      return MUI_EventHandlerRC_Eat;
    }
    return 0;
  }
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    int k;
    const StripItem *it;
    d = INST_DATA(cl, obj);
    layout(d, obj);
    if ((k = hit(d, obj, m->mx, m->my)) < 0) return 0;
    it = &d->items[k];
    if (!it->label && !it->help) return 0;
    return (ULONG)theme_bubble(it->label ? tr(it->label) : tr(it->help), it->label && it->help ? tr(it->help) : "");
  }
  case MUIM_DeleteShortHelp: return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *strip_class(void)
{
  if (!stripcls) stripcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct SData), (APTR)stripdispatch);
  return stripcls;
}

/* ================================================================== the tab strip */
struct TData { struct MUI_EventHandlerNode ehn; int shown, pick, hot; LONG x0[MAXDOCS + 2], x1[MAXDOCS + 2]; };
static int tab_current(int i) { return i == 0 ? page_mode == PM_HOME : i == 1 ? page_mode == PM_TOOLS : page_mode == PM_DOC && i - 2 == curdoc; }
static void tabs_draw(struct TData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x = _mleft(obj) + 4, top = _mtop(obj), bot = _mbottom(obj);
  int i;
  char buf[140];
  theme_fill(rp, _mleft(obj), top, _mright(obj), bot, tpen(T_TABSTRIP));
  for (i = 0; i < ndocs + 2; i++) {
    LONG w;
    int cur = tab_current(i);
    struct TextFont *tf = cur ? theme_bold() : theme_font();
    if (i < 2) strcpy(buf, tr(i == 0 ? "Home" : "Tools"));
    else snprintf(buf, sizeof buf, "%s%s", docs[i - 2].name, docs[i - 2].pdf && docs[i - 2].pdf->changed ? " *" : "");
    w = theme_textlen(rp, tf, buf) + (i < 2 ? 24 : 40);
    if (i >= 2 && w > 200) w = 200;
    if (x + 30 > _mright(obj)) { d->x0[i] = d->x1[i] = -1; continue; }
    if (x + w > _mright(obj)) w = _mright(obj) - x;
    d->x0[i] = x; d->x1[i] = x + w - 1;
    if (cur) {
      theme_fill(rp, x, top + 3, x + w - 1, bot, tpen(T_RIBBON));
      SetAPen(rp, tpen(T_ACCENT)); Move(rp, x, top + 3); Draw(rp, x + w - 1, top + 3);
      Move(rp, x, top + 4); Draw(rp, x + w - 1, top + 4);
    } else if (i == d->hot) theme_fill(rp, x, top + 3, x + w - 1, bot - 1, tpen(T_HOVER));
    if (i >= 2) {
      LONG cx = x + w - 13, cy = (top + 3 + bot) / 2 + 1;
      theme_icon(rp, IC_F_PDF, x + 6, (top + bot) / 2 - 5, 12, 0);
      text_fit(rp, tf, x + 22, (top + bot - fh()) / 2 + 2, buf, w - 40, tpen(cur ? T_TEXT : T_DIM));
      SetAPen(rp, tpen(T_DIM));
      Move(rp, cx - 3, cy - 3); Draw(rp, cx + 3, cy + 3); Move(rp, cx + 3, cy - 3); Draw(rp, cx - 3, cy + 3);
    } else theme_text(rp, tf, x + 12, (top + bot - fh()) / 2 + 2, buf, tpen(cur ? T_TEXT : T_DIM));
    if (!cur && !(i + 1 < ndocs + 2 && tab_current(i + 1))) { SetAPen(rp, tpen(T_BORDER)); Move(rp, x + w - 1, top + 7); Draw(rp, x + w - 1, bot - 4); }
    x += w + 1;
  }
}
static int tab_hit(struct TData *d, LONG mx)
{
  int i;
  for (i = 0; i < ndocs + 2; i++) if (d->x0[i] >= 0 && mx >= d->x0[i] && mx <= d->x1[i]) return i;
  return -1;
}
static ULONG tabdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct TData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_Tabs_Pick || g->opg_AttrID == MUIA_Tabs_Close) { *g->opg_Storage = (ULONG)(LONG)((struct TData *)INST_DATA(cl, obj))->pick; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 100; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += fh() + 11; m->MinMaxInfo->DefHeight += fh() + 11; m->MinMaxInfo->MaxHeight += fh() + 11;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct TData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct TData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct TData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Tabs_Refresh:
    if (((struct TData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) tabs_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int k;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    k = inside(obj, im->MouseX, im->MouseY) ? tab_hit(d, im->MouseX) : -1;
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (k != d->hot) { d->hot = k; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN || k < 0) return 0;
    d->pick = k - 2;
    if (k >= 2 && im->MouseX >= d->x1[k] - 20) SetAttrs(obj, MUIA_Tabs_Close, d->pick, TAG_DONE);
    else SetAttrs(obj, MUIA_Tabs_Pick, d->pick, TAG_DONE);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *tabs_class(void)
{
  if (!tabcls) tabcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct TData), (APTR)tabdispatch);
  return tabcls;
}

/* ================================================================== the status bar */
struct StData { int shown; };
static void status_draw(Object *obj)
{
  struct RastPort *rp = _rp(obj);
  DocView *v = cur_view();
  char buf[160];
  LONG x = _mleft(obj) + 8, y = _mtop(obj) + 3;
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_STATUS));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj), _mtop(obj)); Draw(rp, _mright(obj), _mtop(obj));
  if (!v) {
    theme_text(rp, theme_font(), x, y, page_mode == PM_TOOLS ? "Choose a tool" : "Folio", tpen(T_DIM));
    return;
  }
  {
    double box[4];
    int rot;
    pdf_page_box(v->pdf, v->page, box, &rot);
    sprintf(buf, tr("Page %d of %d"), v->page + 1, v->pdf->npages);
    theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT));
    x += theme_textlen(rp, theme_font(), buf) + 16;
    SetAPen(rp, tpen(T_BORDER)); Move(rp, x - 8, y); Draw(rp, x - 8, y + fh());
    {
      double w = (box[2] - box[0]) * 25.4 / 72, h = (box[3] - box[1]) * 25.4 / 72;
      if (rot == 90 || rot == 270) { double t = w; w = h; h = t; }
      sprintf(buf, "%.0f x %.0f mm", w, h);
    }
    theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT));
    x += theme_textlen(rp, theme_font(), buf) + 16;
    SetAPen(rp, tpen(T_BORDER)); Move(rp, x - 8, y); Draw(rp, x - 8, y + fh());
    sprintf(buf, "%ld%%", (long)(v->zoom * 100 + 0.5));
    theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT));
    x += theme_textlen(rp, theme_font(), "0000%") + 16;
    SetAPen(rp, tpen(T_BORDER)); Move(rp, x - 8, y); Draw(rp, x - 8, y + fh());
  }
  {
    const char *info = view_info[0] ? view_info : tr(tool_names[tool]);
    text_fit(rp, theme_font(), x, y, info, _mright(obj) - x - 4, tpen(T_DIM));
  }
}
static ULONG stdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  switch (msg->MethodID) {
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 100; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += fh() + 6; m->MinMaxInfo->DefHeight += fh() + 6; m->MinMaxInfo->MaxHeight += fh() + 6;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct StData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct StData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Status_Refresh:
    if (((struct StData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) status_draw(obj);
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *status_class(void)
{
  if (!stcls) stcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct StData), (APTR)stdispatch);
  return stcls;
}

void bars_free(void)
{
  struct MUI_CustomClass **c[] = { &stripcls, &tabcls, &stcls };
  int i;
  for (i = 0; i < 3; i++) if (*c[i]) { MUI_DeleteCustomClass(*c[i]); *c[i] = NULL; }
}
