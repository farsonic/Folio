/*
 * doctools.c - PDFCraft's tools beyond reading, commenting and organizing (after Folio 0.1.2):
 * forms (filling in by clicking a field, preparing, data), Fill & Sign, page content (headers and
 * footers, watermarks, backgrounds, Bates numbers, text, pictures, links), attachments, bookmarks,
 * page labels, boxes and cropping, protection (passwords, hidden information, redaction),
 * optimizing, creating and exporting, comparing, accessibility, signatures, print production
 * and PDF/A. The work is done by the core (core/forms.c, pagetools.c, redact.c, optimize.c,
 * export.c, review.c); this file asks what to do and shows what happened.
 *
 * Changes the comment undo can't take back step by step are undone by a snapshot: the document
 * saved in memory before the change (the last three such changes are kept) and opened again by
 * Undo.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/pdfwrite.h"
#include "../core/pagecontent.h"
#include "../core/forms.h"
#include "../core/pagetools.h"
#include "../core/redact.h"
#include "../core/optimize.h"
#include "../core/export.h"
#include "../core/review.h"

extern int comment_mode;
static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }

/* ------------------------------------------------------------------ small helpers */
/* Latin-1 (typed in a dialog) as UTF-8, malloc'd */
static char *u8of(const char *lat)
{
  size_t n = strlen(lat), i, o = 0;
  char *u = (char *)malloc(n * 2 + 1);
  if (!u) return NULL;
  for (i = 0; i < n; i++) o += (size_t)utf8_put(u + o, (u8)lat[i]);
  u[o] = 0;
  return u;
}
static const char *author(void) { static char u[200]; char *s = u8of(opt.author); u[0] = 0; if (s) { strncpy(u, s, sizeof u - 1); u[sizeof u - 1] = 0; free(s); } return u; }
static void say(const char *msg) { ask("OK", "%s", tr(msg)); }
static void sayf(const char *fmt, long n) { char m[300]; snprintf(m, sizeof m, tr(fmt), n); ask("OK", "%s", m); }
static int yes(const char *msg) { return ask("Yes|No", "%s", tr(msg)) == 1; }
static void busy(int on) { set(app, MUIA_Application_Sleep, on ? TRUE : FALSE); }

void doc_redraw_all(DocView *v)
{
  int i;
  DoMethod(view, MUIM_View_Forget, (ULONG)v, 0);
  for (i = 0; i < v->ntexts; i++) textpage_free(&v->texts[i].tp);
  v->ntexts = 0;
  for (i = 0; i < v->nthumbs; i++) free(v->thumbs[i].pm.rgb);
  v->nthumbs = 0;
  if (v->outline) { pdf_outline_free(v->outline); v->outline = NULL; }
  v->outline_done = 0;
  doc_pages_changed(v);
  doc_changed();
}

/* ------------------------------------------------------------------ undo by snapshot */
#define MAXSNAP 3
void doc_snapshot(DocView *v)
{
  size_t len;
  u8 *data;
  int i, n = 0;
  busy(1);
  data = pdf_save(v->pdf, NULL, 0, &len);
  busy(0);
  if (!data) return;
  /* the oldest snapshots forgotten (their steps stay, doing nothing) */
  for (i = v->nundo - 1; i >= 0; i--) if (v->undo[i].kind == U_SNAPSHOT && v->undo[i].text) {
    if (++n >= MAXSNAP) { free(v->undo[i].text); v->undo[i].text = NULL; }
  }
  undo_push(v, U_SNAPSHOT, v->page, 0, 0, 0, NULL, NULL);
  if (v->nundo && v->undo[v->nundo - 1].kind == U_SNAPSHOT) { v->undo[v->nundo - 1].text = (char *)data; v->undo[v->nundo - 1].a = (int)len; }
  else free(data);
}
void doc_restore(DocView *v, Undo *u)
{
  char err[160];
  PDoc *d;
  if (!u->text) { say("That change is too far back to undo."); return; }
  busy(1);
  d = pdf_open((const u8 *)u->text, (size_t)u->a, err, sizeof err);
  busy(0);
  if (!d) return;
  DoMethod(view, MUIM_View_Forget, (ULONG)v, 0);
  if (v->rc) rcache_free(v->rc);
  pdf_close(v->pdf);
  v->pdf = d;
  v->rc = rcache_new(d);
  d->changed = 1;
  /* (the steps before it refer to the old document's objects) */
  {
    int i;
    for (i = 0; i < v->nundo; i++) if (&v->undo[i] != u) { free(v->undo[i].text); v->undo[i].text = NULL; }
    v->nundo = 0;
  }
  doc_redraw_all(v);
}

/* ------------------------------------------------------------------ dialogs */
enum { DF_STR, DF_CYCLE, DF_CHECK, DF_INFO };
typedef struct { const char *label; int kind; char *buf; int cap; const char *const *entries; int *val; Object *o; } DField;
static Object *cycle_of(const char *const *e, int active, const char ***keep)
{
  static const char *tre[8][17];
  static int slot;
  const char **t = tre[slot++ & 7];
  int i;
  for (i = 0; e[i] && i < 16; i++) t[i] = tr(e[i]);
  t[i] = NULL;
  *keep = t;
  return CycleObject, MUIA_Cycle_Entries, (ULONG)t, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End;
}
/* a dialog of labelled fields; 1 if OK was chosen (the fields then hold what was entered) */
static int dform(const char *title, DField *f, int n, const char *oklabel)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *grp = ColGroup(2), End, *w;
  int i, ok;
  if (!grp) return 0;
  for (i = 0; i < n; i++) {
    const char **keep;
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(f[i].label), f[i].kind == DF_STR || f[i].kind == DF_CYCLE ? MUIO_Label_SingleFrame : 0));
    switch (f[i].kind) {
    case DF_STR: f[i].o = StringObject, StringFrame, MUIA_String_Contents, (ULONG)f[i].buf, MUIA_String_MaxLen, f[i].cap, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMM", End; break;
    case DF_CYCLE: f[i].o = cycle_of(f[i].entries, *f[i].val, &keep); break;
    case DF_CHECK: f[i].o = MUI_MakeObject(MUIO_Checkmark, NULL); break;
    default: f[i].o = TextObject, MUIA_Text_Contents, (ULONG)f[i].buf, MUIA_Text_SetMin, FALSE, End;
    }
    if (f[i].kind == DF_CHECK) {
      Object *g = HGroup, Child, f[i].o, Child, HSpace(0), End;
      set(f[i].o, MUIA_Selected, *f[i].val ? TRUE : FALSE);
      set(f[i].o, MUIA_CycleChain, 1);
      DoMethod(grp, OM_ADDMEMBER, (ULONG)g);
    } else DoMethod(grp, OM_ADDMEMBER, (ULONG)f[i].o);
  }
  if (!(w = dialog_window(title, grp, oklabel))) return 0;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) for (i = 0; i < n; i++) {
    if (f[i].kind == DF_STR) { strncpy(f[i].buf, (const char *)xget(f[i].o, MUIA_String_Contents), (size_t)f[i].cap - 1); f[i].buf[f[i].cap - 1] = 0; }
    else if (f[i].kind == DF_CYCLE) *f[i].val = (int)xget(f[i].o, MUIA_Cycle_Active);
    else if (f[i].kind == DF_CHECK) *f[i].val = xget(f[i].o, MUIA_Selected) ? 1 : 0;
  }
  MUI_DisposeObject(w);
  return ok;
}
/* a list to choose from (Latin-1 lines); the index chosen, -1 cancelled. buttons: an extra
   choice's label (e.g. "Delete"): *alt set when it was used */
