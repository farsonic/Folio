/*
 * panes.c - the panes beside Folio's page view and the pages that take its place, as PDFCraft
 * has them:
 *   nav    on the left: the pages' thumbnails (the one in view lit) or the bookmarks as a tree;
 *          a click goes there;
 *   pane   on the right: the tools as a list, or (commenting) every comment in the document with
 *          its author, date and text; a click goes to it;
 *   home   the Home page: open a file, the files opened lately, all the tools;
 *   grid   the Tools page: every tool as a card.
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
#include <proto/dos.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

int nav_page; double nav_top;
int pane_page, pane_index;
char recent[MAXRECENT][512];
int nrecent;

static int fh(void) { return theme_font()->tf_YSize; }
static struct MUI_CustomClass *navcls, *panecls, *homecls, *gridcls;
static int inside(Object *obj, LONG x, LONG y) { return x >= _mleft(obj) && x <= _mright(obj) && y >= _mtop(obj) && y <= _mbottom(obj); }
/* Latin-1 text wrapped to w pixels from y down, at most maxl lines; the lines used */
static int wrap_text(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, LONG w, const char *s, LONG pen, int maxl)
{
  int lines = 0;
  SetFont(rp, tf);
  while (*s && lines < maxl) {
    struct TextExtent te;
    ULONG fit = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)w, 100), cut = fit;
    const char *nl = strchr(s, '\n');
    if (nl && (ULONG)(nl - s) < cut) cut = (ULONG)(nl - s);
    else if (cut < strlen(s)) { ULONG k = cut; while (k > 0 && s[k] != ' ') k--; if (k > 0) cut = k; }
    if (!cut && !nl) cut = fit ? fit : 1;
    if (lines == maxl - 1 && cut < strlen(s)) {
      /* the last line room allows: cut short */
      text_fit(rp, tf, x, y, s, w, pen);
      return lines + 1;
    }
    SetAPen(rp, pen); SetDrMd(rp, JAM1);
    Move(rp, x, y + tf->tf_Baseline);
    Text(rp, (STRPTR)s, cut);
    s += cut;
    while (*s == ' ' || *s == '\n') s++;
    y += tf->tf_YSize + 1;
    lines++;
  }
  return lines;
}
static void accent_button(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, const char *label, int hot)
{
  LONG w = theme_textlen(rp, theme_bold(), label);
  theme_fill(rp, x0 + 1, y0, x1 - 1, y1, tpen(hot ? T_ACCENTHOVER : T_ACCENT));
  theme_fill(rp, x0, y0 + 1, x1, y1 - 1, tpen(hot ? T_ACCENTHOVER : T_ACCENT));
  theme_text(rp, theme_bold(), (x0 + x1 - w) / 2, (y0 + y1 - fh()) / 2 + 1, label, tpen(T_ONACCENT));
}

