/*
 * dialogs.c - Folio's dialogs: the document's properties (its description to edit, and what it
 * is made of), going to a page, a blank page's size, exporting a page as a picture or the text
 * as a file, extracting pages into a new PDF, and the password a protected document asks for.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/pdfwrite.h"
#include "../core/pngout.h"

static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static Object *field(const char *contents, int max)
{
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)contents, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End;
}
static Object *info_text(const char *s) { return TextObject, MUIA_Text_Contents, (ULONG)s, MUIA_Text_SetMin, FALSE, End; }
static char *info_latin1(PDoc *d, const char *key, char *out, size_t cap)
{
  char *u = pdf_info(d, key);
  out[0] = 0;
  if (u) { to_latin1(u, out, cap); free(u); }
  return out;
}
/* a PDF date (D:YYYYMMDDHHmmSS...) as YYYY-MM-DD HH:MM */
static void nice_date(const char *s, char *out)
{
  if (!s[0]) { strcpy(out, "-"); return; }
  if (s[0] == 'D' && s[1] == ':') s += 2;
  if (strlen(s) >= 12) sprintf(out, "%.4s-%.2s-%.2s %.2s:%.2s", s, s + 4, s + 6, s + 8, s + 10);
  else if (strlen(s) >= 8) sprintf(out, "%.4s-%.2s-%.2s", s, s + 4, s + 6);
  else strcpy(out, s);
}

/* ------------------------------------------------------------------ properties */
void dlg_properties(void)
{
  DocView *v = cur_view();
  static const char *const keys[] = { "Title", "Author", "Subject", "Keywords" };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *f[4], *grp;
  char cur[4][256], producer[160], creator[160], created[40], modified[40], tmp[80], size[60], pages[24], ver[16], file[160], sec[80];
  int i;
  double box[4];
  int rot;
  if (!v) return;
  for (i = 0; i < 4; i++) info_latin1(v->pdf, keys[i], cur[i], sizeof cur[i]);
  info_latin1(v->pdf, "Producer", producer, sizeof producer);
  info_latin1(v->pdf, "Creator", creator, sizeof creator);
  nice_date(info_latin1(v->pdf, "CreationDate", tmp, sizeof tmp), created);
  nice_date(info_latin1(v->pdf, "ModDate", tmp, sizeof tmp), modified);
  pdf_page_box(v->pdf, v->page, box, &rot);
  sprintf(size, "%.0f x %.0f mm (%.0f x %.0f pt)", (box[2] - box[0]) * 25.4 / 72, (box[3] - box[1]) * 25.4 / 72, box[2] - box[0], box[3] - box[1]);
  sprintf(pages, "%d", v->pdf->npages);
  ver[0] = 0;
  if (v->pdf->len > 8 && !memcmp(v->pdf->data, "%PDF-", 5)) { memcpy(ver, v->pdf->data + 5, 3); ver[3] = 0; }
  sprintf(file, "%s (%lu KB)", v->path[0] ? (const char *)FilePart((STRPTR)v->path) : v->name, (unsigned long)((v->pdf->len + 1023) / 1024));
  strcpy(sec, v->was_encrypted ? tr(v->pdf->crypt == 3 ? "Password protected (AES-256)" : v->pdf->crypt == 2 ? "Password protected (AES-128)" : "Password protected (RC4)") : tr("None"));
  grp = VGroup,
    Child, QTitleObject(tr("Description")),
    Child, ColGroup(2),
      Child, QLabel(tr("Title:"), MUIO_Label_SingleFrame), Child, f[0] = field(cur[0], 250),
      Child, QLabel(tr("Author:"), MUIO_Label_SingleFrame), Child, f[1] = field(cur[1], 250),
      Child, QLabel(tr("Subject:"), MUIO_Label_SingleFrame), Child, f[2] = field(cur[2], 250),
      Child, QLabel(tr("Keywords:"), MUIO_Label_SingleFrame), Child, f[3] = field(cur[3], 250),
    End,
    Child, QTitleObject(tr("Document")),
    Child, ColGroup(2),
      Child, QLabel(tr("File:"), 0), Child, info_text(file),
      Child, QLabel(tr("Pages:"), 0), Child, info_text(pages),
      Child, QLabel(tr("Page size:"), 0), Child, info_text(size),
      Child, QLabel(tr("PDF version:"), 0), Child, info_text(ver[0] ? ver : "?"),
      Child, QLabel(tr("Created:"), 0), Child, info_text(created),
      Child, QLabel(tr("Modified:"), 0), Child, info_text(modified),
      Child, QLabel(tr("Application:"), 0), Child, info_text(creator[0] ? creator : "-"),
      Child, QLabel(tr("PDF producer:"), 0), Child, info_text(producer[0] ? producer : "-"),
      Child, QLabel(tr("Security:"), 0), Child, info_text(sec),
    End,
  End;
  if (!(w = dialog_window("Document Properties", grp, "OK"))) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)f[0]);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    for (i = 0; i < 4; i++) {
      const char *s = (const char *)xget(f[i], MUIA_String_Contents);
      if (strcmp(s, cur[i])) {
        char u[600];
        size_t n = 0, k;
        for (k = 0; s[k] && n + 3 < sizeof u; k++) n += (size_t)utf8_put(u + n, (u8)s[k]);
        u[n] = 0;
        pdf_set_info(v->pdf, keys[i], u);
      }
    }
    doc_changed();
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ go to a page */
void dlg_goto(void)
{
  DocView *v = cur_view();
  char buf[16], label[60];
  int p;
  if (!v) return;
  sprintf(buf, "%d", v->page + 1);
  sprintf(label, tr("Page (1 to %d):"), v->pdf->npages);
  if (!ask_string("Go to Page", label, buf, sizeof buf)) return;
  p = atoi(buf);
  if (p < 1) p = 1;
  if (p > v->pdf->npages) p = v->pdf->npages;
  DoMethod(view, MUIM_View_GoPage, p - 1, -1);
}