static int dlist(const char *title, char **items, int n, int cur, const char *alt_label, int *alt)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLGCHANGE, 0 };
  Object *lv, *lst, *w, *ab = NULL, *grp;
  int i, r = -1;
  ULONG id;
  grp = VGroup,
    Child, lv = ListviewObject, MUIA_Listview_List, lst = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
           MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
    Child, HGroup, Child, HSpace(0), Child, ab = alt_label ? QButton(tr(alt_label)) : HSpace(0), End,
  End;
  if (!grp) return -1;
  for (i = 0; i < n; i++) DoMethod(lst, MUIM_List_InsertSingle, (ULONG)items[i], MUIV_List_Insert_Bottom);
  if (cur >= 0 && cur < n) set(lst, MUIA_List_Active, cur);
  set(lv, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n");
  if (!(w = dialog_window(title, grp, "OK"))) return -1;
  DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  if (alt_label && ab) DoMethod(ab, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
  id = run_modal(w, ends, NULL);
  if (id == ID_DLGOK || id == ID_DLGCHANGE) r = (int)(LONG)xget(lst, MUIA_List_Active);
  if (alt) *alt = id == ID_DLGCHANGE;
  MUI_DisposeObject(w);
  return r >= 0 && r < n ? r : -1;
}
/* true if s is well-formed UTF-8 (text in Latin-1 with letters past ASCII almost never is) */
static int is_utf8(const char *s)
{
  const u8 *p = (const u8 *)s;
  while (*p) {
    int k = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if (k < 0) return 0;
    for (p++; k > 0; k--, p++) if ((*p & 0xC0) != 0x80) return 0;
  }
  return 1;
}
/* text to read: UTF-8 (from the document or the core), or Latin-1 (the interface's translated words) */
static void dshow(const char *title, const char *utf8)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  size_t n = utf8 ? strlen(utf8) : 0;
  char *lat = (char *)malloc(n * 3 + 4);
  Object *w;
  if (!lat) return;
  if (utf8 && !is_utf8(utf8)) memcpy(lat, utf8, n + 1);
  else utf8_to_latin1(utf8 ? utf8 : "", n, lat, n * 3 + 4);
  w = dialog_window(title, ListviewObject, MUIA_Listview_Input, FALSE, MUIA_Listview_List, FloattextObject, ReadListFrame,
                    MUIA_Floattext_Text, (ULONG)lat, End, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n\n\n", End, "OK");
  if (w) { run_modal(w, ends, NULL); MUI_DisposeObject(w); }
  free(lat);
}
/* "3-5" (or "all", or empty) as a range of pages, 0-based; 0 if it can't be read */
static int page_range(DocView *v, const char *s, int *first, int *last)
{
  int a, b;
  *first = 0; *last = v->pdf->npages - 1;
  while (*s == ' ') s++;
  if (!*s || !Stricmp((STRPTR)s, (STRPTR)tr("all")) || !Stricmp((STRPTR)s, (STRPTR)"all")) return 1;
  if (sscanf(s, "%d-%d", &a, &b) == 2 || (sscanf(s, "%d", &a) == 1 && (b = a))) {
    if (a < 1) a = 1;
    if (b > v->pdf->npages) b = v->pdf->npages;
    if (a > b) return 0;
    *first = a - 1; *last = b - 1;
    return 1;
  }
  return 0;
}
static void base_with(DocView *v, char *path, size_t cap, const char *ext)
{
  char *dot;
  strncpy(path, v->path[0] ? v->path : v->name, cap - 1); path[cap - 1] = 0;
  dot = strrchr((char *)FilePart((STRPTR)path), '.');
  if (dot) *dot = 0;
  strncat(path, ext, cap - 1 - strlen(path));
}
static int save_bytes(const char *title, DocView *v, const char *ext, const char *pattern, const u8 *data, size_t len)
{
  char path[512];
  base_with(v, path, sizeof path, ext);
  if (!file_request(1, title, path, sizeof path, pattern)) return 0;
  if (!data || !write_file(path, data, len)) { say("The file couldn't be saved (out of memory, or the disk is full or write-protected)."); return 0; }
  return 1;
}
/* a document made here opened in a tab of its own (written to T: first, so it has a file) */
static void open_new(PDoc *d, const char *name)
{
  char path[256];
  size_t len;
  u8 *data;
  if (!d) { say("The document couldn't be made (out of memory, or nothing could be read)."); return; }
  data = pdf_save(d, NULL, 0, &len);
  pdf_close(d);
  snprintf(path, sizeof path, "T:%s", name);
  if (!data || !write_file(path, data, len)) { free(data); say("The new document couldn't be written to T:."); return; }
  free(data);
  {
    int enc;
    PDoc *nd = open_pdf(path, &enc);
    extern void doc_show_new(PDoc *d, const char *path, int enc);
    if (nd) doc_show_new(nd, path, enc);
  }
}
/* the text of page p, for the core's functions that want it */
static TextPage *get_text(void *ud, int p) { return page_text((DocView *)ud, p); }

/* ------------------------------------------------------------------ forms */
static int pick_option(FField *f, int cur)
{
  char **it = (char **)calloc((size_t)(f->nopts + 1), sizeof(char *));
  int i, r;
  if (!it) return -1;
  for (i = 0; i < f->nopts; i++) { it[i] = (char *)malloc(strlen(f->opts[i]) * 3 + 4); if (it[i]) to_latin1(f->opts[i], it[i], strlen(f->opts[i]) * 3 + 4); else it[i] = (char *)""; }
  r = dlist(f->name, it, f->nopts, cur, NULL, NULL);
  for (i = 0; i < f->nopts; i++) if (it[i][0] || f->opts[i][0]) free(it[i]);
  free(it);
  return r;
}
int form_click(DocView *v, int page, double x, double y)
{
  FForm fm;
  int w = 0, i, done = 0;
  if (comment_mode || !form_load(v->pdf, &fm)) { form_free(&fm); return 0; }
  i = form_field_at(&fm, page, x, y, &w);
  if (i >= 0) {
    FField *f = &fm.f[i];
    char title[160], buf[1024];
    to_latin1(f->tooltip ? f->tooltip : f->name, title, sizeof title);
    if (f->flags & FFL_READONLY) { say("This field is read-only."); done = 1; }
    else switch (f->type) {
    case FF_TEXT:
      to_latin1(f->value ? f->value : "", buf, sizeof buf);
      if (f->flags & FFL_MULTILINE) {
        char *p;
        /* (lines shown as \n to type) */
        char tmp[1024];
        size_t k, o = 0;
        for (k = 0; buf[k] && o + 3 < sizeof tmp; k++) { if (buf[k] == '\n') { tmp[o++] = '\\'; tmp[o++] = 'n'; } else tmp[o++] = buf[k]; }
        tmp[o] = 0;
        strcpy(buf, tmp);
        if (!ask_text(title, "Text (\\n: new line):", buf, sizeof buf)) { done = 1; break; }
        (void)p;
      } else if (!ask_string(title, "Text:", buf, f->maxlen > 0 && f->maxlen < (int)sizeof buf ? f->maxlen + 1 : (int)sizeof buf)) { done = 1; break; }
      { char *u = u8of(buf); if (u) { form_set_text(v->pdf, f, u); free(u); } }
      done = 2;
      break;
    case FF_CHECK: case FF_RADIO:
      form_set_check(v->pdf, f, w, f->type == FF_RADIO ? 1 : !(f->value && strcmp(f->value, "Off")));
      done = 2;
      break;
    case FF_COMBO: case FF_LIST: {
      int cur = -1, k, o;
      for (k = 0; k < f->nopts; k++) if (f->value && !strcmp(f->value, f->optx[k])) cur = k;
      if ((o = pick_option(f, cur)) >= 0) { form_set_choice(v->pdf, f, o); done = 2; } else done = 1;
      break; }
    case FF_SIG: {
      SigInfo *si;
      int n = sig_list(v->pdf, &si), k;
      char m[400];
      m[0] = 0;
      for (k = 0; k < n; k++) {
        char t[300];
        snprintf(t, sizeof t, "%s: %s %s\n", si[k].field, si[k].name[0] ? si[k].name : "?", si[k].digest_ok == 1 ? tr("(unchanged since signing)") : si[k].digest_ok == 0 ? tr("(CHANGED since signing)") : "");
        if (strlen(m) + strlen(t) < sizeof m) strcat(m, t);
      }
      free(si);
      dshow("Signatures", m[0] ? m : tr("This signature field isn't signed. Folio can't sign with a certificate."));
      done = 1;
      break; }
    default: done = 1;
    }
  }
  if (done == 2) {
    int k;
    for (k = 0; k < fm.f[i].nw; k++) if (fm.f[i].w[k].page >= 0) page_edited(v, fm.f[i].w[k].page, 0);
    doc_changed();
  }
  form_free(&fm);
  return done != 0;
}
static int pick_field(DocView *v, FForm *fm, const char *title)
{
  char **it;
  int i, r;
  if (!form_load(v->pdf, fm)) { say("This document has no form fields."); form_free(fm); return -1; }
  if (!(it = (char **)calloc((size_t)fm->n, sizeof(char *)))) return -1;
  for (i = 0; i < fm->n; i++) {
    static const char *const kinds[] = { "Text", "Check box", "Radio", "Button", "Drop-down", "List", "Signature" };
    char t[300], nm[200], val[100];
    to_latin1(fm->f[i].name, nm, sizeof nm); to_latin1(fm->f[i].value ? fm->f[i].value : "", val, sizeof val);
    snprintf(t, sizeof t, "%s  [%s, p. %d]  %s", nm, tr(kinds[fm->f[i].type]), fm->f[i].nw && fm->f[i].w[0].page >= 0 ? fm->f[i].w[0].page + 1 : 0, val);
    it[i] = str_dup(t);
  }
  r = dlist(title, it, fm->n, 0, NULL, NULL);
  for (i = 0; i < fm->n; i++) free(it[i]);
  free(it);
  return r;
}
static void field_props(DocView *v)
{
  FForm fm;
  int i = pick_field(v, &fm, "Field Properties");
  if (i >= 0) {
    FField *f = &fm.f[i];
    char nm[100], tip[200], ml[12], size[12];
    int ro = (f->flags & FFL_READONLY) != 0, rq = (f->flags & FFL_REQUIRED) != 0, ml_on = 0, q = f->quad;
    static const char *const align[] = { "Left", "Centre", "Right", NULL };
    DField df[7];
    const char *dot = strrchr(f->name, '.');
    to_latin1(dot ? dot + 1 : f->name, nm, sizeof nm); to_latin1(f->tooltip ? f->tooltip : "", tip, sizeof tip);
    sprintf(ml, "%d", f->maxlen); strcpy(size, "0");
    memset(df, 0, sizeof df);
    df[0].label = "Name:"; df[0].kind = DF_STR; df[0].buf = nm; df[0].cap = sizeof nm;
    df[1].label = "Tooltip:"; df[1].kind = DF_STR; df[1].buf = tip; df[1].cap = sizeof tip;
    df[2].label = "Read-only:"; df[2].kind = DF_CHECK; df[2].val = &ro;
    df[3].label = "Required:"; df[3].kind = DF_CHECK; df[3].val = &rq;
    df[4].label = "Most characters (0 any):"; df[4].kind = DF_STR; df[4].buf = ml; df[4].cap = sizeof ml;
    df[5].label = "Alignment:"; df[5].kind = DF_CYCLE; df[5].entries = align; df[5].val = &q;
    df[6].label = "Text size (0 auto):"; df[6].kind = DF_STR; df[6].buf = size; df[6].cap = sizeof size;
    if (dform("Field Properties", df, 7, "OK")) {
      char *un = u8of(nm), *ut = u8of(tip);
      doc_snapshot(v);
      form_set_props(v->pdf, f, un, ut, (ro ? FFL_READONLY : 0) | (rq ? FFL_REQUIRED : 0), (ro ? 0 : FFL_READONLY) | (rq ? 0 : FFL_REQUIRED), atoi(ml), q, atof(size));
      free(un); free(ut);
      (void)ml_on;
      doc_redraw_all(v);
    }
  }
  form_free(&fm);
}
static void form_data(DocView *v, int import)
{
  FForm fm;
  char path[512];
  if (!form_load(v->pdf, &fm)) { say("This document has no form fields."); form_free(&fm); return; }
  if (import) {
    size_t len;
    u8 *data;
    int n;
    base_with(v, path, sizeof path, ".xfdf");
    if (file_request(0, "Import form data (FDF or XFDF)", path, sizeof path, "#?.(fdf|xfdf)") && (data = read_file(path, &len))) {
      doc_snapshot(v);
      n = form_import(v->pdf, &fm, data, len);
      free(data);
      if (n < 0) say("That isn't an FDF or XFDF file.");
      else { sayf("%ld fields were filled in.", n); doc_redraw_all(v); }
    }
  } else {
    int fmt = ask("XFDF|FDF|Cancel", "%s", tr("Export the form's data as XFDF (XML) or FDF?"));
    if (fmt == 1 || fmt == 2) {
      size_t len;
      char *data = form_export(v->pdf, &fm, fmt == 2, v->name, &len);
      save_bytes("Export form data", v, fmt == 2 ? ".fdf" : ".xfdf", fmt == 2 ? "#?.fdf" : "#?.xfdf", (const u8 *)data, len);
      free(data);
    }
  }
  form_free(&fm);
}

static RedactProps *rp_get(void);
/* ------------------------------------------------------------------ the page tools */
static void stamp_pick(void)
{
  static const char *const stamps[] = { "APPROVED", "NOT APPROVED", "DRAFT", "FINAL", "CONFIDENTIAL", "FOR COMMENT", "COMPLETED", "VOID", "REVISED", "RECEIVED", "SIGN HERE", "WITNESS" };
  char *it[13];
  int i, r;
  for (i = 0; i < 12; i++) it[i] = (char *)tr(stamps[i]);
  it[12] = (char *)tr("Your own text...");
  r = dlist("Stamp", it, 13, 0, NULL, NULL);
  if (r < 0) return;
  if (r == 12) { char buf[32]; strcpy(buf, opt.stamp); if (!ask_string("Stamp", "Stamp text:", buf, sizeof buf) || !buf[0]) return; strcpy(opt.stamp, buf); }
  else { strncpy(opt.stamp, tr(stamps[r]), sizeof opt.stamp - 1); opt.stamp[sizeof opt.stamp - 1] = 0; }
}
static void added(DocView *v, int page)
{
  undo_push(v, U_ADDANNOT, page, annot_count(v->pdf, page) - 1, 0, 0, NULL, NULL);
  page_edited(v, page, 1);
  doc_changed_panes();
}
void tool_click(DocView *v, int page, int t, double x, double y)
{
  char buf[512];
  char *u;
  PDoc *d = v->pdf;
  switch (t) {
  case TL_STAMP:
    if (!opt.stamp[0]) stamp_pick();
    if (opt.stamp[0] && (u = u8of(opt.stamp))) { if (annot_stamp(d, page, x, y, u, opt.color[TL_STAMP], author())) added(v, page); free(u); }
    return;
  case TL_ADDTEXT:
    buf[0] = 0;
    if (!ask_text("Add Text", "Text (\\n: new line):", buf, sizeof buf) || !buf[0] || !(u = u8of(buf))) return;
    doc_snapshot(v);
    page_add_text(d, page, x, y + opt.text_size * 0.8, u, opt.text_size, opt.color[TL_ADDTEXT], 0);
    free(u);
    page_changed(v, page); doc_changed();
    return;
  case TL_FTEXT: case TL_DATE: case TL_SIGN: case TL_INITIALS: {
    double r[4], size = t == TL_SIGN || t == TL_INITIALS ? 20 : opt.text_size;
    buf[0] = 0;
    if (t == TL_DATE) {
      time_t now = time(NULL);
      struct tm *tm = localtime(&now);
      if (tm) sprintf(buf, "%02d/%02d/%04d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
    } else if (t == TL_SIGN || t == TL_INITIALS) {
      char *s = t == TL_SIGN ? opt.signature : opt.initials;
      if (!s[0]) { if (!ask_string(t == TL_SIGN ? "Signature" : "Initials", t == TL_SIGN ? "Your name, to sign with:" : "Your initials:", s, t == TL_SIGN ? (int)sizeof opt.signature : (int)sizeof opt.initials) || !s[0]) return; }
      strcpy(buf, s);
    } else if (!ask_text("Add Text", "Text (\\n: new line):", buf, sizeof buf) || !buf[0]) return;
    if (!(u = u8of(buf))) return;
    r[0] = x; r[3] = y + size * 0.6; r[2] = x + strlen(buf) * size * 0.55 + 8; r[1] = r[3] - size * 1.4;
    if (annot_freetext(d, page, r, u, size, opt.color[t], author())) added(v, page);
    free(u);
    return; }
  case TL_CHECK: case TL_CROSS: case TL_DOT: {
    double s = opt.text_size * 0.6;
    if (t == TL_DOT) {
      double c[4];
      c[0] = x - s / 3; c[1] = y - s / 3; c[2] = x + s / 3; c[3] = y + s / 3;
      if (annot_shape(d, page, AN_CIRCLE, c, s / 1.5, opt.color[t], author())) added(v, page);
    } else {
      double pts[8];
      int np[2];
      if (t == TL_CHECK) { pts[0] = x - s / 2; pts[1] = y; pts[2] = x - s / 8; pts[3] = y - s / 2; pts[4] = x + s / 2; pts[5] = y + s / 2; np[0] = 3; if (annot_ink(d, page, pts, np, 1, 1.5, opt.color[t], author())) added(v, page); }
      else { pts[0] = x - s / 2; pts[1] = y - s / 2; pts[2] = x + s / 2; pts[3] = y + s / 2; pts[4] = x - s / 2; pts[5] = y + s / 2; pts[6] = x + s / 2; pts[7] = y - s / 2; np[0] = 2; np[1] = 2; if (annot_ink(d, page, pts, np, 2, 1.5, opt.color[t], author())) added(v, page); }
    }
    return; }
  }
}
void tool_drag(DocView *v, int page, int t, const double *c, int moved)
{
  PDoc *d = v->pdf;
  double r[4];
  r[0] = c[0] < c[2] ? c[0] : c[2]; r[2] = c[0] < c[2] ? c[2] : c[0];
  r[1] = c[1] < c[3] ? c[1] : c[3]; r[3] = c[1] < c[3] ? c[3] : c[1];
  if (!moved) return;
  switch (t) {
  case TL_ARROW: if (annot_arrow(d, page, c, opt.width, opt.color[t], author())) added(v, page); return;
  case TL_MEASURE: if (annot_measure(d, page, c, opt.scale, opt.unit, opt.color[t], author())) added(v, page); return;
  case TL_CLOUD: if (annot_cloud(d, page, r, opt.width, opt.color[t], author())) added(v, page); return;
  case TL_AREA: case TL_PERIM: {
    double pts[8];
    pts[0] = r[0]; pts[1] = r[1]; pts[2] = r[2]; pts[3] = r[1]; pts[4] = r[2]; pts[5] = r[3]; pts[6] = r[0]; pts[7] = r[3];
    if (annot_measure_area(d, page, pts, 4, t == TL_AREA, opt.scale, opt.unit, opt.color[t], author())) added(v, page);
    return; }
  case TL_REDACT: if (redact_mark_area(d, page, r, rp_get())) added(v, page); return;
  case TL_LINK: {
    char buf[300] = "";
    int target = -1;
    if (!ask_string("Add Link", "A page number, or a web address:", buf, sizeof buf) || !buf[0]) return;
    if (buf[0] >= '0' && buf[0] <= '9') target = atoi(buf) - 1;
    if (target >= d->npages) target = d->npages - 1;
    if (page_add_link(d, page, r, target, -1, target >= 0 ? NULL : buf)) added(v, page);
    return; }
  case TL_ADDIMAGE: {
    char path[512] = "";
    size_t len;
    u8 *data;
    if (!file_request(0, "Choose a picture (JPEG or PNG)", path, sizeof path, "#?.(jpg|jpeg|png)") || !(data = read_file(path, &len))) return;
    doc_snapshot(v);
    if (!page_add_image(d, page, r, data, len)) say("That picture couldn't be read (Folio takes JPEG and PNG).");
    free(data);
    page_changed(v, page); doc_changed();
    return; }
  case TL_CROP: {
    double box[4];
    int rot;
    pdf_page_box(d, page, box, &rot);
    if (!yes("Crop this page to the box drawn?")) return;
    doc_snapshot(v);
    page_set_box(d, page, BOX_CROP, r);
    doc_redraw_all(v);
    return; }
  case TL_FIELD: {
    int k = opt.field_kind, ok;
    char name[64] = "";
    const char *const *opts = NULL;
    char *ov[16];
    int nopts = 0;
    if (k == FF_COMBO || k == FF_LIST) {
      char buf[400] = "One, Two, Three", *p, *s;
      if (!ask_string("Choices", "The choices, separated by commas:", buf, sizeof buf)) return;
      for (s = buf; nopts < 16 && (p = strtok(s, ",")); s = NULL) { while (*p == ' ') p++; ov[nopts++] = u8of(p); }
      opts = (const char *const *)ov;
    }
    if (k == FF_RADIO && !ask_string("Radio Button", "The group's name (buttons of one name go together):", name, sizeof name)) return;
    doc_snapshot(v);
    if (k == 8) {
      char rc[16] = "3 x 2";
      int rows = 3, cols = 2;
      if (!ask_string("Text Field Table", "Rows x columns:", rc, sizeof rc)) return;
      sscanf(rc, "%d x %d", &rows, &cols);
      ok = form_add_table(d, page, r, rows, cols, "Table");
    } else if (k == 7) ok = form_add_field(d, page, FF_TEXT, r, "Date", NULL, 0, 0);
    else if (k == 9) ok = form_add_field(d, page, FF_PUSH, r, "Picture", NULL, 0, 0);
    else {
      char *un = name[0] ? u8of(name) : NULL;
      ok = form_add_field(d, page, k, r, un, opts, nopts, k == FF_TEXT && r[3] - r[1] > 40);
      free(un);
    }
    while (nopts) free(ov[--nopts]);
    if (!ok) say("The field couldn't be added.");
    page_changed(v, page); doc_changed();
    return; }
  }
}

/* ------------------------------------------------------------------ page content dialogs */
static void header_footer(DocView *v, int bates)
{
  static HFOpts h;
  static int init;
  char lat[6][200], range[24] = "all", size[12], start[12], digits[8], prefix[32];
  DField df[12];
  int i, n = 0, first, last;
  if (!init) { h.size = 10; h.bates_digits = 6; h.bates_start = 1; strcpy(h.text[4], "<<page>> / <<pages>>"); init = 1; }
  for (i = 0; i < 6; i++) to_latin1(h.text[i], lat[i], sizeof lat[i]);
  sprintf(size, "%g", h.size); sprintf(start, "%d", h.bates_start); sprintf(digits, "%d", h.bates_digits);
  strcpy(prefix, h.bates_prefix);
  memset(df, 0, sizeof df);
  if (bates) {
    df[n].label = "Prefix:"; df[n].kind = DF_STR; df[n].buf = prefix; df[n].cap = sizeof prefix; n++;
    df[n].label = "Start number:"; df[n].kind = DF_STR; df[n].buf = start; df[n].cap = sizeof start; n++;
    df[n].label = "Digits:"; df[n].kind = DF_STR; df[n].buf = digits; df[n].cap = sizeof digits; n++;
  } else {
    static const char *const labels[6] = { "Header left:", "Header centre:", "Header right:", "Footer left:", "Footer centre:", "Footer right:" };
    for (i = 0; i < 6; i++) { df[n].label = labels[i]; df[n].kind = DF_STR; df[n].buf = lat[i]; df[n].cap = 200; n++; }
    df[n].label = "Tokens:"; df[n].kind = DF_INFO; df[n].buf = (char *)"<<page>> <<pages>> <<label>> <<date>>"; n++;
  }
  df[n].label = "Text size:"; df[n].kind = DF_STR; df[n].buf = size; df[n].cap = sizeof size; n++;
  df[n].label = "Pages (e.g. 2-5, or all):"; df[n].kind = DF_STR; df[n].buf = range; df[n].cap = sizeof range; n++;
  if (!dform(bates ? "Bates Numbering" : "Header & Footer", df, n, "Add")) return;
  if (!page_range(v, range, &first, &last)) { say("Those pages aren't in the document."); return; }
  {
    HFOpts o = h;
    if (bates) {
      memset(o.text, 0, sizeof o.text);
      strcpy(o.text[5], "<<bates>>");
      strncpy(o.bates_prefix, prefix, sizeof o.bates_prefix - 1);
      o.bates_start = atoi(start); o.bates_digits = atoi(digits);
      h.bates_start = o.bates_start; h.bates_digits = o.bates_digits; strcpy(h.bates_prefix, o.bates_prefix);
    } else for (i = 0; i < 6; i++) { char *u = u8of(lat[i]); if (u) { strncpy(o.text[i], u, sizeof o.text[i] - 1); o.text[i][sizeof o.text[i] - 1] = 0; strcpy(h.text[i], o.text[i]); free(u); } }
    o.size = atof(size) > 2 ? atof(size) : 10; h.size = o.size;
    o.first = first; o.last = last;
    doc_snapshot(v);
    busy(1);
    /* (adding again updates: the old one goes first) */
    if (marks_present(v->pdf, bates ? "bates" : "header")) marks_remove(v->pdf, bates ? "bates" : "header");
    hf_apply(v->pdf, &o, bates ? "bates" : "header");
    busy(0);
    doc_redraw_all(v);
  }
}
static void watermark(DocView *v)
{
  static char text[100] = "CONFIDENTIAL", size[12] = "72", opac[12] = "30", angle[12] = "45";
  static int behind, kind;
  static const char *const kinds[] = { "Text", "Picture", NULL };
  char range[24] = "all";
  DField df[7];
  int first, last;
  WMOpts w;
  u8 *img = NULL;
  size_t il = 0;
  memset(df, 0, sizeof df);
  df[0].label = "Watermark:"; df[0].kind = DF_CYCLE; df[0].entries = kinds; df[0].val = &kind;
  df[1].label = "Text:"; df[1].kind = DF_STR; df[1].buf = text; df[1].cap = sizeof text;
  df[2].label = "Size (points, or % of the page for a picture):"; df[2].kind = DF_STR; df[2].buf = size; df[2].cap = sizeof size;
  df[3].label = "Opacity (%):"; df[3].kind = DF_STR; df[3].buf = opac; df[3].cap = sizeof opac;
  df[4].label = "Angle:"; df[4].kind = DF_STR; df[4].buf = angle; df[4].cap = sizeof angle;
  df[5].label = "Behind the page:"; df[5].kind = DF_CHECK; df[5].val = &behind;
  df[6].label = "Pages (e.g. 2-5, or all):"; df[6].kind = DF_STR; df[6].buf = range; df[6].cap = sizeof range;
  if (!dform("Watermark", df, 7, "Add")) return;
  if (!page_range(v, range, &first, &last)) { say("Those pages aren't in the document."); return; }
  memset(&w, 0, sizeof w);
  if (kind == 1) {
    char path[512] = "";
    if (!file_request(0, "Choose a picture (JPEG or PNG)", path, sizeof path, "#?.(jpg|jpeg|png)") || !(img = read_file(path, &il))) return;
    w.image = img; w.image_len = il; w.size = atof(size) / 100;
  } else { char *u = u8of(text); if (u) { strncpy(w.text, u, sizeof w.text - 1); free(u); } w.size = atof(size); }
  w.opacity = atof(opac) / 100; w.angle = atof(angle); w.color = opt.color[TL_ADDTEXT] ? opt.color[TL_ADDTEXT] : 0xC00000; w.behind = behind;
  w.first = first; w.last = last;
  doc_snapshot(v);
  busy(1);
  if (marks_present(v->pdf, "watermark")) marks_remove(v->pdf, "watermark");
  if (!watermark_apply(v->pdf, &w)) { busy(0); say("The watermark couldn't be added."); }
  busy(0);
  free(img);
  doc_redraw_all(v);
}
static void background(DocView *v)
{
  char range[24] = "all";
  int r = ask("Colour...|Picture...|Cancel", "%s", tr("A colour or a picture behind the pages?")), first, last;
  u8 *img = NULL;
  size_t il = 0;
  LONG c = 0xFFFFE0;
  if (r == 1) { if ((c = pick_color("Background", 0xFFFFE0)) < 0) return; }
  else if (r == 2) { char path[512] = ""; if (!file_request(0, "Choose a picture (JPEG or PNG)", path, sizeof path, "#?.(jpg|jpeg|png)") || !(img = read_file(path, &il))) return; }
  else return;
  if (!ask_string("Background", "Pages (e.g. 2-5, or all):", range, sizeof range) || !page_range(v, range, &first, &last)) { free(img); return; }
  doc_snapshot(v);
  busy(1);
  if (marks_present(v->pdf, "background")) marks_remove(v->pdf, "background");
  background_apply(v->pdf, (u32)c, img, il, first, last);
  busy(0);
  free(img);
  doc_redraw_all(v);
}
static void remove_mark(DocView *v, const char *mark, const char *what)
{
  char m[200];
  if (!marks_present(v->pdf, mark)) { snprintf(m, sizeof m, tr("This document has no %s that Folio added."), tr(what)); ask("OK", "%s", m); return; }
  doc_snapshot(v);
  marks_remove(v->pdf, mark);
  doc_redraw_all(v);
}

/* ------------------------------------------------------------------ attachments, bookmarks, labels, boxes */
static void attachments(DocView *v, int add)
{
  PAttach *a;
  int n, i, r, del = 0;
  char **it;
  if (add) {
    char path[512] = "";
    size_t len;
    u8 *data;
    char *nm;
    if (!file_request(0, "Attach a file", path, sizeof path, NULL) || !(data = read_file(path, &len))) return;
    nm = u8of((const char *)FilePart((STRPTR)path));
    doc_snapshot(v);
    if (!nm || !attach_add(v->pdf, nm, data, len, NULL)) say("The file couldn't be attached (out of memory?).");
    free(nm); free(data);
    doc_changed();
    return;
  }
  n = attach_list(v->pdf, &a);
  if (!n) { say("This document has no attachments."); return; }
  if (!(it = (char **)calloc((size_t)n, sizeof(char *)))) { attach_free(a, n); return; }
  for (i = 0; i < n; i++) { char t[300], nm[200]; to_latin1(a[i].name, nm, sizeof nm); snprintf(t, sizeof t, "%s (%ld bytes)", nm, a[i].size); it[i] = str_dup(t); }
  r = dlist("Attachments: double-click to save one", it, n, 0, "Delete", &del);
  if (r >= 0) {
    if (del) { if (yes("Delete this attachment?")) { doc_snapshot(v); attach_delete(v->pdf, a[r].name); doc_changed(); } }
    else {
      size_t len;
      u8 *data = attach_data(v->pdf, a[r].name, &len);
      char path[512];
      to_latin1(a[r].name, path, sizeof path);
      if (file_request(1, "Save the attachment as", path, sizeof path, NULL) && (!data || !write_file(path, data, len))) say("The attachment couldn't be saved.");
      free(data);
    }
  }
  for (i = 0; i < n; i++) free(it[i]);
  free(it);
  attach_free(a, n);
}
/* the bookmarks flattened into a list (with their depth) for choosing */
typedef struct { POutline *o, *parent, *prev; int depth; } BRow;
static int flat(POutline *o, POutline *parent, int depth, BRow *rows, int n, int cap)
{
  POutline *prev = NULL;
  for (; o && n < cap; prev = o, o = o->next) {
    rows[n].o = o; rows[n].parent = parent; rows[n].prev = prev; rows[n].depth = depth; n++;
    n = flat(o->child, o, depth + 1, rows, n, cap);
  }
  return n;
}
static void bookmarks(DocView *v, int what)
{
  POutline *tree = pdf_outline(v->pdf);
  BRow rows[400];
  char *it[400];
  int n, i, r;
  if (what == 0) {
    /* a new bookmark for the page in view, at the end */
    char buf[200] = "";
    POutline *o, *t;
    char *u;
    sprintf(buf, "%s %d", tr("Page"), v->page + 1);
    if (!ask_string("Add Bookmark", "Bookmark title:", buf, sizeof buf) || !buf[0] || !(u = u8of(buf))) { pdf_outline_free(tree); return; }
    o = outline_item(u, v->page, -1);
    free(u);
    if (!o) { pdf_outline_free(tree); return; }
    if (!tree) tree = o; else { for (t = tree; t->next; t = t->next) ; t->next = o; }
  } else if (what == 3) {
    POutline *made = outline_from_styles(v->pdf, get_text, v);
    if (!made) { say("No headings were found (no text set larger than the body text)."); pdf_outline_free(tree); return; }
    if (tree && !yes("Replace the bookmarks with those made from the headings?")) { pdf_outline_free(made); pdf_outline_free(tree); return; }
    pdf_outline_free(tree);
    tree = made;
  } else {
    if (!tree) { say("This document has no bookmarks."); return; }
    n = flat(tree, NULL, 0, rows, 0, 400);
    for (i = 0; i < n; i++) { char t[300], ti[200]; to_latin1(rows[i].o->title, ti, sizeof ti); snprintf(t, sizeof t, "%*s%s", rows[i].depth * 3, "", ti); it[i] = str_dup(t); }
    r = dlist(what == 1 ? "Rename Bookmark" : "Delete Bookmark", it, n, 0, NULL, NULL);
    for (i = 0; i < n; i++) free(it[i]);
    if (r < 0) { pdf_outline_free(tree); return; }
    if (what == 1) {
      char buf[200];
      char *u;
      to_latin1(rows[r].o->title, buf, sizeof buf);
      if (!ask_string("Rename Bookmark", "Bookmark title:", buf, sizeof buf) || !(u = u8of(buf))) { pdf_outline_free(tree); return; }
      free(rows[r].o->title); rows[r].o->title = u;
    } else {
      POutline *o = rows[r].o;
      if (rows[r].prev) rows[r].prev->next = o->next;
      else if (rows[r].parent) rows[r].parent->child = o->next;
      else tree = o->next;
      o->next = NULL;
      pdf_outline_free(o);
    }
  }
  doc_snapshot(v);
  outline_write(v->pdf, tree);
  pdf_outline_free(tree);
  if (v->outline) { pdf_outline_free(v->outline); v->outline = NULL; }
  v->outline_done = 0;
  doc_changed();
}
static void labels(DocView *v)
{
  static const char *const styles[] = { "1, 2, 3", "i, ii, iii", "I, II, III", "a, b, c", "A, B, C", "None", NULL };
  static const char sty[] = { 'D', 'r', 'R', 'a', 'A', 0 };
  char from[12], prefix[32] = "", start[12] = "1";
  int s = 0;
  DField df[4];
  PLabel *l, nl;
  int n, i;
  sprintf(from, "%d", v->page + 1);
  memset(df, 0, sizeof df);
  df[0].label = "From page:"; df[0].kind = DF_STR; df[0].buf = from; df[0].cap = sizeof from;
  df[1].label = "Style:"; df[1].kind = DF_CYCLE; df[1].entries = styles; df[1].val = &s;
  df[2].label = "Prefix:"; df[2].kind = DF_STR; df[2].buf = prefix; df[2].cap = sizeof prefix;
  df[3].label = "Start at:"; df[3].kind = DF_STR; df[3].buf = start; df[3].cap = sizeof start;
  if (!dform("Page Labels", df, 4, "OK")) return;
  memset(&nl, 0, sizeof nl);
  nl.start = atoi(from) - 1; nl.style = sty[s]; nl.first = atoi(start) > 0 ? atoi(start) : 1;
  { char *u = u8of(prefix); if (u) { strncpy(nl.prefix, u, sizeof nl.prefix - 1); free(u); } }
  if (nl.start < 0 || nl.start >= v->pdf->npages) { say("That page isn't in the document."); return; }
  n = page_labels_get(v->pdf, &l);
  {
    PLabel *all = (PLabel *)malloc((size_t)(n + 2) * sizeof(PLabel));
    int k = 0;
    if (!all) { free(l); return; }
    if (!n && nl.start > 0) { memset(&all[k], 0, sizeof all[0]); all[k].style = 'D'; all[k].first = 1; k++; }
    for (i = 0; i < n; i++) if (l[i].start != nl.start) all[k++] = l[i];
    all[k++] = nl;
    /* in page order */
    for (i = 1; i < k; i++) { int j; for (j = i; j > 0 && all[j - 1].start > all[j].start; j--) { PLabel t = all[j]; all[j] = all[j - 1]; all[j - 1] = t; } }
    doc_snapshot(v);
    page_labels_set(v->pdf, all, k);
    free(all);
  }
  free(l);
  doc_changed();
}
static void crop_dialog(DocView *v)
{
  char m[4][12], range[24] = "all";
  DField df[5];
  int i, first, last;
  for (i = 0; i < 4; i++) strcpy(m[i], "0");
  memset(df, 0, sizeof df);
  df[0].label = "Left (mm):"; df[1].label = "Bottom (mm):"; df[2].label = "Right (mm):"; df[3].label = "Top (mm):";
  for (i = 0; i < 4; i++) { df[i].kind = DF_STR; df[i].buf = m[i]; df[i].cap = 12; }
  df[4].label = "Pages (e.g. 2-5, or all):"; df[4].kind = DF_STR; df[4].buf = range; df[4].cap = sizeof range;
  if (!dform("Crop Pages", df, 5, "Crop")) return;
  if (!page_range(v, range, &first, &last)) { say("Those pages aren't in the document."); return; }
  doc_snapshot(v);
  page_crop(v->pdf, first, last, atof(m[0]) * 72 / 25.4, atof(m[1]) * 72 / 25.4, atof(m[2]) * 72 / 25.4, atof(m[3]) * 72 / 25.4);
  doc_redraw_all(v);
}
static void boxes_dialog(DocView *v)
{
  static const char *const which[] = { "Media box", "Crop box", "Bleed box", "Trim box", "Art box", NULL };
  int w = BOX_TRIM, i;
  double r[4];
  char c[4][16];
  DField df[5];
  page_get_box(v->pdf, v->page, w, r);
  for (i = 0; i < 4; i++) sprintf(c[i], "%.1f", r[i]);
  memset(df, 0, sizeof df);
  df[0].label = "Box:"; df[0].kind = DF_CYCLE; df[0].entries = which; df[0].val = &w;
  df[1].label = "Left (pt):"; df[2].label = "Bottom (pt):"; df[3].label = "Right (pt):"; df[4].label = "Top (pt):";
  for (i = 0; i < 4; i++) { df[i + 1].kind = DF_STR; df[i + 1].buf = c[i]; df[i + 1].cap = 16; }
  if (!dform("Set Page Boxes (this page)", df, 5, "Set")) return;
  for (i = 0; i < 4; i++) r[i] = atof(c[i]);
  doc_snapshot(v);
  if (!page_set_box(v->pdf, v->page, w, r)) say("That box is too small.");
  doc_redraw_all(v);
}

/* ------------------------------------------------------------------ pages */
static void split(DocView *v)
{
  static const char *const modes[] = { "Every so many pages", "At each top-level bookmark", "Into so many files", NULL };
  int mode = 0, *st, n, i;
  char val[12] = "1", dir[512];
  DField df[2];
  memset(df, 0, sizeof df);
  df[0].label = "Split:"; df[0].kind = DF_CYCLE; df[0].entries = modes; df[0].val = &mode;
  df[1].label = "Pages, or files:"; df[1].kind = DF_STR; df[1].buf = val; df[1].cap = sizeof val;
  if (!dform("Split Document", df, 2, "Split")) return;
  n = split_points(v->pdf, mode, atoi(val), &st);
  if (n < 2) { free(st); say("That gives only one part: nothing to split."); return; }
  base_with(v, dir, sizeof dir, "");
  if (!file_request(1, "Split: the parts' name (a number is added)", dir, sizeof dir, "#?.pdf")) { free(st); return; }
  { char *dot = strrchr((char *)FilePart((STRPTR)dir), '.'); if (dot) *dot = 0; }
  busy(1);
  for (i = 0; i < n; i++) {
    int a = st[i], b = i + 1 < n ? st[i + 1] : v->pdf->npages, k, *order = (int *)malloc((size_t)(b - a) * sizeof(int));
    char path[560];
    size_t len;
    u8 *data;
    if (!order) break;
    for (k = a; k < b; k++) order[k - a] = k;
    data = pdf_save(v->pdf, order, b - a, &len);
    snprintf(path, sizeof path, "%s-%d.pdf", dir, i + 1);
    if (!data || !write_file(path, data, len)) { free(data); free(order); busy(0); say("A part couldn't be saved."); free(st); return; }
    free(data); free(order);
  }
  busy(0);
  sayf("The document was split into %ld files.", n);
  free(st);
}
static void replace_pages(DocView *v)
{
  char path[512] = "", range[24];
  int enc, first, last;
  PDoc *src;
  if (!file_request(0, "Replace pages with those of", path, sizeof path, "#?.pdf") || !(src = open_pdf(path, &enc))) return;
  sprintf(range, "1-%d", src->npages < 1 ? 1 : src->npages);
  if (ask_string("Replace Pages", "Its pages to put in (e.g. 1-3):", range, sizeof range)) {
    int a = 1, b = 1;
    if (sscanf(range, "%d-%d", &a, &b) < 2) b = a;
    first = a - 1; last = b - 1;
    if (first < 0 || last >= src->npages || first > last) say("Those pages aren't in that document.");
    else {
      int n = last - first + 1;
      if (v->page + n > v->pdf->npages) n = v->pdf->npages - v->page;
      doc_snapshot(v);
      if (!page_replace(v->pdf, v->page, n, src, first, last)) say("The pages couldn't be replaced.");
      doc_redraw_all(v);
    }
  }
  pdf_close(src);
}

/* ------------------------------------------------------------------ protecting */
static void save_with_password(DocView *v)
{
  static const char *const methods[] = { "AES-256 (Acrobat X and later)", "AES-128 (Acrobat 7 and later)", NULL };
  char up[64] = "", op[64] = "", path[512];
  int m = 0, pr = 1, cp = 1, ed = 1, an = 1, fi = 1;
  DField df[8];
  SaveOpts so;
  size_t len;
  u8 *data;
  memset(df, 0, sizeof df);
  df[0].label = "Password to open:"; df[0].kind = DF_STR; df[0].buf = up; df[0].cap = sizeof up;
  df[1].label = "Password for permissions:"; df[1].kind = DF_STR; df[1].buf = op; df[1].cap = sizeof op;
  df[2].label = "Encryption:"; df[2].kind = DF_CYCLE; df[2].entries = methods; df[2].val = &m;
  df[3].label = "Allow printing:"; df[3].kind = DF_CHECK; df[3].val = &pr;
  df[4].label = "Allow copying text:"; df[4].kind = DF_CHECK; df[4].val = &cp;
  df[5].label = "Allow changes:"; df[5].kind = DF_CHECK; df[5].val = &ed;
  df[6].label = "Allow comments:"; df[6].kind = DF_CHECK; df[6].val = &an;
  df[7].label = "Allow filling in forms:"; df[7].kind = DF_CHECK; df[7].val = &fi;
  if (!dform("Protect with Password", df, 8, "Save...")) return;
  if (!up[0] && !op[0]) { say("Give a password to open the document, or one for its permissions."); return; }
  memset(&so, 0, sizeof so);
  so.crypt = m == 0 ? SAVE_AES256 : SAVE_AES128;
  so.user_pw = up; so.owner_pw = op[0] ? op : NULL;
  so.perms = PERM_ACCESS | (pr ? PERM_PRINT | PERM_PRINTHQ : 0) | (cp ? PERM_COPY : 0) | (ed ? PERM_MODIFY | PERM_ASSEMBLE : 0) | (an ? PERM_ANNOTATE : 0) | (fi ? PERM_FILL : 0);
  so.compress = 1;
  base_with(v, path, sizeof path, "-protected.pdf");
  if (!file_request(1, "Save the protected copy as", path, sizeof path, "#?.pdf")) return;
  busy(1);
  data = pdf_save_opts(v->pdf, NULL, 0, &so, &len);
  busy(0);
  if (!data || !write_file(path, data, len)) say("The file couldn't be saved (out of memory, or the disk is full or write-protected).");
  else say("The protected copy was saved. The document open here stays as it was.");
  free(data);
}
static void security_props(DocView *v)
{
  char m[600];
  PDoc *d = v->pdf;
  long p = d->perms;
  if (!v->was_encrypted) { dshow("Security", tr("No security: anyone can open, print, copy and change this document.\n\nProtect with Password saves a protected copy.")); return; }
  snprintf(m, sizeof m, "%s: %s\n\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n\n%s",
           tr("Encryption"), d->crypt == 3 ? "AES-256" : d->crypt == 2 ? "AES-128" : "RC4",
           tr("Printing"), p & 4 ? tr("allowed") : tr("not allowed"),
           tr("Copying text"), p & 16 ? tr("allowed") : tr("not allowed"),
           tr("Changing the document"), p & 8 ? tr("allowed") : tr("not allowed"),
           tr("Commenting"), p & 32 ? tr("allowed") : tr("not allowed"),
           tr("Filling in forms"), p & 256 ? tr("allowed") : tr("not allowed"),
           tr("Assembling pages"), p & 1024 ? tr("allowed") : tr("not allowed"),
           tr("Remove Security saves the document without its protection."));
  dshow("Security", m);
}
static void hidden_info(DocView *v, int sanitize)
{
  static const struct { int bit; const char *name; } kinds[] = {
    { HID_METADATA, "Metadata and document information" }, { HID_SCRIPTS, "Scripts and actions" }, { HID_ATTACH, "Attachments" },
    { HID_COMMENTS, "Comments" }, { HID_FORMS, "Form fields (flattened)" }, { HID_LAYERS, "Hidden layers" },
    { HID_INVISIBLE, "Invisible text" }, { HID_THUMBS, "Page thumbnails" }, { HID_PRIVATE, "Private application data" },
    { HID_LINKS, "Links to the web and files" }, { HID_BOOKMARKS, "Bookmarks" } };
  int have = hidden_survey(v->pdf), sel[11], i, n = 0, what = 0;
  DField df[11];
  if (sanitize) {
    if (!yes("Sanitize: remove metadata, scripts, attachments, comments, form fields, hidden layers, invisible text, thumbnails, private data and links?")) return;
    what = HID_SANITIZE;
  } else {
    memset(df, 0, sizeof df);
    for (i = 0; i < 11; i++) if (have & kinds[i].bit) { sel[n] = kinds[i].bit != HID_BOOKMARKS && kinds[i].bit != HID_FORMS; df[n].label = kinds[i].name; df[n].kind = DF_CHECK; df[n].val = &sel[n]; n++; }
    if (!n) { say("Nothing hidden was found."); return; }
    if (!dform("Remove Hidden Information", df, n, "Remove")) return;
    for (i = 0, n = 0; i < 11; i++) if (have & kinds[i].bit) { if (sel[n]) what |= kinds[i].bit; n++; }
  }
  doc_snapshot(v);
  busy(1);
  n = hidden_remove(v->pdf, what);
  busy(0);
  sayf("%ld things were removed.", n);
  doc_redraw_all(v);
}
static RedactProps rprops;
static int rprops_set;
static RedactProps *rp_get(void) { if (!rprops_set) { redact_props_default(&rprops); rprops_set = 1; } return &rprops; }
static void redact_props(void)
{
  RedactProps *p = rp_get();
  char ov[64];
  LONG c;
  DField df[1];
  to_latin1(p->overlay, ov, sizeof ov);
  memset(df, 0, sizeof df);
  df[0].label = "Text over each area (none if empty):"; df[0].kind = DF_STR; df[0].buf = ov; df[0].cap = sizeof ov;
  if ((c = pick_color("Redaction colour", p->fill)) < 0) return;
  p->fill = (u32)c;
  if (dform("Redaction Properties", df, 1, "OK")) { char *u = u8of(ov); if (u) { strncpy(p->overlay, u, sizeof p->overlay - 1); p->overlay[sizeof p->overlay - 1] = 0; free(u); } }
}
static void redact_text(DocView *v)
{
  char buf[100] = "";
  char *u;
  int p, n = 0;
  if (!ask_string("Find Text and Redact", "Text to redact:", buf, sizeof buf) || !buf[0] || !(u = u8of(buf))) return;
  busy(1);
  for (p = 0; p < v->pdf->npages; p++) { int k = redact_mark_text(v->pdf, p, page_text(v, p), u, rp_get()); if (k) { n += k; page_changed(v, p); } }
  busy(0);
  free(u);
  sayf("%ld places were marked for redaction. Apply Redactions takes them out for good.", n);
  doc_changed();
}
static void apply_redactions(DocView *v)
{
  int n = redact_count(v->pdf, -1);
  if (!n) { say("No areas are marked for redaction."); return; }
  if (!yes("Apply the redactions? What is under them is removed from the document for good when it is saved.")) return;
  doc_snapshot(v);
  busy(1);
  n = redact_apply(v->pdf, -1);
  busy(0);
  doc_redraw_all(v);
  sayf("%ld areas were redacted.", n);
}

/* ------------------------------------------------------------------ optimizing */
static void reduce(DocView *v, int advanced)
{
  static const char *const res[] = { "150 dpi (screen and e-mail)", "100 dpi (smallest)", "300 dpi (print)", "Keep the pictures", NULL };
  static const int dpis[] = { 150, 100, 300, 0 };
  int r = 0, q = 0, dedupe = 1, objs = 1, strip = 0, n, *alias = NULL, dup = 0;
  static const char *const quals[] = { "Medium (75)", "Low (50)", "High (90)", NULL };
  static const int qv[] = { 75, 50, 90 };
  DField df[5];
  char path[512];
  SaveOpts so;
  size_t len;
  u8 *data;
  memset(df, 0, sizeof df);
  df[0].label = "Pictures:"; df[0].kind = DF_CYCLE; df[0].entries = res; df[0].val = &r;
  n = 1;
  if (advanced) {
    df[n].label = "JPEG quality:"; df[n].kind = DF_CYCLE; df[n].entries = quals; df[n].val = &q; n++;
    df[n].label = "Store identical streams once:"; df[n].kind = DF_CHECK; df[n].val = &dedupe; n++;
    df[n].label = "Pack objects (PDF 1.5):"; df[n].kind = DF_CHECK; df[n].val = &objs; n++;
    df[n].label = "Remove metadata and thumbnails:"; df[n].kind = DF_CHECK; df[n].val = &strip; n++;
  }
  if (!dform(advanced ? "Advanced Optimization" : "Reduce File Size", df, n, "Save...")) return;
  base_with(v, path, sizeof path, "-reduced.pdf");
  if (!file_request(1, "Save the smaller copy as", path, sizeof path, "#?.pdf")) return;
  doc_snapshot(v);
  busy(1);
  if (dpis[r]) optimize_images(v->pdf, dpis[r], qv[q]);
  if (strip) hidden_remove(v->pdf, HID_METADATA | HID_THUMBS | HID_PRIVATE);
  if (dedupe) alias = optimize_dedupe(v->pdf, &dup);
  memset(&so, 0, sizeof so);
  so.compress = 1; so.objstm = objs; so.alias = alias; so.nalias = v->pdf->nxref + 1;
  data = pdf_save_opts(v->pdf, NULL, 0, &so, &len);
  busy(0);
  free(alias);
  if (!data || !write_file(path, data, len)) say("The file couldn't be saved (out of memory, or the disk is full or write-protected).");
  else {
    char m[200];
    snprintf(m, sizeof m, tr("Saved: %lu KB (the original was %lu KB)."), (unsigned long)((len + 1023) / 1024), (unsigned long)((v->pdf->len + 1023) / 1024));
    ask("OK", "%s", m);
  }
  free(data);
  doc_redraw_all(v);
}
static void audit(DocView *v)
{
  SpaceAudit a;
  char m[600];
  double t;
  optimize_audit(v->pdf, &a);
  t = a.total > 0 ? a.total : 1;
  snprintf(m, sizeof m, "%s: %ld KB\n\n%s (%d): %ld KB, %.0f%%\n%s (%d): %ld KB, %.0f%%\n%s: %ld KB, %.0f%%\n%s: %ld KB, %.0f%%\n%s: %ld KB, %.0f%%\n%s: %ld KB, %.0f%%\n%s: %ld KB, %.0f%%\n%s (%d): %ld KB, %.0f%%",
           tr("File"), a.total / 1024,
           tr("Pictures"), a.nimages, a.images / 1024, a.images * 100 / t, tr("Embedded fonts"), a.nfonts, a.fonts / 1024, a.fonts * 100 / t,
           tr("Page content"), a.content / 1024, a.content * 100 / t, tr("Forms and appearances"), a.forms / 1024, a.forms * 100 / t,
           tr("Metadata"), a.metadata / 1024, a.metadata * 100 / t, tr("Attachments"), a.attachments / 1024, a.attachments * 100 / t,
           tr("Other streams"), a.other_streams / 1024, a.other_streams * 100 / t, tr("Objects and structure"), a.nobjects, a.objects / 1024, a.objects * 100 / t);
  dshow("Audit Space Usage", m);
}

/* ------------------------------------------------------------------ creating and exporting */
static PDoc *from_files(int text)
{
  struct FileRequester *fr;
  PDoc *d = NULL;
  int i;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(text ? "Choose a text file" : "Choose pictures (JPEG or PNG)"),
        ASLFR_DoMultiSelect, !text, ASLFR_InitialPattern, (ULONG)(text ? "#?" : "#?.(jpg|jpeg|png)"), ASLFR_DoPatterns, TRUE, TAG_DONE))) return NULL;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)xget(win, MUIA_Window_Window), TAG_DONE)) {
    int n = text ? 1 : (fr->fr_NumArgs > 0 ? (int)fr->fr_NumArgs : 1);
    u8 **files = (u8 **)calloc((size_t)n, sizeof(u8 *));
    size_t *lens = (size_t *)calloc((size_t)n, sizeof(size_t));
    busy(1);
    for (i = 0; files && lens && i < n; i++) {
      char path[512];
      strncpy(path, (const char *)fr->fr_Drawer, sizeof path - 1); path[sizeof path - 1] = 0;
      AddPart((STRPTR)path, (STRPTR)(fr->fr_NumArgs > 0 ? (const char *)fr->fr_ArgList[i].wa_Name : (const char *)fr->fr_File), sizeof path);
      files[i] = read_file(path, &lens[i]);
    }
    if (files && lens) {
      if (text) { if (files[0]) { char *u = u8of((const char *)files[0]); d = u ? doc_from_text(u, strlen(u), 595.28, 841.89, 10, 3) : NULL; free(u); } }
      else d = doc_from_images((const u8 *const *)files, lens, n, 0, 0);
    }
    for (i = 0; files && i < n; i++) free(files[i]);
    free(files); free(lens);
    busy(0);
  }
  MUI_FreeAslRequest(fr);
  return d;
}
static int img_each(void *ud, const char *name, const u8 *data, size_t n)
{
  char path[600];
  snprintf(path, sizeof path, "%s%s", (const char *)ud, name);
  return write_file(path, data, n);
}
static void export_as(DocView *v, ULONG id)
{
  size_t len;
  u8 *data = NULL;
  busy(1);
  switch (id) {
  case ID_EXPHTML: data = export_text(v->pdf, EXP_HTML, get_text, v, &len); busy(0); save_bytes("Export as a web page", v, ".html", "#?.html", data, len); break;
  case ID_EXPRTF: data = export_text(v->pdf, EXP_RTF, get_text, v, &len); busy(0); save_bytes("Export as RTF", v, ".rtf", "#?.rtf", data, len); break;
  case ID_EXPDOCX: data = export_text(v->pdf, EXP_DOCX, get_text, v, &len); busy(0); save_bytes("Export as Word", v, ".docx", "#?.docx", data, len); break;
  case ID_EXPPPTX: data = export_pptx(v->pdf, v->rc, 96, &len); busy(0); save_bytes("Export as PowerPoint", v, ".pptx", "#?.pptx", data, len); break;
  case ID_EXPIMAGES: {
    char path[512];
    busy(0);
    base_with(v, path, sizeof path, "-");
    if (file_request(1, "Export all pictures: their names start with", path, sizeof path, NULL)) {
      int n;
      busy(1);
      n = export_images(v->pdf, img_each, path);
      busy(0);
      sayf("%ld pictures were saved.", n);
    }
    break; }
  }
  busy(0);
  free(data);
}