/* ================================================================== the navigation pane */
struct NData {
  struct MUI_EventHandlerNode ehn;
  struct MUI_InputHandlerNode ihn;
  Object *vprop;
  int shown, handlers, mode;
  LONG first;                          /* scrolled this many pixels */
  int lastpage; DocView *lastv;
  /* the bookmarks shown: each row's item and depth */
  POutline *rows[400]; UBYTE depth[400]; int nrows;
};
#define NHEAD (fh() + 10)
#define NCELL (THUMB_H + fh() + 14)
static LONG brow(void) { return fh() + 6; }
static void flatten(struct NData *d, POutline *o, int depth)
{
  for (; o && d->nrows < 400; o = o->next) {
    d->rows[d->nrows] = o; d->depth[d->nrows] = (UBYTE)(depth > 12 ? 12 : depth); d->nrows++;
    if (o->child && o->open) flatten(d, o->child, depth + 1);
  }
}
static LONG nav_total(struct NData *d)
{
  DocView *v = cur_view();
  if (!v) return 0;
  if (d->mode == 0) return (LONG)v->pdf->npages * NCELL + 8;
  return (LONG)d->nrows * brow() + 8;
}
static void nav_props(struct NData *d, Object *obj)
{
  LONG vis = _mheight(obj) - NHEAD, tot = nav_total(d);
  if (d->first > tot - vis) d->first = tot - vis;
  if (d->first < 0) d->first = 0;
  if (d->vprop) SetAttrs(d->vprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, tot > 0 ? tot : 1, MUIA_Prop_Visible, vis > 0 ? vis : 1, MUIA_Prop_First, d->first, TAG_DONE);
}
static void nav_prepare(struct NData *d)
{
  DocView *v = cur_view();
  d->nrows = 0;
  if (!v || d->mode != 1) return;
  if (!v->outline_done) { v->outline = pdf_outline(v->pdf); v->outline_done = 1; }
  flatten(d, v->outline, 0);
}
static void nav_draw(struct NData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  DocView *v = cur_view();
  LONG top = _mtop(obj) + NHEAD;
  APTR clip;
  int i;
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  theme_text(rp, theme_bold(), _mleft(obj) + 8, _mtop(obj) + 5, d->mode == 0 ? "Page Thumbnails" : "Bookmarks", tpen(T_TEXT));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj), top - 1); Draw(rp, _mright(obj), top - 1);
  Move(rp, _mright(obj), _mtop(obj)); Draw(rp, _mright(obj), _mbottom(obj));
  if (!v) return;
  nav_props(d, obj);
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), top, _mwidth(obj) - 1, _mbottom(obj) - top + 1);
  if (d->mode == 0) {
    for (i = (int)(d->first / NCELL); i < v->pdf->npages; i++) {
      LONG y = top + 4 + (LONG)i * NCELL - d->first, cx = (_mleft(obj) + _mright(obj)) / 2;
      Thumb *t;
      char num[12];
      if (y > _mbottom(obj)) break;
      t = thumb_get(v, i, 0);
      if (i == v->page) theme_fill(rp, _mleft(obj) + 4, y - 2, _mright(obj) - 5, y + NCELL - 6, tpen(T_SELECTION));
      if (t && t->pm.rgb) {
        LONG x = cx - t->pm.w / 2, ty = y + 2 + (THUMB_H - t->pm.h);
        draw_rgb(rp, _screen(obj), t->pm.rgb, t->pm.w * 3, x, ty, t->pm.w, t->pm.h);
        theme_rect(rp, x - 1, ty - 1, x + t->pm.w, ty + t->pm.h, tpen(i == v->page ? T_ACCENT : T_BORDER));
      } else {
        int w, h;
        double s;
        pdf_page_pixels(v->pdf, i, 1, 0, &w, &h);
        s = (double)THUMB_W / (w > 0 ? w : 1);
        if ((double)THUMB_H / (h > 0 ? h : 1) < s) s = (double)THUMB_H / (h > 0 ? h : 1);
        w = (int)(w * s); h = (int)(h * s);
        draw_rgb_box(rp, _screen(obj), cx - w / 2, y + 2 + THUMB_H - h, cx - w / 2 + w - 1, y + 1 + THUMB_H, 0xFFFFFF);
        theme_rect(rp, cx - w / 2 - 1, y + 1 + THUMB_H - h, cx - w / 2 + w, y + 2 + THUMB_H, tpen(T_BORDER));
      }
      sprintf(num, "%d", i + 1);
      theme_text(rp, i == v->page ? theme_bold() : theme_font(), cx - theme_textlen(rp, theme_font(), num) / 2, y + THUMB_H + 5, num, tpen(T_TEXT));
    }
  } else {
    if (!d->nrows) theme_text(rp, theme_font(), _mleft(obj) + 8, top + 8, "No bookmarks", tpen(T_DIM));
    for (i = (int)(d->first / brow()); i < d->nrows; i++) {
      LONG y = top + 4 + (LONG)i * brow() - d->first, x = _mleft(obj) + 6 + d->depth[i] * 10;
      POutline *o = d->rows[i];
      char t[200];
      if (y > _mbottom(obj)) break;
      if (o->child) {
        LONG cy = y + brow() / 2 - 1;
        SetAPen(rp, tpen(T_ICON));
        if (o->open) { Move(rp, x, cy - 2); Draw(rp, x + 6, cy - 2); Move(rp, x + 1, cy - 1); Draw(rp, x + 5, cy - 1); Move(rp, x + 2, cy); Draw(rp, x + 4, cy); WritePixel(rp, x + 3, cy + 1); }
        else { Move(rp, x + 1, cy - 3); Draw(rp, x + 1, cy + 3); Move(rp, x + 2, cy - 2); Draw(rp, x + 2, cy + 2); Move(rp, x + 3, cy - 1); Draw(rp, x + 3, cy + 1); WritePixel(rp, x + 4, cy); }
      }
      to_latin1(o->title ? o->title : "", t, sizeof t);
      text_fit(rp, o->page == v->page ? theme_bold() : theme_font(), x + 10, y + 3, t, _mright(obj) - x - 14, tpen(o->page >= 0 || o->uri ? T_TEXT : T_DIM));
    }
  }
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
/* the page in view kept in sight (thumbnails) */
static void nav_follow(struct NData *d, Object *obj)
{
  DocView *v = cur_view();
  LONG y, vis = _mheight(obj) - NHEAD;
  if (!v || d->mode != 0) return;
  y = (LONG)v->page * NCELL;
  if (y < d->first) d->first = y;
  else if (y + NCELL > d->first + vis) d->first = y + NCELL - vis;
}
static ULONG navdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct NData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->vprop = (Object *)GetTagData(MUIA_Nav_VProp, 0, ((struct opSet *)msg)->ops_AttrList);
    d->lastpage = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY;
    d->ihn.ihn_Object = obj; d->ihn.ihn_Flags = MUIIHNF_TIMER; d->ihn.ihn_Method = MUIM_Nav_Tick;
    d->ihn.ihn_stuff.ihn_timer.ihn_millis = 150;
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) {
      if (t->ti_Tag == MUIA_Nav_VProp) d->vprop = (Object *)t->ti_Data;
      else if (t->ti_Tag == MUIA_Nav_Top && d->first != (LONG)t->ti_Data) { d->first = (LONG)t->ti_Data; if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT); }
      else if (t->ti_Tag == MUIA_Nav_Mode && d->mode != (int)t->ti_Data) { d->mode = (int)t->ti_Data; d->first = 0; nav_prepare(d); if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT); }
    }
    break;
  }
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Nav_Go) { *((struct opGet *)msg)->opg_Storage = TRUE; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += THUMB_W + 20; m->MinMaxInfo->DefWidth += THUMB_W + 24; m->MinMaxInfo->MaxWidth += THUMB_W + 60;
    m->MinMaxInfo->MinHeight += 60; m->MinMaxInfo->DefHeight += 300; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    theme_setup(obj);
    theme_handler(obj, &d->ehn);
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->ihn);
    d->handlers = 1;
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    if (d->handlers) { DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->ihn); d->handlers = 0; }
    theme_cleanup(obj);
    break;
  case MUIM_Show: ((struct NData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct NData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) nav_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Nav_Refresh:
    d = INST_DATA(cl, obj);
    if (d->lastv != cur_view()) { d->first = 0; d->lastv = cur_view(); }
    nav_prepare(d);
    if (d->shown) { nav_follow(d, obj); MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return 0;
  case MUIM_Nav_Tick: {
    DocView *v = cur_view();
    int i;
    LONG top;
    d = INST_DATA(cl, obj);
    if (!d->shown || !v || d->mode != 0) return 0;
    top = _mtop(obj) + NHEAD;
    for (i = (int)(d->first / NCELL); i < v->pdf->npages; i++) {
      LONG y = top + 4 + (LONG)i * NCELL - d->first;
      Thumb *t;
      if (y > _mbottom(obj)) break;
      t = thumb_get(v, i, 0);
      if (t && t->pm.rgb) continue;
      thumb_get(v, i, 1);
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      break;
    }
    return 0;
  }
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    DocView *v = cur_view();
    LONG top;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || !v || !inside(obj, im->MouseX, im->MouseY)) return 0;
    top = _mtop(obj) + NHEAD;
    if (im->Class == IDCMP_RAWKEY && (im->Code == 0x7A || im->Code == 0x7B)) {
      d->first += im->Code == 0x7A ? -60 : 60;
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN || im->MouseY < top) return 0;
    if (d->mode == 0) {
      int i = (int)((im->MouseY - top - 4 + d->first) / NCELL);
      if (i >= 0 && i < v->pdf->npages) { nav_page = i; nav_top = -1; SetAttrs(obj, MUIA_Nav_Go, TRUE, TAG_DONE); }
    } else {
      int i = (int)((im->MouseY - top - 4 + d->first) / brow());
      if (i >= 0 && i < d->nrows) {
        POutline *o = d->rows[i];
        LONG x = _mleft(obj) + 6 + d->depth[i] * 10;
        if (o->child && im->MouseX < x + 10) { o->open = !o->open; nav_prepare(d); MUI_Redraw(obj, MADF_DRAWOBJECT); }
        else if (o->page >= 0) { nav_page = o->page; nav_top = o->top; SetAttrs(obj, MUIA_Nav_Go, TRUE, TAG_DONE); }
        else if (o->child) { o->open = !o->open; nav_prepare(d); MUI_Redraw(obj, MADF_DRAWOBJECT); }
      }
    }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *nav_class(void)
{
  if (!navcls) navcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct NData), (APTR)navdispatch);
  return navcls;
}