/* ------------------------------------------------------------------ a blank page */
int dlg_blank(double *w, double *h)
{
  static const char *labels[] = { "Same as the page in view", "A4", "A5", "Letter", "Legal", NULL };
  static const char *orient[] = { "Portrait", "Landscape", NULL };
  static const double sizes[][2] = { { 0, 0 }, { 595.28, 841.89 }, { 419.53, 595.28 }, { 612, 792 }, { 612, 1008 } };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static int last_size, last_orient;
  DocView *v = cur_view();
  Object *win2, *cy, *co;
  int ok = 0, i;
  static const char *tl[6], *to[3];
  for (i = 0; labels[i]; i++) tl[i] = tr(labels[i]);
  tl[i] = NULL;
  for (i = 0; orient[i]; i++) to[i] = tr(orient[i]);
  to[i] = NULL;
  win2 = dialog_window("Insert Blank Page", ColGroup(2),
    Child, QLabel(tr("Page size:"), MUIO_Label_SingleFrame), Child, cy = CycleObject, MUIA_Cycle_Entries, (ULONG)tl, MUIA_Cycle_Active, last_size, MUIA_CycleChain, 1, End,
    Child, QLabel(tr("Orientation:"), MUIO_Label_SingleFrame), Child, co = CycleObject, MUIA_Cycle_Entries, (ULONG)to, MUIA_Cycle_Active, last_orient, MUIA_CycleChain, 1, End,
  End, "Insert");
  if (!win2) return 0;
  if (run_modal(win2, ends, NULL) == ID_DLGOK) {
    int s = (int)xget(cy, MUIA_Cycle_Active), o = (int)xget(co, MUIA_Cycle_Active);
    last_size = s; last_orient = o;
    if (s == 0 && v) {
      double box[4];
      int rot;
      pdf_page_box(v->pdf, v->page, box, &rot);
      *w = box[2] - box[0]; *h = box[3] - box[1];
      if (rot == 90 || rot == 270) { double t = *w; *w = *h; *h = t; }
    } else { *w = sizes[s][0]; *h = sizes[s][1]; }
    if (s && o == 1) { double t = *w; *w = *h; *h = t; }
    if (s == 0 && ((o == 1) != (*w > *h))) { double t = *w; *w = *h; *h = t; }
    ok = 1;
  }
  MUI_DisposeObject(win2);
  return ok;
}