/* ------------------------------------------------------------------ reviewing */
static void compare(DocView *v)
{
  char path[512] = "";
  int enc, n;
  PDoc *old;
  Diff df;
  if (!file_request(0, "Compare with (the older version)", path, sizeof path, "#?.pdf") || !(old = open_pdf(path, &enc))) return;
  {
    DocView tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.pdf = old; tmp.rc = rcache_new(old);
    doc_snapshot(v);
    busy(1);
    n = compare_docs(old, get_text, &tmp, v->pdf, get_text, v, 1, &df);
    busy(0);
    {
      int i;
      for (i = 0; i < tmp.ntexts; i++) textpage_free(&tmp.texts[i].tp);
      free(tmp.texts);
    }
    rcache_free(tmp.rc);
  }
  pdf_close(old);
  if (!n) say("The documents' text is the same.");
  else { char *rep = diff_report(&df); dshow("Compare Files: changes are highlighted in this document", rep); free(rep); }
  diff_free(&df);
  doc_redraw_all(v);
}
static void a11y(DocView *v, ULONG id)
{
  switch (id) {
  case ID_A11YCHECK: {
    A11yItem *it;
    int n;
    char *rep;
    busy(1);
    n = a11y_check(v->pdf, get_text, v, &it);
    busy(0);
    rep = a11y_report(it, n);
    dshow("Accessibility Check", rep);
    free(rep); free(it);
    return; }
  case ID_READOPTS: {
    char lang[16] = "en-GB";
    int dt = 1;
    DField df[2];
    PObj *root = pdf_dget(v->pdf, v->pdf->trailer, "Root"), *l = root ? pdf_dget(v->pdf, root, "Lang") : NULL;
    if (l && l->type == PO_STRING) { char *u = pdf_text_string(l); if (u) { to_latin1(u, lang, sizeof lang); free(u); } }
    memset(df, 0, sizeof df);
    df[0].label = "Language (e.g. en-GB, de-DE):"; df[0].kind = DF_STR; df[0].buf = lang; df[0].cap = sizeof lang;
    df[1].label = "Show the title, not the file name:"; df[1].kind = DF_CHECK; df[1].val = &dt;
    if (dform("Reading Options", df, 2, "OK")) { doc_snapshot(v); a11y_reading_options(v->pdf, lang, dt); doc_changed(); }
    return; }
  case ID_AUTOTAG: {
    int n;
    if (pdf_dget(v->pdf, pdf_dget(v->pdf, v->pdf->trailer, "Root"), "StructTreeRoot")) { say("This document is tagged already."); return; }
    doc_snapshot(v);
    busy(1);
    n = a11y_autotag(v->pdf, 1);
    busy(0);
    sayf("%ld pages were tagged (a paragraph each; form fields as fields).", n);
    doc_redraw_all(v);
    return; }
  case ID_TAGTREE: { char *t = a11y_tags(v->pdf); dshow("Tags", t); free(t); return; }
  case ID_UNTAGGED: {
    int *p, n = a11y_untagged(v->pdf, &p);
    if (!n) say("All content is tagged.");
    else { sayf("%ld pages have untagged content; the first is shown.", n); DoMethod(view, MUIM_View_GoPage, p[0], -1); }
    free(p);
    return; }
  case ID_ALTTEXT: {
    char **alts, *it[200];
    int n = a11y_figures(v->pdf, &alts), i, r;
    if (!n) { free(alts); say("No figures are tagged in this document."); return; }
    for (i = 0; i < n && i < 200; i++) { char t[200], a[150]; to_latin1(alts[i] ? alts[i] : "", a, sizeof a); snprintf(t, sizeof t, "%s %d: %s", tr("Figure"), i + 1, a[0] ? a : tr("(no alternate text)")); it[i] = str_dup(t); }
    r = dlist("Add Alternate Text", it, n < 200 ? n : 200, 0, NULL, NULL);
    if (r >= 0) {
      char buf[300] = "";
      char *u;
      if (alts[r]) to_latin1(alts[r], buf, sizeof buf);
      if (ask_string("Alternate Text", "What the figure shows:", buf, sizeof buf) && (u = u8of(buf))) { doc_snapshot(v); a11y_set_alt(v->pdf, r, u); free(u); doc_changed(); }
    }
    for (i = 0; i < n; i++) { free(alts[i]); if (i < 200) free(it[i]); }
    free(alts);
    return; }
  }
}
static void signatures(DocView *v)
{
  SigInfo *si;
  int n = sig_list(v->pdf, &si), i;
  Buf b;
  if (!n) { say("This document has no signatures."); return; }
  buf_init(&b);
  for (i = 0; i < n; i++) {
    buf_printf(&b, "%s: %s%s\n", si[i].field, si[i].name[0] ? si[i].name : tr("(signer not named)"), si[i].certify ? tr(" (certifies the document)") : "");
    if (si[i].reason[0]) buf_printf(&b, "  %s: %s\n", tr("Reason"), si[i].reason);
    if (si[i].date[0]) buf_printf(&b, "  %s: %s\n", tr("Date"), si[i].date);
    buf_printf(&b, "  %s\n", si[i].digest_ok == 1 ? tr("The signed part of the file hasn't changed since signing.") :
                             si[i].digest_ok == 0 ? tr("The signed part of the file HAS CHANGED since signing.") : tr("Folio couldn't check this signature's digest."));
    buf_printf(&b, "  %s\n", si[i].covers_all ? tr("Nothing was added after it.") : tr("The document was changed after it was signed (an update was added)."));
  }
  buf_adds(&b, "\n");
  buf_adds(&b, tr("Folio checks the digest only: it doesn't check the signer's certificate."));
  free(si);
  dshow("Signatures", b.data ? b.data : "");
  buf_free(&b);
}
static void pdfa(DocView *v)
{
  char probs[600], path[512];
  size_t len;
  u8 *data;
  doc_snapshot(v);
  busy(1);
  pdfa_convert(v->pdf, probs, sizeof probs);
  busy(0);
  base_with(v, path, sizeof path, "-pdfa.pdf");
  if (!file_request(1, "Save as PDF/A", path, sizeof path, "#?.pdf")) return;
  data = pdf_save(v->pdf, NULL, 0, &len);
  if (!data || !write_file(path, data, len)) say("The file couldn't be saved (out of memory, or the disk is full or write-protected).");
  else if (probs[0]) { char m[800]; snprintf(m, sizeof m, "%s\n\n%s", tr("Saved, but these keep it from being valid PDF/A:"), probs); dshow("Save as PDF/A", m); }
  else say("Saved as PDF/A-2B.");
  free(data);
}
static void measures(DocView *v, int export_csv)
{
  Buf b;
  int p, i;
  buf_init(&b);
  if (export_csv) buf_adds(&b, "page,kind,value\n");
  for (p = 0; p < v->pdf->npages; p++) {
    PObj *a = pdf_dget(v->pdf, v->pdf->pages[p], "Annots");
    if (a && a->type == PO_ARRAY) for (i = 0; i < a->u.arr.n; i++) {
      PObj *an = pdf_resolve(v->pdf, a->u.arr.v[i]), *c;
      const char *it;
      char *t;
      if (!an || an->type != PO_DICT || !pdf_dget(v->pdf, an, "Measure")) continue;
      it = pdf_dname(v->pdf, an, "IT");
      c = pdf_dget(v->pdf, an, "Contents");
      t = c ? pdf_text_string(c) : NULL;
      buf_printf(&b, export_csv ? "%d,%s,\"%s\"\n" : "%s %d: %s %s\n", p + 1, it ? (strstr(it, "Polygon") ? "area" : strstr(it, "PolyLine") ? "perimeter" : "distance") : "distance", t ? t : "");
      free(t);
    }
  }
  if (export_csv) save_bytes("Export measurements", v, "-measurements.csv", "#?.csv", (const u8 *)b.data, b.len);
  else dshow("Measurements", b.len ? b.data : tr("No measurements in this document."));
  buf_free(&b);
}
static void word_count(DocView *v)
{
  long words = 0, chars = 0;
  int p, i;
  char m[200];
  busy(1);
  for (p = 0; p < v->pdf->npages; p++) {
    TextPage *t = page_text(v, p);
    int inword = 0;
    if (!t) continue;
    for (i = 0; i < t->no; i++) { u32 c = t->o[i].u; if (c > 32) { chars++; if (!inword) words++; inword = 1; } else inword = 0; }
  }
  busy(0);
  snprintf(m, sizeof m, tr("%d pages, %ld words, %ld characters (not counting spaces)."), v->pdf->npages, words, chars);
  ask("OK", "%s", m);
}