/* ================================================================== the tools pane */
typedef struct { ULONG id; int icon; const char *name; const char *help; } ToolDef;
static const ToolDef tooldefs[] = {
  { ID_COMMENTMODE, IC_F_COMMENTS, "Comment", "Add sticky notes, highlight, underline and strike out text, add text boxes, draw and add shapes." },
  { ID_ORGANIZE, IC_F_ORGANIZE, "Organize Pages", "See every page as a thumbnail: reorder, rotate, delete, insert and extract pages." },
  { ID_COMBINE, IC_F_COMBINE, "Combine Files", "Add the pages of another PDF to the end of this one." },
  { ID_GROUP0 + 10, IC_F_EXPORT, "Export PDF", "As pictures, text, a web page, RTF, Word or PowerPoint, or every picture it holds." },
  { ID_EXTRACT, IC_F_EXTRACT, "Extract Pages", "Save some of the pages as a new PDF file." },
  { ID_INSBLANK, IC_F_INSERT, "Insert Blank Page", "Add an empty page after the one in view." },
  { ID_FIND, IC_F_FIND, "Find Text", "Search the document for words." },
  { ID_PROPS, IC_F_PROPS, "Properties", "The document's title, author, subject and keywords, and what it is made of." },
  /* (after 0.1.2) PDFCraft's other tool groups: each offers its tools in a list */
  { ID_GROUP0 + 0, IC_F_TEXTBOX, "Edit a PDF", "Add text, pictures, links, headers and footers, watermarks, backgrounds, Bates numbers and attachments." },
  { ID_GROUP0 + 2, IC_F_INK, "Fill & Sign", "Type into a form without fields, tick, cross, date and sign." },
  { ID_GROUP0 + 3, IC_F_RECT, "Prepare a Form", "Add form fields, find them, set their properties and tab order, clear, flatten, import and export data." },
  { ID_GROUP0 + 4, IC_F_PROPS, "Protect a PDF", "Password protection with permissions, hidden information, security." },
  { ID_GROUP0 + 5, IC_F_DELETE, "Redact a PDF", "Mark areas, pages or text found, then remove what is under them for good." },
  { ID_GROUP0 + 6, IC_F_EXTRACT, "Compress a PDF", "Make the file smaller: pictures at a lower resolution, streams compressed, objects packed." },
  { ID_GROUP0 + 1, IC_F_BLANK, "Create a PDF", "A blank document, or one from pictures, a text file or the clipboard." },
  { ID_GROUP0 + 11, IC_F_ORGANIZE, "More Page Tools", "Replace, split, duplicate, reverse and crop pages; page boxes and labels." },
  { ID_STAMPPICK, IC_F_NOTE, "Add a Stamp", "Approved, Draft, Confidential and other stamps, or your own text." },
  { ID_GROUP0 + 7, IC_F_LINE, "Measure", "Distances, perimeters and areas at a drawing scale." },
  { ID_GROUP0 + 8, IC_F_PROPS, "Accessibility", "Tag the document, check it, reading options and alternate text." },
  { ID_GROUP0 + 9, IC_F_EXPORT, "Print Production", "Hairlines, grey, printer's marks and page boxes." },
  { ID_GROUP0 + 12, IC_F_FIND, "Compare, Sign, Standards", "Compare two versions, sign with a certificate and check signatures, save as PDF/A." },
  { ID_GROUP0 + 13, IC_F_EXPORT, "Scan & OCR", "Recognise the text of scanned pages, straighten and clean scans, make them smaller." },
};
#define NTOOLDEFS (int)(sizeof tooldefs / sizeof tooldefs[0])
typedef struct { int page, index, kind; char who[48], date[24], text[160]; } CRow;
struct PData {
  struct MUI_EventHandlerNode ehn;
  int shown, hot, first;
  CRow *rows; int nrows, caprows;
};
extern int comment_mode;
static LONG trow(void) { return fh() + 12; }
static LONG crow(void) { return fh() * 3 + 12; }
#define PHEAD (fh() + 12)
static void pane_collect(struct PData *d)
{
  DocView *v = cur_view();
  int p;
  d->nrows = 0;
  if (!v || !comment_mode) return;
  for (p = 0; p < v->pdf->npages; p++) {
    PAnnot *an;
    int n = pdf_annots(v->pdf, p, &an), k;
    for (k = 0; k < n; k++) {
      CRow *r;
      if (an[k].kind == AN_WIDGET || an[k].kind == AN_OTHER) continue;
      if (d->nrows >= d->caprows) {
        int nc = d->caprows ? d->caprows * 2 : 32;
        CRow *n2 = (CRow *)realloc(d->rows, (size_t)nc * sizeof(CRow));
        if (!n2) break;
        d->rows = n2; d->caprows = nc;
      }
      r = &d->rows[d->nrows++];
      r->page = p; r->index = an[k].index; r->kind = an[k].kind;
      to_latin1(an[k].author ? an[k].author : "", r->who, sizeof r->who);
      to_latin1(an[k].date ? an[k].date : "", r->date, sizeof r->date);
      to_latin1(an[k].contents ? an[k].contents : "", r->text, sizeof r->text);
    }
    pdf_annots_free(an, n);
  }
}
static int kind_icon(int k)
{
  switch (k) {
  case AN_HIGHLIGHT: return IC_F_HIGHLIGHT; case AN_UNDERLINE: return IC_F_UNDERLINE; case AN_STRIKE: return IC_F_STRIKE;
  case AN_SQUIGGLY: return IC_F_SQUIGGLY; case AN_NOTE: return IC_F_NOTE; case AN_FREETEXT: return IC_F_TEXTBOX;
  case AN_INK: return IC_F_INK; case AN_SQUARE: return IC_F_RECT; case AN_CIRCLE: return IC_F_OVAL; case AN_LINE: return IC_F_LINE;
  }
  return IC_F_COMMENTS;
}
static const char *kind_name(int k)
{
  switch (k) {
  case AN_HIGHLIGHT: return "Highlight"; case AN_UNDERLINE: return "Underline"; case AN_STRIKE: return "Strikethrough";
  case AN_SQUIGGLY: return "Squiggly"; case AN_NOTE: return "Sticky Note"; case AN_FREETEXT: return "Text Box";
  case AN_INK: return "Drawing"; case AN_SQUARE: return "Rectangle"; case AN_CIRCLE: return "Oval"; case AN_LINE: return "Line";
  case AN_STAMP: return "Stamp";
  }
  return "Comment";
}
static void pane_draw(struct PData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  DocView *v = cur_view();
  LONG x0 = _mleft(obj), x1 = _mright(obj), top = _mtop(obj) + PHEAD;
  APTR clip;
  int i;
  char buf[200];
  theme_fill(rp, x0, _mtop(obj), x1, _mbottom(obj), tpen(T_PANEL));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, x0, _mtop(obj)); Draw(rp, x0, _mbottom(obj));
  Move(rp, x0, top - 1); Draw(rp, x1, top - 1);
  if (!comment_mode) {
    theme_text(rp, theme_bold(), x0 + 8, _mtop(obj) + 6, "Tools", tpen(T_TEXT));
    for (i = 0; i < NTOOLDEFS; i++) {
      LONG y = top + 2 + i * trow();
      ULONG st = strip_state(tooldefs[i].id);
      if (y + trow() > _mbottom(obj)) break;
      if (i == d->hot && !(st & SS_DISABLED)) theme_fill(rp, x0 + 2, y, x1 - 2, y + trow() - 2, tpen(T_HOVER));
      theme_icon(rp, tooldefs[i].icon, x0 + 8, y + (trow() - 16) / 2 - 1, 16, (st & SS_DISABLED) != 0);
      text_fit(rp, theme_font(), x0 + 30, y + (trow() - fh()) / 2 - 1, tr(tooldefs[i].name), x1 - x0 - 34, tpen(st & SS_DISABLED ? T_DISABLED : T_TEXT));
    }
    return;
  }
  sprintf(buf, "%s (%d)", tr("Comments"), d->nrows);
  theme_text(rp, theme_bold(), x0 + 8, _mtop(obj) + 6, buf, tpen(T_TEXT));
  theme_icon(rp, IC_F_CLOSE, x1 - 18, _mtop(obj) + 5, 12, 0);
  if (!v) return;
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), x0 + 1, top, x1 - x0, _mbottom(obj) - top + 1);
  if (!d->nrows) wrap_text(rp, theme_font(), x0 + 8, top + 8, x1 - x0 - 16, tr("No comments yet. Choose a tool above and click or drag on the page."), tpen(T_DIM), 6);
  if (d->first > d->nrows - 1) d->first = d->nrows > 0 ? d->nrows - 1 : 0;
  for (i = d->first; i < d->nrows; i++) {
    CRow *r = &d->rows[i];
    LONG y = top + 2 + (i - d->first) * crow();
    int chosen = v->an_page == r->page && v->an_index == r->index;
    if (y > _mbottom(obj)) break;
    theme_fill(rp, x0 + 4, y, x1 - 4, y + crow() - 4, tpen(chosen ? T_SELECTION : T_WHITE));
    theme_rect(rp, x0 + 4, y, x1 - 4, y + crow() - 4, tpen(chosen ? T_ACCENT : T_BORDER));
    theme_icon(rp, kind_icon(r->kind), x0 + 8, y + 3, 14, 0);
    text_fit(rp, theme_bold(), x0 + 26, y + 3, r->who[0] ? r->who : tr(kind_name(r->kind)), x1 - x0 - 70, tpen(T_TEXT));
    sprintf(buf, tr("p. %d"), r->page + 1);
    theme_text(rp, theme_font(), x1 - 8 - theme_textlen(rp, theme_font(), buf), y + 3, buf, tpen(T_DIM));
    text_fit(rp, theme_font(), x0 + 10, y + 4 + fh(), r->date[0] ? r->date : tr(kind_name(r->kind)), x1 - x0 - 20, tpen(T_DIM));
    text_fit(rp, theme_font(), x0 + 10, y + 5 + fh() * 2, r->text[0] ? r->text : tr(kind_name(r->kind)), x1 - x0 - 20, tpen(r->text[0] ? T_TEXT : T_DIM));
  }
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
static int pane_hit(struct PData *d, Object *obj, LONG mx, LONG my)
{
  LONG top = _mtop(obj) + PHEAD;
  if (!inside(obj, mx, my) || my < top) return -1;
  if (!comment_mode) { int i = (int)((my - top - 2) / trow()); return i >= 0 && i < NTOOLDEFS ? i : -1; }
  { int i = d->first + (int)((my - top - 2) / crow()); return i >= 0 && i < d->nrows ? i : -1; }
}
static ULONG panedispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case OM_DISPOSE:
    free(((struct PData *)INST_DATA(cl, obj))->rows);
    break;
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Pane_Pick) { *((struct opGet *)msg)->opg_Storage = 0; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 130; m->MinMaxInfo->DefWidth += 140; m->MinMaxInfo->MaxWidth += 200;
    m->MinMaxInfo->MinHeight += 60; m->MinMaxInfo->DefHeight += 300; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct PData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct PData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct PData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) pane_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Pane_Refresh:
    d = INST_DATA(cl, obj);
    pane_collect(d);
    if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int k;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    k = pane_hit(d, obj, im->MouseX, im->MouseY);
    if (im->Class == IDCMP_MOUSEMOVE) { if (!comment_mode && k != d->hot) { d->hot = k; MUI_Redraw(obj, MADF_DRAWOBJECT); } return 0; }
    if (!inside(obj, im->MouseX, im->MouseY)) return 0;
    if (im->Class == IDCMP_RAWKEY && (im->Code == 0x7A || im->Code == 0x7B)) {
      d->first += im->Code == 0x7A ? -1 : 1;
      if (d->first < 0) d->first = 0;
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
    if (comment_mode && im->MouseY < _mtop(obj) + PHEAD && im->MouseX > _mright(obj) - 24) { SetAttrs(obj, MUIA_Pane_Pick, ID_DONE, TAG_DONE); return MUI_EventHandlerRC_Eat; }
    if (k < 0) return 0;
    if (!comment_mode) { if (!(strip_state(tooldefs[k].id) & SS_DISABLED)) SetAttrs(obj, MUIA_Pane_Pick, tooldefs[k].id, TAG_DONE); }
    else { pane_page = d->rows[k].page; pane_index = d->rows[k].index; SetAttrs(obj, MUIA_Pane_Pick, ID_COMMENTPICK, TAG_DONE); }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *pane_class(void)
{
  if (!panecls) panecls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PData), (APTR)panedispatch);
  return panecls;
}