/* ------------------------------------------------------------------ export */
static void base_with(DocView *v, char *path, size_t cap, const char *ext)
{
  char *dot;
  strncpy(path, v->path[0] ? v->path : v->name, cap - 1); path[cap - 1] = 0;
  dot = strrchr((char *)FilePart((STRPTR)path), '.');
  if (dot) *dot = 0;
  strncat(path, ext, cap - 1 - strlen(path));
}
void dlg_export_page(void)
{
  DocView *v = cur_view();
  static const char *res[] = { "72 dpi (screen)", "100 dpi", "150 dpi", "300 dpi (print)", NULL };
  static const int dpis[] = { 72, 100, 150, 300 };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static int last = 1;
  static const char *tres[5];
  Object *w, *cy;
  char path[512], ext[24];
  int i, dpi;
  if (!v) return;
  for (i = 0; res[i]; i++) tres[i] = tr(res[i]);
  tres[i] = NULL;
  w = dialog_window("Export Page as PNG", ColGroup(2),
    Child, QLabel(tr("Resolution:"), MUIO_Label_SingleFrame), Child, cy = CycleObject, MUIA_Cycle_Entries, (ULONG)tres, MUIA_Cycle_Active, last, MUIA_CycleChain, 1, End,
  End, "Export");
  if (!w) return;
  if (run_modal(w, ends, NULL) != ID_DLGOK) { MUI_DisposeObject(w); return; }
  last = (int)xget(cy, MUIA_Cycle_Active);
  MUI_DisposeObject(w);
  dpi = dpis[last];
  sprintf(ext, "-%d.png", v->page + 1);
  base_with(v, path, sizeof path, ext);
  if (!file_request(1, "Export the page as", path, sizeof path, "#?.png")) return;
  set(app, MUIA_Application_Sleep, TRUE);
  {
    RenderOpts o;
    Pixmap pm;
    u8 *png = NULL;
    size_t len = 0;
    memset(&o, 0, sizeof o);
    memset(&pm, 0, sizeof pm);
    o.scale = dpi / 72.0; o.annots = 1; o.paper = 0xFFFFFF; o.rotate = v->rot * 90;
    if (pdf_render(v->pdf, v->rc, v->page, &o, &pm) && pm.rgb) png = png_write_rgb(pm.rgb, pm.w, pm.h, dpi, &len);
    free(pm.rgb);
    set(app, MUIA_Application_Sleep, FALSE);
    if (!png || !write_file(path, png, len)) ask("OK", "%s", tr("The page couldn't be exported (out of memory, or the disk is full or write-protected)."));
    free(png);
  }
}
void dlg_export_text(void)
{
  DocView *v = cur_view();
  char path[512];
  Buf b;
  int p, ok;
  if (!v) return;
  base_with(v, path, sizeof path, ".txt");
  if (!file_request(1, "Save the text as", path, sizeof path, "#?.txt")) return;
  set(app, MUIA_Application_Sleep, TRUE);
  buf_init(&b);
  for (p = 0; p < v->pdf->npages; p++) {
    TextPage *t = page_text(v, p);
    if (t && t->no) {
      char *u = textpage_text(t, 0, t->no - 1);
      size_t n = strlen(u);
      char *lat = (char *)malloc(n * 3 + 4);
      if (lat) { size_t m = utf8_to_latin1(u, n, lat, n * 3 + 4); buf_add(&b, lat, m); free(lat); }
      free(u);
    }
    buf_adds(&b, "\n\f\n");
  }
  ok = !b.oom && write_file(path, (const u8 *)(b.data ? b.data : ""), b.len);
  buf_free(&b);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!ok) ask("OK", "%s", tr("The text couldn't be saved (out of memory, or the disk is full or write-protected)."));
}

/* ------------------------------------------------------------------ extract pages */
void dlg_extract(void)
{
  DocView *v = cur_view();
  char range[64], path[512];
  int *order, n = 0, i, a, b;
  u8 *data;
  size_t len;
  if (!v) return;
  /* the pages picked (organizing), else the one in view, as the range offered */
  a = b = -1;
  if (v->mode == VM_ORGANIZE && v->picked) for (i = 0; i < v->pdf->npages; i++) if (v->picked[i]) { if (a < 0) a = i; b = i; }
  if (a < 0) a = b = v->page;
  if (a == b) sprintf(range, "%d", a + 1); else sprintf(range, "%d-%d", a + 1, b + 1);
  if (!ask_string("Extract Pages", "Pages (for example 2-5, 8):", range, sizeof range)) return;
  if (!(order = (int *)malloc((size_t)(v->pdf->npages + 1) * sizeof(int)))) return;
  {
    const char *s = range;
    while (*s) {
      int x, y;
      while (*s == ' ' || *s == ',') s++;
      if (*s < '0' || *s > '9') break;
      x = y = (int)strtol(s, (char **)&s, 10);
      while (*s == ' ') s++;
      if (*s == '-') { s++; while (*s == ' ') s++; y = *s >= '0' && *s <= '9' ? (int)strtol(s, (char **)&s, 10) : v->pdf->npages; }
      if (x < 1) x = 1;
      if (y > v->pdf->npages) y = v->pdf->npages;
      for (i = x; i <= y && n < v->pdf->npages; i++) order[n++] = i - 1;
    }
  }
  if (!n) { free(order); ask("OK", "%s", tr("No pages were given.")); return; }
  base_with(v, path, sizeof path, "-extract.pdf");
  if (!file_request(1, "Save the pages as", path, sizeof path, "#?.pdf")) { free(order); return; }
  set(app, MUIA_Application_Sleep, TRUE);
  data = pdf_save(v->pdf, order, n, &len);
  free(order);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!data || !write_file(path, data, len)) ask("OK", "%s", tr("The pages couldn't be saved (out of memory, or the disk is full or write-protected)."));
  free(data);
}

/* ------------------------------------------------------------------ the password */
int dlg_password(const char *name, char *pw, int cap, int again)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *str;
  char msg[300];
  int ok;
  snprintf(msg, sizeof msg, tr(again ? "That password isn't right.\nEnter the password that opens %s:" : "%s is protected.\nEnter the password that opens it:"), name);
  w = dialog_window("Password", VGroup,
    Child, HGroup, Child, TextObject, MUIA_Text_Contents, (ULONG)msg, End, Child, HSpace(0), End,
    Child, HGroup, Child, QLabel(tr("Password:"), MUIO_Label_SingleFrame),
      Child, str = StringObject, StringFrame, MUIA_String_Secret, TRUE, MUIA_String_MaxLen, cap, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMM", End,
    End,
  End, "Open");
  if (!w) return 0;
  DoMethod(str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)str);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) { strncpy(pw, (const char *)xget(str, MUIA_String_Contents), cap - 1); pw[cap - 1] = 0; }
  MUI_DisposeObject(w);
  return ok;
}