/* ------------------------------------------------------------------ the Tools page's groups */
typedef struct { const char *label; ULONG id; } GItem;
static const GItem g_edit[] = { { "Add Text", ID_TOOL0 + TL_ADDTEXT }, { "Add Picture", ID_TOOL0 + TL_ADDIMAGE }, { "Add Link", ID_TOOL0 + TL_LINK },
  { "Header & Footer...", ID_HEADER }, { "Watermark...", ID_WATERMARK }, { "Background...", ID_BACKGROUND }, { "Bates Numbering...", ID_BATES },
  { "Attach File...", ID_ATTACH }, { "Rotate Pages", ID_ROTR }, { "Insert Pages...", ID_INSFILE }, { "Delete Pages...", ID_DELPAGES },
  { "Extract Pages...", ID_EXTRACT }, { "Organize Pages", ID_ORGANIZE }, { 0, 0 } };
static const GItem g_create[] = { { "Blank Document...", ID_NEWBLANK }, { "From Pictures...", ID_FROMIMAGES }, { "From a Text File...", ID_FROMTEXT },
  { "From the Clipboard", ID_FROMCLIP }, { "Pictures to Separate PDFs...", ID_SEPARATE }, { "Combine Files...", ID_COMBINE }, { 0, 0 } };
static const GItem g_fill[] = { { "Add Text", ID_TOOL0 + TL_FTEXT }, { "Checkmark", ID_TOOL0 + TL_CHECK }, { "Cross", ID_TOOL0 + TL_CROSS },
  { "Dot", ID_TOOL0 + TL_DOT }, { "Line", ID_TOOL0 + TL_LINE }, { "Date", ID_TOOL0 + TL_DATE }, { "Signature", ID_TOOL0 + TL_SIGN },
  { "Initials", ID_TOOL0 + TL_INITIALS }, { "Change Signature...", ID_SIGCHANGE }, { "Change Initials...", ID_INITCHANGE }, { 0, 0 } };