/* ================================================================== the Home page */
struct HData { struct MUI_EventHandlerNode ehn; int shown, hot; LONG bx0, by0, bx1, by1, ry, lx0, lx1, ly, lh; };
/* the parts' places: the Open button, the recent list, the link to the tools */
static void home_geo(struct HData *d, Object *obj)
{
  LONG l = _mleft(obj) + 24;
  d->bx0 = l; d->by0 = _mtop(obj) + 26 + theme_big()->tf_YSize + fh() + 18; d->bx1 = l + 150; d->by1 = d->by0 + fh() + 14;
  d->ry = d->by1 + 26 + fh() + 8;
  d->lh = fh() * 2 + 10;
  d->lx0 = l; d->lx1 = _mright(obj) - 24;
}
static void home_draw(struct HData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj) + 24;
  int i;
  home_geo(d, obj);
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  theme_text(rp, theme_big(), l, _mtop(obj) + 20, "Welcome to Folio", tpen(T_TEXT));
  theme_text(rp, theme_font(), l, _mtop(obj) + 24 + theme_big()->tf_YSize, "Read, comment on and organize PDF documents.", tpen(T_DIM));
  accent_button(rp, d->bx0, d->by0, d->bx1, d->by1, tr("Open File..."), d->hot == 100);
  {
    LONG tx = d->bx1 + 16, w = theme_textlen(rp, theme_font(), "See all tools");
    theme_text(rp, theme_font(), tx, (d->by0 + d->by1 - fh()) / 2 + 1, "See all tools", tpen(T_ACCENT));
    if (d->hot == 101) { SetAPen(rp, tpen(T_ACCENT)); Move(rp, tx, (d->by0 + d->by1 + fh()) / 2 + 1); Draw(rp, tx + w - 1, (d->by0 + d->by1 + fh()) / 2 + 1); }
  }
  theme_text(rp, theme_bold(), l, d->ry - fh() - 8, "Recent files", tpen(T_TEXT));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, d->ry - 3); Draw(rp, d->lx1, d->ry - 3);
  if (!nrecent) theme_text(rp, theme_font(), l, d->ry + 6, "The files you open will be listed here.", tpen(T_DIM));
  for (i = 0; i < nrecent; i++) {
    LONG y = d->ry + i * d->lh;
    char dir[512];
    if (y + d->lh > _mbottom(obj)) break;
    if (i == d->hot) theme_fill(rp, l, y, d->lx1, y + d->lh - 2, tpen(T_HOVER));
    theme_icon(rp, IC_F_PDF, l + 6, y + (d->lh - 20) / 2, 20, 0);
    text_fit(rp, theme_bold(), l + 34, y + 4, (const char *)FilePart((STRPTR)recent[i]), d->lx1 - l - 40, tpen(T_TEXT));
    strcpy(dir, recent[i]);
    *PathPart((STRPTR)dir) = 0;
    text_fit(rp, theme_font(), l + 34, y + 5 + fh(), dir, d->lx1 - l - 40, tpen(T_DIM));
    SetAPen(rp, tpen(T_BORDER)); Move(rp, l, y + d->lh - 1); Draw(rp, d->lx1, y + d->lh - 1);
  }
}
static int home_hit(struct HData *d, Object *obj, LONG mx, LONG my)
{
  home_geo(d, obj);
  if (mx >= d->bx0 && mx <= d->bx1 && my >= d->by0 && my <= d->by1) return 100;
  if (my >= d->by0 && my <= d->by1 && mx > d->bx1 + 10 && mx < d->bx1 + 30 + theme_textlen(_rp(obj), theme_font(), "See all tools")) return 101;
  if (mx >= d->lx0 && mx <= d->lx1 && my >= d->ry) { int i = (int)((my - d->ry) / d->lh); if (i < nrecent) return i; }
  return -1;
}
static ULONG homedispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct HData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Home_Pick) { *((struct opGet *)msg)->opg_Storage = 0; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 600; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 100; m->MinMaxInfo->DefHeight += 400; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct HData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct HData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct HData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) home_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Home_Refresh:
    if (((struct HData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int k;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    k = inside(obj, im->MouseX, im->MouseY) ? home_hit(d, obj, im->MouseX, im->MouseY) : -1;
    if (im->Class == IDCMP_MOUSEMOVE) { if (k != d->hot) { d->hot = k; MUI_Redraw(obj, MADF_DRAWOBJECT); } return 0; }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN || k < 0) return 0;
    SetAttrs(obj, MUIA_Home_Pick, k == 100 ? ID_OPEN : k == 101 ? ID_TOOLS : ID_RECENT0 + k, TAG_DONE);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *home_class(void)
{
  if (!homecls) homecls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct HData), (APTR)homedispatch);
  return homecls;
}

