/*
 * signpad.c - (round 2) Fill & Sign's drawn signature and initials: a white pad to write on with
 * the mouse, in a dialog with Clear, OK and Cancel. The strokes are kept as text (core/pagetools.c
 * ink_format: points 0-1000 across and down the pad) in Folio.prefs, and drawn onto the page as
 * an ink annotation fitted into the box clicked (annot_ink_fit).
 */
#include <string.h>
#include <stdlib.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/pagetools.h"

#define PADMAX 1500
#define PADSTROKES 40
struct PadData {
  struct MUI_EventHandlerNode ehn;
  int handler, down;
  double pts[PADMAX * 2]; int npts[PADSTROKES]; int ns, n;
};
static struct MUI_CustomClass *pcls;
#define MUIM_Pad_Clear (QTAG + 0x780)

static void pad_line(struct RastPort *rp, Object *obj, const double *a, const double *b)
{
  LONG w = _mwidth(obj) - 1, h = _mheight(obj) - 1;
  Move(rp, _mleft(obj) + (LONG)(a[0] * w), _mtop(obj) + (LONG)(a[1] * h));
  Draw(rp, _mleft(obj) + (LONG)(b[0] * w), _mtop(obj) + (LONG)(b[1] * h));
}
static void pad_draw(struct PadData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  int s, k = 0, j;
  SetAPen(rp, 2);                                   /* (white in the Workbench pens) */
  RectFill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
  SetAPen(rp, 1);
  /* the line to sign on */
  Move(rp, _mleft(obj) + 8, _mbottom(obj) - _mheight(obj) / 4); Draw(rp, _mright(obj) - 8, _mbottom(obj) - _mheight(obj) / 4);
  for (s = 0; s < d->ns; s++) {
    for (j = 1; j < d->npts[s]; j++) pad_line(rp, obj, &d->pts[(k + j - 1) * 2], &d->pts[(k + j) * 2]);
    k += d->npts[s];
  }
}
static ULONG pdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PadData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    return (ULONG)obj;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 240; m->MinMaxInfo->DefWidth += 400; m->MinMaxInfo->MaxWidth += 560;
    m->MinMaxInfo->MinHeight += 80; m->MinMaxInfo->DefHeight += 130; m->MinMaxInfo->MaxHeight += 200;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
    d->handler = 1;
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    if (d->handler) { DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn); d->handler = 0; }
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) pad_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Pad_Clear:
    d = INST_DATA(cl, obj);
    d->ns = d->n = 0;
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    LONG mx, my;
    double x, y;
    d = INST_DATA(cl, obj);
    if (!im) return 0;
    mx = im->MouseX; my = im->MouseY;
    x = (double)(mx - _mleft(obj)) / (_mwidth(obj) - 1); y = (double)(my - _mtop(obj)) / (_mheight(obj) - 1);
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    if (y < 0) y = 0;
    if (y > 1) y = 1;
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) {
      if (mx < _mleft(obj) || mx > _mright(obj) || my < _mtop(obj) || my > _mbottom(obj) || d->ns >= PADSTROKES || d->n >= PADMAX) return 0;
      d->down = 1;
      d->npts[d->ns++] = 1;
      d->pts[d->n * 2] = x; d->pts[d->n * 2 + 1] = y; d->n++;
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP && d->down) { d->down = 0; return MUI_EventHandlerRC_Eat; }
    if (im->Class == IDCMP_MOUSEMOVE && d->down && d->n < PADMAX) {
      double *last = &d->pts[(d->n - 1) * 2];
      if (((x - last[0]) * (_mwidth(obj)) * (x - last[0]) * (_mwidth(obj)) + (y - last[1]) * (_mheight(obj)) * (y - last[1]) * (_mheight(obj))) < 4) return MUI_EventHandlerRC_Eat;
      d->pts[d->n * 2] = x; d->pts[d->n * 2 + 1] = y; d->n++;
      d->npts[d->ns - 1]++;
      {
        struct RastPort *rp = _rp(obj);
        SetAPen(rp, 1);
        pad_line(rp, obj, &d->pts[(d->n - 2) * 2], &d->pts[(d->n - 1) * 2]);
      }
      return MUI_EventHandlerRC_Eat;
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
void signpad_class_free(void) { if (pcls) { MUI_DeleteCustomClass(pcls); pcls = NULL; } }
/* the pad in a dialog: the strokes drawn, as text, into out (cap bytes); 1 if OK was chosen with
   something drawn */
int signpad_dialog(const char *title, char *out, int cap)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *pad, *clear, *w;
  int ok = 0;
  if (!pcls && !(pcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PadData), (APTR)pdispatch))) return 0;
  w = dialog_window(title, VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Draw with the mouse, holding the left button down."), End,
        Child, pad = (Object *)NewObject(pcls->mcc_Class, NULL, MUIA_Frame, MUIV_Frame_Text, TAG_DONE),
        Child, HGroup, Child, clear = QButton(tr("Clear")), Child, HSpace(0), End,
      End, "OK");
  if (!w) return 0;
  DoMethod(clear, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)pad, 1, MUIM_Pad_Clear);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    struct PadData *d = INST_DATA(pcls->mcc_Class, pad);
    ok = d->ns > 0 && ink_format(d->pts, d->npts, d->ns, out, cap);
  }
  MUI_DisposeObject(w);
  return ok;
}