static const GItem g_form[] = { { "Field List...", ID_FIELDLIST }, { "Detect Form Fields", ID_DETECT }, { "Clear Form", ID_CLEARFORM },
  { "Field Properties...", ID_FIELDPROPS }, { "Tab Order by Rows", ID_TABROWS }, { "Tab Order by Columns", ID_TABCOLS }, { "Calculation Order...", ID_CALCORDER },
  { "Flatten Form", ID_FLATTENFORM }, { "Import Data...", ID_IMPORTDATA }, { "Export Data...", ID_EXPORTDATA }, { "Add Text Field", ID_FIELDTOOL + 0 },
  { "Add Text Field Table", ID_FIELDTOOL + 8 }, { "Add Check Box", ID_FIELDTOOL + 1 }, { "Add Radio Button", ID_FIELDTOOL + 2 }, { "Add Drop-down List", ID_FIELDTOOL + 4 },
  { "Add List Box", ID_FIELDTOOL + 5 }, { "Add Button", ID_FIELDTOOL + 3 }, { "Add Picture Field", ID_FIELDTOOL + 9 }, { "Add Date Field", ID_FIELDTOOL + 7 },
  { "Add Signature Field", ID_FIELDTOOL + 6 }, { 0, 0 } };
static const GItem g_protect[] = { { "Protect with Password...", ID_SAVEPW }, { "Remove Hidden Information...", ID_HIDDEN }, { "Security Properties...", ID_SECPROPS },
  { "Remove Security", ID_REMSEC }, { 0, 0 } };