/* ================================================================== the Tools page */
struct GData { struct MUI_EventHandlerNode ehn; int shown, hot; };
#define GCOLS 3
/* (after 0.1.2) the cards shrink to fit the page: shorter (their description cut, then left out)
   and, when wide enough, four to a row, so every tool shows on a 640 x 480 screen */
static void card_box(Object *obj, int i, LONG *r)
{
  LONG top = _mtop(obj) + 22 + theme_big()->tf_YSize, avail = _mbottom(obj) - top, l = _mleft(obj) + 16;
  int cols = _mwidth(obj) >= 760 ? 4 : GCOLS, rows = (NTOOLDEFS + cols - 1) / cols;
  LONG gap = 12, w = (_mwidth(obj) - 32 - (cols - 1) * gap) / cols, h = fh() * 4 + 44;
  if (rows * (h + gap) > avail) { gap = 6; h = avail / rows - gap; }
  if (h < fh() + 22) h = fh() + 22;
  r[0] = l + (i % cols) * (w + gap); r[1] = top + (i / cols) * (h + gap); r[2] = r[0] + w - 1; r[3] = r[1] + h - 1;
}
static void grid_draw(struct GData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  int i;
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_CANVAS));
  theme_text(rp, theme_big(), _mleft(obj) + 16, _mtop(obj) + 10, "All tools", tpen(T_TEXT));
  for (i = 0; i < NTOOLDEFS; i++) {
    LONG r[4];
    ULONG st = strip_state(tooldefs[i].id);
    card_box(obj, i, r);
    if (r[3] > _mbottom(obj)) break;
    theme_fill(rp, r[0], r[1], r[2], r[3], tpen(i == d->hot ? T_HOVER : T_PANEL));
    theme_rect(rp, r[0], r[1], r[2], r[3], tpen(i == d->hot ? T_ACCENT : T_BORDER));
    theme_icon(rp, tooldefs[i].icon, r[0] + 10, r[1] + 10, 24, 0);
    text_fit(rp, theme_bold(), r[0] + 42, r[1] + 10 + (24 - fh()) / 2, tr(tooldefs[i].name), r[2] - r[0] - 50, tpen(T_TEXT));
    if (r[3] - r[1] >= fh() + 44) wrap_text(rp, theme_font(), r[0] + 10, r[1] + 42, r[2] - r[0] - 20, tr(tooldefs[i].help), tpen(T_DIM), (int)((r[3] - r[1] - 44) / fh()) < 3 ? (int)((r[3] - r[1] - 44) / fh()) : 3);
    (void)st;
  }
}
static int grid_hit(Object *obj, LONG mx, LONG my)
{
  int i;
  for (i = 0; i < NTOOLDEFS; i++) {
    LONG r[4];
    card_box(obj, i, r);
    if (mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3]) return i;
  }
  return -1;
}
static ULONG griddispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct GData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Grid_Pick) { *((struct opGet *)msg)->opg_Storage = 0; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 300; m->MinMaxInfo->DefWidth += 600; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 100; m->MinMaxInfo->DefHeight += 400; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct GData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct GData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct GData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) grid_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int k;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    k = inside(obj, im->MouseX, im->MouseY) ? grid_hit(obj, im->MouseX, im->MouseY) : -1;
    if (im->Class == IDCMP_MOUSEMOVE) { if (k != d->hot) { d->hot = k; MUI_Redraw(obj, MADF_DRAWOBJECT); } return 0; }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN || k < 0) return 0;
    SetAttrs(obj, MUIA_Grid_Pick, tooldefs[k].id, TAG_DONE);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *grid_class(void)
{
  if (!gridcls) gridcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct GData), (APTR)griddispatch);
  return gridcls;
}

void panes_free(void)
{
  struct MUI_CustomClass **c[] = { &navcls, &panecls, &homecls, &gridcls };
  int i;
  for (i = 0; i < 4; i++) if (*c[i]) { MUI_DeleteCustomClass(*c[i]); *c[i] = NULL; }
}