static const GItem g_redact[] = { { "Redact Area", ID_TOOL0 + TL_REDACT }, { "Redact Page", ID_REDACTPAGE }, { "Find Text and Redact...", ID_REDACTTEXT },
  { "Redaction Properties...", ID_REDACTPROPS }, { "Apply Redactions", ID_REDACTAPPLY }, { "Sanitize Document...", ID_SANITIZE }, { 0, 0 } };
static const GItem g_compress[] = { { "Reduce File Size...", ID_REDUCE }, { "Advanced Optimization...", ID_ADVOPT }, { "Audit Space Usage...", ID_AUDIT }, { 0, 0 } };
static const GItem g_measure[] = { { "Distance", ID_TOOL0 + TL_MEASURE }, { "Perimeter", ID_TOOL0 + TL_PERIM }, { "Area", ID_TOOL0 + TL_AREA },
  { "Drawing Scale...", ID_SCALE }, { "Measurements...", ID_MEASINFO }, { "Export Measurements...", ID_MEASEXPORT }, { 0, 0 } };
static const GItem g_a11y[] = { { "Automatically Tag", ID_AUTOTAG }, { "Reading Options...", ID_READOPTS }, { "Check for Accessibility...", ID_A11YCHECK },
  { "Find Untagged Content", ID_UNTAGGED }, { "Alternate Text...", ID_ALTTEXT }, { "Tags...", ID_TAGTREE }, { 0, 0 } };
static const GItem g_print[] = { { "Fix Hairlines...", ID_HAIRLINES }, { "Convert Colours to Grey", ID_TOGRAY }, { "Add Printer Marks...", ID_MARKS },
  { "Set Page Boxes...", ID_BOXES }, { "Crop Pages...", ID_CROP }, { 0, 0 } };
static const GItem g_export[] = { { "Page as PNG Picture...", ID_EXPPAGE }, { "Text...", ID_EXPTEXT }, { "Web Page (HTML)...", ID_EXPHTML },
  { "Rich Text (RTF)...", ID_EXPRTF }, { "Word (.docx)...", ID_EXPDOCX }, { "PowerPoint (.pptx)...", ID_EXPPPTX }, { "All Pictures...", ID_EXPIMAGES }, { 0, 0 } };
static const GItem g_organize[] = { { "Organize Pages", ID_ORGANIZE }, { "Replace Pages...", ID_REPLACE }, { "Split Document...", ID_SPLIT },
  { "Duplicate Page...", ID_DUPLICATE }, { "Reverse Pages", ID_REVERSE }, { "Crop Pages...", ID_CROP }, { "Set Page Boxes...", ID_BOXES }, { "Page Labels...", ID_LABELS }, { 0, 0 } };
static const GItem g_standards[] = { { "Save as PDF/A...", ID_PDFA }, { "Signatures...", ID_SIGNATURES }, { "Compare Files...", ID_COMPARE }, { 0, 0 } };
static const GItem *const groups[13] = { g_edit, g_create, g_fill, g_form, g_protect, g_redact, g_compress, g_measure, g_a11y, g_print, g_export, g_organize, g_standards };
static const char *const group_names[13] = { "Edit a PDF", "Create a PDF", "Fill & Sign", "Prepare a Form", "Protect a PDF", "Redact a PDF", "Compress a PDF",
  "Measure Objects", "Prepare for Accessibility", "Print Production", "Export a PDF", "Organize Pages", "Standards and Signatures" };
static void group(int g)
{
  char *it[24];
  int n, r;
  for (n = 0; groups[g][n].label; n++) it[n] = (char *)tr(groups[g][n].label);
  r = dlist(group_names[g], it, n, 0, NULL, NULL);
  if (r >= 0) DoMethod(app, MUIM_Application_ReturnID, groups[g][r].id);
}

/* ------------------------------------------------------------------ the commands */
ULONG doc_strip_state(ULONG id) { return 0; }
int doc_command(ULONG id)
{
  DocView *v = cur_view();
  /* commands that need no document */
  switch (id) {
  case ID_NEWBLANK: {
    double w = 595.28, h = 841.89;
    if (!dlg_blank(&w, &h)) return 1;
    open_new(doc_new_blank(w, h, 1), "Untitled.pdf");
    return 1; }
  case ID_FROMIMAGES: open_new(from_files(0), "Pictures.pdf"); return 1;
  case ID_FROMTEXT: open_new(from_files(1), "Text.pdf"); return 1;
  case ID_FROMCLIP: {
    extern char *clip_read(void);
    char *s = clip_read(), *u;
    if (!s || !*s) { free(s); say("The clipboard holds no text."); return 1; }
    u = u8of(s);
    free(s);
    open_new(u ? doc_from_text(u, strlen(u), 595.28, 841.89, 11, 0) : NULL, "Clipboard.pdf");
    free(u);
    return 1; }
  case ID_SEPARATE: {
    /* each picture as a PDF of its own beside it */
    struct FileRequester *fr;
    int i, n = 0;
    if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr("Choose pictures: each becomes a PDF beside it"),
          ASLFR_DoMultiSelect, TRUE, ASLFR_InitialPattern, (ULONG)"#?.(jpg|jpeg|png)", ASLFR_DoPatterns, TRUE, TAG_DONE))) return 1;
    if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)xget(win, MUIA_Window_Window), TAG_DONE)) {
      busy(1);
      for (i = 0; i < (fr->fr_NumArgs > 0 ? (int)fr->fr_NumArgs : 1); i++) {
        char path[512];
        size_t len, ol;
        u8 *f, *out;
        const u8 *fl[1];
        size_t ll[1];
        PDoc *d;
        strncpy(path, (const char *)fr->fr_Drawer, sizeof path - 1); path[sizeof path - 1] = 0;
        AddPart((STRPTR)path, (STRPTR)(fr->fr_NumArgs > 0 ? (const char *)fr->fr_ArgList[i].wa_Name : (const char *)fr->fr_File), sizeof path);
        if (!(f = read_file(path, &len))) continue;
        fl[0] = f; ll[0] = len;
        d = doc_from_images(fl, ll, 1, 0, 0);
        free(f);
        if (!d) continue;
        out = pdf_save(d, NULL, 0, &ol);
        pdf_close(d);
        { char *dot = strrchr((char *)FilePart((STRPTR)path), '.'); if (dot) *dot = 0; strncat(path, ".pdf", sizeof path - 1 - strlen(path)); }
        if (out && write_file(path, out, ol)) n++;
        free(out);
      }
      busy(0);
      sayf("%ld PDF files were made.", n);
    }
    MUI_FreeAslRequest(fr);
    return 1; }
  case ID_SIGCHANGE: case ID_INITCHANGE:
    if (id == ID_SIGCHANGE) ask_string("Signature", "Your name, to sign with:", opt.signature, sizeof opt.signature);
    else ask_string("Initials", "Your initials:", opt.initials, sizeof opt.initials);
    return 1;
  case ID_SCALE: {
    char s[16], u[8];
    DField df[2];
    sprintf(s, "%g", opt.scale > 0 ? 72.0 / opt.scale : 1);
    strcpy(u, opt.unit);
    memset(df, 0, sizeof df);
    df[0].label = "An inch on the page is (units):"; df[0].kind = DF_STR; df[0].buf = s; df[0].cap = sizeof s;
    df[1].label = "Unit:"; df[1].kind = DF_STR; df[1].buf = u; df[1].cap = sizeof u;
    if (dform("Drawing Scale", df, 2, "OK") && atof(s) > 0) { opt.scale = 72.0 / atof(s); strcpy(opt.unit, u[0] ? u : "in"); }
    return 1; }
  case ID_STAMPPICK: stamp_pick(); if (opt.stamp[0] && v) DoMethod(app, MUIM_Application_ReturnID, ID_TOOL0 + TL_STAMP); return 1;
  }
  if (id >= ID_GROUP0 && id < ID_GROUP0 + 13) { group((int)(id - ID_GROUP0)); return 1; }
  if (id >= ID_FIELDTOOL && id < ID_FIELDTOOL + 10) {
    if (!v) return 1;
    opt.field_kind = (int)(id - ID_FIELDTOOL);
    set_tool(TL_FIELD);
    return 1;
  }
  if (id < ID_NEWBLANK || id >= ID_FIELDTOOL + 10) return 0;
  if (!v) { say("Open a document first."); return 1; }
  switch (id) {
  case ID_EXPHTML: case ID_EXPRTF: case ID_EXPDOCX: case ID_EXPPPTX: case ID_EXPIMAGES: export_as(v, id); return 1;
  case ID_SAVEPW: save_with_password(v); return 1;
  case ID_SECPROPS: security_props(v); return 1;
  case ID_REMSEC:
    if (!v->was_encrypted) say("This document isn't protected.");
    else { v->was_encrypted = 2; v->pdf->changed = 1; say("Save the document now: it is saved without its protection."); refresh_bars(); }
    return 1;
  case ID_REDUCE: reduce(v, 0); return 1;
  case ID_ADVOPT: reduce(v, 1); return 1;
  case ID_AUDIT: audit(v); return 1;
  case ID_PDFA: pdfa(v); return 1;
  case ID_WORDCOUNT: word_count(v); return 1;
  case ID_HEADER: header_footer(v, 0); return 1;
  case ID_BATES: header_footer(v, 1); return 1;
  case ID_HEADERDEL: remove_mark(v, "header", "headers or footers"); return 1;
  case ID_BATESDEL: remove_mark(v, "bates", "Bates numbers"); return 1;
  case ID_WATERMARK: watermark(v); return 1;
  case ID_WATERMARKDEL: remove_mark(v, "watermark", "watermarks"); return 1;
  case ID_BACKGROUND: background(v); return 1;
  case ID_BACKGROUNDDEL: remove_mark(v, "background", "backgrounds"); return 1;
  case ID_ATTACH: attachments(v, 1); return 1;
  case ID_ATTACHMENTS: attachments(v, 0); return 1;
  case ID_BMADD: bookmarks(v, 0); return 1;
  case ID_BMRENAME: bookmarks(v, 1); return 1;
  case ID_BMDELETE: bookmarks(v, 2); return 1;
  case ID_BMSTYLES: bookmarks(v, 3); return 1;
  case ID_LABELS: labels(v); return 1;
  case ID_CROP: crop_dialog(v); return 1;
  case ID_BOXES: boxes_dialog(v); return 1;
  case ID_COMPARE: compare(v); return 1;
  case ID_A11YCHECK: case ID_READOPTS: case ID_AUTOTAG: case ID_TAGTREE: case ID_ALTTEXT: case ID_UNTAGGED: a11y(v, id); return 1;
  case ID_SIGNATURES: signatures(v); return 1;
  case ID_HAIRLINES: { char w[12] = "0.25"; if (ask_string("Fix Hairlines", "Lines thinner than this (points) made this wide:", w, sizeof w) && atof(w) > 0) { int n; doc_snapshot(v); busy(1); n = fix_hairlines(v->pdf, atof(w), atof(w)); busy(0); sayf("%ld pages had hairlines fixed.", n); doc_redraw_all(v); } return 1; }
  case ID_TOGRAY: if (yes("Convert the document's colours (and colour pictures) to grey?")) { doc_snapshot(v); busy(1); convert_to_gray(v->pdf); busy(0); doc_redraw_all(v); } return 1;
  case ID_MARKS: { char b[12] = "3"; if (ask_string("Add Printer Marks", "Bleed (mm):", b, sizeof b)) { doc_snapshot(v); printer_marks(v->pdf, atof(b) * 72 / 25.4); doc_redraw_all(v); } return 1; }
  case ID_FLATTENCOMMENTS: if (yes("Flatten the comments into the pages? They can't be edited afterwards.")) { doc_snapshot(v); busy(1); annots_flatten(v->pdf, -1); busy(0); doc_redraw_all(v); } return 1;
  case ID_MEASINFO: measures(v, 0); return 1;
  case ID_MEASEXPORT: measures(v, 1); return 1;
  case ID_DUPLICATE: { char c[8] = "1"; if (ask_string("Duplicate Page", "Copies of the page in view:", c, sizeof c) && atoi(c) > 0) { doc_snapshot(v); page_duplicate(v->pdf, v->page, atoi(c)); doc_redraw_all(v); } return 1; }
  case ID_REVERSE: if (yes("Reverse the order of the pages?")) { doc_snapshot(v); page_reverse(v->pdf, 0, -1); doc_redraw_all(v); } return 1;
  case ID_REPLACE: replace_pages(v); return 1;
  case ID_SPLIT: split(v); return 1;
  case ID_FIELDLIST: { FForm fm; int i = pick_field(v, &fm, "Fields: double-click to go to one"); if (i >= 0 && fm.f[i].nw && fm.f[i].w[0].page >= 0) DoMethod(view, MUIM_View_GoPage, fm.f[i].w[0].page, (LONG)fm.f[i].w[0].rect[3] + 20); form_free(&fm); return 1; }
  case ID_DETECT: {
    int n;
    doc_snapshot(v);
    busy(1);
    n = form_detect(v->pdf, v->page, page_text(v, v->page));
    busy(0);
    sayf("%ld fields were made on this page.", n);
    page_changed(v, v->page); doc_changed();
    return 1; }
  case ID_CLEARFORM: { FForm fm; if (form_load(v->pdf, &fm) && yes("Clear every field of the form?")) { doc_snapshot(v); form_reset(v->pdf, &fm); doc_redraw_all(v); } form_free(&fm); return 1; }
  case ID_FIELDPROPS: field_props(v); return 1;
  case ID_TABROWS: case ID_TABCOLS: doc_snapshot(v); form_tab_order(v->pdf, id == ID_TABCOLS); say("The fields' tab order was set."); doc_changed(); return 1;
  case ID_CALCORDER: {
    /* the fields with calculations: their order, one moved first at a time */
    FForm fm;
    int i = pick_field(v, &fm, "Calculation Order: the field to calculate first"), k, *ord;
    if (i >= 0 && (ord = (int *)malloc((size_t)fm.n * sizeof(int)))) {
      ord[0] = i;
      for (k = 0, i = 1; k < fm.n; k++) if (k != ord[0]) ord[i++] = k;
      doc_snapshot(v);
      form_calc_order(v->pdf, &fm, ord, fm.n);
      free(ord);
      say("The calculation order was set (Folio itself doesn't run calculations: they need JavaScript).");
    }
    form_free(&fm);
    return 1; }
  case ID_FLATTENFORM: { FForm fm; if (form_load(v->pdf, &fm) && yes("Flatten the form? Its fields become part of the pages and can't be filled in afterwards.")) { doc_snapshot(v); busy(1); form_flatten(v->pdf, &fm); busy(0); doc_redraw_all(v); } else if (!fm.n) say("This document has no form fields."); form_free(&fm); return 1; }
  case ID_IMPORTDATA: form_data(v, 1); return 1;
  case ID_EXPORTDATA: form_data(v, 0); return 1;
  case ID_HIDDEN: hidden_info(v, 0); return 1;
  case ID_SANITIZE: hidden_info(v, 1); return 1;
  case ID_REDACTTEXT: redact_text(v); return 1;
  case ID_REDACTPAGE: if (redact_mark_page(v->pdf, v->page, rp_get())) added(v, v->page); return 1;
  case ID_REDACTAPPLY: apply_redactions(v); return 1;
  case ID_REDACTPROPS: redact_props(); return 1;
  case ID_REVERT: {
    int enc;
    PDoc *d;
    if (!v->path[0] || !v->pdf->changed) { say("There are no changes to throw away."); return 1; }
    if (!yes("Throw away the changes and go back to the file as it was saved?")) return 1;
    if (!(d = open_pdf(v->path, &enc))) return 1;
    DoMethod(view, MUIM_View_Forget, (ULONG)v, 0);
    if (v->rc) rcache_free(v->rc);
    pdf_close(v->pdf);
    v->pdf = d; v->rc = rcache_new(d); v->was_encrypted = enc;
    undo_clear(v);
    doc_redraw_all(v);
    return 1; }
  }
  return 0;
}
