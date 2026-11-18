/*
 * review.c - comparing, accessibility, signatures and PDF/A (see review.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "review.h"
#include "pagecontent.h"
#include "pagetools.h"
#include "pdfedit.h"
#include "redact.h"
#include "crypto.h"
#include "pki.h"

/* ------------------------------------------------------------------ compare */
typedef struct { char *s; int page; u32 h; double box[4]; } Line;
typedef struct { Line *l; int n, cap; } Lines;
static void lines_of(PDoc *d, ReviewText get, void *ud, Lines *ls)
{
  int p, i;
  memset(ls, 0, sizeof *ls);
  for (p = 0; p < d->npages; p++) {
    TextPage *t = get(ud, p);
    int a = 0;
    if (!t) continue;
    for (i = 0; i <= t->no; i++) {
      char *s;
      size_t l;
      int j;
      Line *ln;
      if (i < t->no && t->o[i].u != 10) continue;
      if (i <= a) { a = i + 1; continue; }
      s = textpage_text(t, a, i - 1);
      l = s ? strlen(s) : 0;
      while (l && (s[l - 1] == ' ' || s[l - 1] == '\n')) s[--l] = 0;
      if (!s || !l) { free(s); a = i + 1; continue; }
      if (ls->n >= ls->cap) { int nc = ls->cap ? ls->cap * 2 : 256; Line *nl = (Line *)realloc(ls->l, (size_t)nc * sizeof(Line)); if (!nl) { free(s); return; } ls->l = nl; ls->cap = nc; }
      ln = &ls->l[ls->n++];
      ln->s = s; ln->page = p;
      ln->h = 2166136261u;
      for (j = 0; s[j]; j++) { ln->h ^= (u8)s[j]; ln->h *= 16777619u; }
      ln->box[0] = ln->box[1] = 1e30; ln->box[2] = ln->box[3] = -1e30;
      for (j = a; j < i; j++) {
        TChar *c = &t->o[j];
        if (c->x1 <= c->x0) continue;
        if (c->x0 < ln->box[0]) ln->box[0] = c->x0;
        if (c->y0 < ln->box[1]) ln->box[1] = c->y0;
        if (c->x1 > ln->box[2]) ln->box[2] = c->x1;
        if (c->y1 > ln->box[3]) ln->box[3] = c->y1;
      }
      a = i + 1;
    }
  }
}
static void lines_free(Lines *ls) { int i; for (i = 0; i < ls->n; i++) free(ls->l[i].s); free(ls->l); }
static int same(const Line *a, const Line *b) { return a->h == b->h && !strcmp(a->s, b->s); }
static void add_change(Diff *df, int kind, int pa, int pb, const char *text)
{
  Change *nc = (Change *)realloc(df->c, (size_t)(df->n + 1) * sizeof(Change));
  if (!nc) return;
  df->c = nc;
  df->c[df->n].kind = kind; df->c[df->n].page_a = pa; df->c[df->n].page_b = pb;
  df->c[df->n].text = str_dup(text);
  df->n++;
}
int compare_docs(PDoc *a, ReviewText ga, void *uda, PDoc *b, ReviewText gb, void *udb, int mark, Diff *df)
{
  Lines la, lb;
  int s = 0, ea, eb, n, m, i, j;
  u16 *tab = NULL;
  memset(df, 0, sizeof *df);
  df->pages_a = a->npages; df->pages_b = b->npages;
  lines_of(a, ga, uda, &la); lines_of(b, gb, udb, &lb);
  /* the common start and end left out of the table */
  while (s < la.n && s < lb.n && same(&la.l[s], &lb.l[s])) s++;
  ea = la.n; eb = lb.n;
  while (ea > s && eb > s && same(&la.l[ea - 1], &lb.l[eb - 1])) { ea--; eb--; }
  n = ea - s; m = eb - s;
  if (n > 0 && m > 0 && (long)(n + 1) * (m + 1) <= 2000000L && n < 65535 && m < 65535) tab = (u16 *)calloc((size_t)(n + 1) * (m + 1), sizeof(u16));
  if (tab) {
    /* the longest common subsequence, from the end */
    for (i = n - 1; i >= 0; i--) for (j = m - 1; j >= 0; j--)
      tab[i * (m + 1) + j] = same(&la.l[s + i], &lb.l[s + j]) ? (u16)(tab[(i + 1) * (m + 1) + j + 1] + 1) :
                             tab[(i + 1) * (m + 1) + j] >= tab[i * (m + 1) + j + 1] ? tab[(i + 1) * (m + 1) + j] : tab[i * (m + 1) + j + 1];
    i = 0; j = 0;
    while (i < n || j < m) {
      if (i < n && j < m && same(&la.l[s + i], &lb.l[s + j])) { i++; j++; }
      else if (j < m && (i == n || tab[i * (m + 1) + j + 1] >= tab[(i + 1) * (m + 1) + j])) { add_change(df, 1, -1, lb.l[s + j].page, lb.l[s + j].s); df->c[df->n - 1].page_a = s + j; j++; }
      else { add_change(df, 2, la.l[s + i].page, j < m ? lb.l[s + j].page : (eb < lb.n ? lb.l[eb].page : b->npages - 1), la.l[s + i].s); df->c[df->n - 1].page_a = la.l[s + i].page; i++; }
    }
    free(tab);
  } else {
    /* too long to tabulate: everything between the common parts is changed */
    for (i = s; i < ea; i++) add_change(df, 2, la.l[i].page, eb > s ? lb.l[s].page : 0, la.l[i].s);
    for (j = s; j < eb; j++) { add_change(df, 1, -1, lb.l[j].page, lb.l[j].s); df->c[df->n - 1].page_a = j; }
  }
  /* (for insertions page_a held the line's index in b, for marking) */
  if (mark) for (i = 0; i < df->n; i++) {
    Change *c = &df->c[i];
    if (c->kind == 1) {
      Line *ln = &lb.l[c->page_a];
      if (ln->box[2] > ln->box[0]) {
        double q[8];
        q[0] = ln->box[0]; q[1] = ln->box[3]; q[2] = ln->box[2]; q[3] = ln->box[3]; q[4] = ln->box[0]; q[5] = ln->box[1]; q[6] = ln->box[2]; q[7] = ln->box[1];
        annot_markup(b, ln->page, AN_HIGHLIGHT, q, 1, 0x7FD0FF, "Compare", "Inserted");
      }
    } else {
      char t[300];
      double box[4];
      int rot;
      snprintf(t, sizeof t, "Deleted: %.280s", c->text);
      pdf_page_box(b, c->page_b, box, &rot);
      annot_note(b, c->page_b, box[0] + 4, box[3] - 24 - (i % 20) * 4, 0xFF6060, "Compare", t);
    }
  }
  for (i = 0; i < df->n; i++) if (df->c[i].kind == 1) df->c[i].page_a = -1;
  lines_free(&la); lines_free(&lb);
  return df->n;
}
void diff_free(Diff *df) { int i; for (i = 0; i < df->n; i++) free(df->c[i].text); free(df->c); memset(df, 0, sizeof *df); }
char *diff_report(const Diff *df)
{
  Buf b;
  int i, ins = 0, del = 0;
  buf_init(&b);
  for (i = 0; i < df->n; i++) { if (df->c[i].kind == 1) ins++; else del++; }
  buf_printf(&b, "Pages: %d and %d. Lines inserted: %d, deleted: %d.\n", df->pages_a, df->pages_b, ins, del);
  for (i = 0; i < df->n; i++) {
    if (df->c[i].kind == 1) buf_printf(&b, "+ p.%d: %s\n", df->c[i].page_b + 1, df->c[i].text ? df->c[i].text : "");
    else buf_printf(&b, "- p.%d: %s\n", df->c[i].page_a + 1, df->c[i].text ? df->c[i].text : "");
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}

/* ------------------------------------------------------------------ accessibility */
static PObj *root_of(PDoc *d) { return pdf_dget(d, d->trailer, "Root"); }
static void add_item(A11yItem **it, int *n, const char *rule, int status, const char *detail)
{
  A11yItem *ni = (A11yItem *)realloc(*it, (size_t)(*n + 1) * sizeof(A11yItem));
  if (!ni) return;
  *it = ni;
  ni[*n].rule = rule; ni[*n].status = status;
  snprintf(ni[*n].detail, sizeof ni[*n].detail, "%s", detail ? detail : "");
  (*n)++;
}
typedef struct { int figures, figures_alt, elems, tables, headings; } StructCount;
static void walk_struct(PDoc *d, PObj *e, StructCount *c, int depth, Buf *out, int *fig, int want, const char *setalt, char ***alts, int *nalts)
{
  PObj *k;
  const char *s;
  int i;
  e = pdf_resolve(d, e);
  if (!e || e->type != PO_DICT || depth > 40) return;
  s = pdf_dname(d, e, "S");
  if (s) {
    PObj *alt = pdf_dget(d, e, "Alt");
    c->elems++;
    if (!strcmp(s, "Figure")) {
      c->figures++;
      if (alt) c->figures_alt++;
      if (alts) {
        char **na = (char **)realloc(*alts, (size_t)(*nalts + 1) * sizeof(char *));
        if (na) { *alts = na; na[*nalts] = alt && alt->type == PO_STRING ? pdf_text_string(alt) : NULL; (*nalts)++; }
      }
      if (want >= 0 && *fig == want && setalt) { pobj_dict_set(e, "Alt", om_text(setalt)); d->changed = 1; }
      (*fig)++;
    }
    if (!strcmp(s, "Table")) c->tables++;
    if (s[0] == 'H' && (s[1] == 0 || (s[1] >= '1' && s[1] <= '6'))) c->headings++;
    if (out) {
      int j;
      for (j = 0; j < depth; j++) buf_adds(out, "  ");
      buf_adds(out, s);
      if (alt && alt->type == PO_STRING) { char *a = pdf_text_string(alt); buf_printf(out, " (alt: %s)", a ? a : ""); free(a); }
      buf_addc(out, '\n');
    }
  }
  k = pdf_dget(d, e, "K");
  if (!k) return;
  if (k->type == PO_ARRAY) { for (i = 0; i < k->u.arr.n; i++) walk_struct(d, k->u.arr.v[i], c, depth + (s ? 1 : 0), out, fig, want, setalt, alts, nalts); }
  else if (k->type == PO_DICT && !pobj_is_name(pdf_dget(d, k, "Type"), "MCR") && !pobj_is_name(pdf_dget(d, k, "Type"), "OBJR")) walk_struct(d, k, c, depth + (s ? 1 : 0), out, fig, want, setalt, alts, nalts);
}
static int content_tagged(PDoc *d, int p)
{
  size_t n, i;
  u8 *c = pc_content(d, d->pages[p], &n);
  int r = 0;
  for (i = 0; c && i + 5 <= n; i++) if (!memcmp(c + i, "/MCID", 5)) { r = 1; break; }
  if (!c || !n) r = 1;                                 /* (an empty page has nothing to tag) */
  free(c);
  return r;
}
int a11y_check(PDoc *d, ReviewText get, void *ud, A11yItem **out)
{
  PObj *root = root_of(d), *mi = root ? pdf_dget(d, root, "MarkInfo") : NULL, *st = root ? pdf_dget(d, root, "StructTreeRoot") : NULL, *vp, *lang;
  int n = 0, p, i, fig = 0, notext = 0, unemb = 0, links = 0, links_ok = 0, fields = 0, fields_ok = 0, tabs_ok = 1, untagged = 0;
  StructCount sc;
  char t[120], *title = pdf_info(d, "Title");
  *out = NULL;
  memset(&sc, 0, sizeof sc);
  if (st) walk_struct(d, st, &sc, 0, NULL, &fig, -1, NULL, NULL, NULL);
  add_item(out, &n, "Tagged PDF", st && mi && pdf_dget(d, mi, "Marked") && pdf_dget(d, mi, "Marked")->type == PO_BOOL && pdf_dget(d, mi, "Marked")->u.b ? A11Y_PASS : A11Y_FAIL,
           st ? "the document has a structure tree" : "the document isn't tagged (Automatically Tag PDF tags it)");
  for (p = 0; p < d->npages; p++) if (!content_tagged(d, p)) untagged++;
  snprintf(t, sizeof t, untagged ? "%d pages have content outside the tags" : "all content is tagged", untagged);
  add_item(out, &n, "Tagged content", untagged ? A11Y_FAIL : A11Y_PASS, t);
  lang = root ? pdf_dget(d, root, "Lang") : NULL;
  add_item(out, &n, "Primary language", lang ? A11Y_PASS : A11Y_FAIL, lang ? "set" : "no language set (Change Reading Options)");
  vp = root ? pdf_dget(d, root, "ViewerPreferences") : NULL;
  add_item(out, &n, "Title", title && *title && vp && pdf_dget(d, vp, "DisplayDocTitle") ? A11Y_PASS : A11Y_FAIL,
           title && *title ? (vp && pdf_dget(d, vp, "DisplayDocTitle") ? "set and shown" : "set but not shown in the title bar") : "no title");
  for (p = 0; p < d->npages; p++) { TextPage *tp = get ? get(ud, p) : NULL; if (tp && tp->no == 0) notext++; }
  snprintf(t, sizeof t, notext ? "%d pages have no text (scanned? they need OCR)" : "every page has text", notext);
  add_item(out, &n, "Image-only PDF", notext ? A11Y_FAIL : A11Y_PASS, t);
  /* fonts: embedded, or one of the standard ones a reader has */
  for (p = 0; p < d->npages; p++) {
    PObj *res = pdf_resolve(d, pdf_page_attr(d, d->pages[p], "Resources")), *fonts = res ? pdf_dget(d, res, "Font") : NULL;
    if (!fonts || fonts->type != PO_DICT) continue;
    for (i = 0; i < fonts->u.dict.n; i++) {
      PObj *f = pdf_resolve(d, fonts->u.dict.v[i]), *fd;
      if (!f || f->type != PO_DICT) continue;
      if (pobj_is_name(pdf_dget(d, f, "Subtype"), "Type0")) { PObj *df = pdf_dget(d, f, "DescendantFonts"); f = df && df->type == PO_ARRAY && df->u.arr.n ? pdf_resolve(d, df->u.arr.v[0]) : f; }
      fd = pdf_dget(d, f, "FontDescriptor");
      if (pobj_is_name(pdf_dget(d, f, "Subtype"), "Type3")) continue;
      if (!fd || !(pdf_dget(d, fd, "FontFile") || pdf_dget(d, fd, "FontFile2") || pdf_dget(d, fd, "FontFile3"))) unemb++;
    }
  }
  snprintf(t, sizeof t, unemb ? "%d font uses aren't embedded (readers substitute them)" : "every font is embedded", unemb);
  add_item(out, &n, "Fonts embedded", unemb ? A11Y_MANUAL : A11Y_PASS, t);
  snprintf(t, sizeof t, "%d of %d figures have alternate text", sc.figures_alt, sc.figures);
  add_item(out, &n, "Figures alternate text", sc.figures_alt < sc.figures ? A11Y_FAIL : A11Y_PASS, sc.figures ? t : "no figures tagged");
  snprintf(t, sizeof t, "%d headings, %d tables tagged", sc.headings, sc.tables);
  add_item(out, &n, "Headings and tables", A11Y_MANUAL, t);
  for (p = 0; p < d->npages; p++) {
    PObj *a = pdf_dget(d, d->pages[p], "Annots");
    int has_widget = 0;
    if (a && a->type == PO_ARRAY) for (i = 0; i < a->u.arr.n; i++) {
      PObj *an = pdf_resolve(d, a->u.arr.v[i]);
      const char *s = an && an->type == PO_DICT ? pdf_dname(d, an, "Subtype") : NULL;
      if (!s) continue;
      if (!strcmp(s, "Link")) { links++; if (pdf_dget(d, an, "Contents") || pdf_dget(d, an, "StructParent")) links_ok++; }
      if (!strcmp(s, "Widget")) { fields++; has_widget = 1; if (pdf_dget(d, an, "TU") || (pdf_dget(d, an, "Parent") && pdf_dget(d, pdf_dget(d, an, "Parent"), "TU"))) fields_ok++; }
    }
    if ((a && a->type == PO_ARRAY && a->u.arr.n) && !pobj_is_name(pdf_dget(d, d->pages[p], "Tabs"), "S") && has_widget) tabs_ok = 0;
  }
  snprintf(t, sizeof t, "%d of %d links described", links_ok, links);
  add_item(out, &n, "Links", links_ok < links ? A11Y_FAIL : A11Y_PASS, links ? t : "no links");
  snprintf(t, sizeof t, "%d of %d form fields have a description", fields_ok, fields);
  add_item(out, &n, "Form fields described", fields_ok < fields ? A11Y_FAIL : A11Y_PASS, fields ? t : "no form fields");
  add_item(out, &n, "Tab order", tabs_ok ? A11Y_PASS : A11Y_FAIL, tabs_ok ? "consistent with the structure" : "pages with fields don't follow the structure (/Tabs /S)");
  add_item(out, &n, "Bookmarks", d->npages <= 20 || (root && pdf_dget(d, root, "Outlines")) ? A11Y_PASS : A11Y_FAIL, d->npages > 20 ? "a long document needs bookmarks" : "short enough without");
  add_item(out, &n, "Logical reading order", A11Y_MANUAL, "check by reading the tags in order");
  add_item(out, &n, "Colour contrast", A11Y_MANUAL, "check that text stands out from its background");
  free(title);
  return n;
}
char *a11y_report(const A11yItem *it, int n)
{
  Buf b;
  int i;
  static const char *const st[3] = { "Passed", "Failed", "Check by hand" };
  buf_init(&b);
  for (i = 0; i < n; i++) buf_printf(&b, "%s: %s - %s\n", st[it[i].status], it[i].rule, it[i].detail);
  return b.oom ? (buf_free(&b), (char *)NULL) : buf_take(&b);
}
int a11y_reading_options(PDoc *d, const char *lang, int display_title)
{
  PObj *root = root_of(d), *vp;
  if (!root) return 0;
  if (lang && *lang) pobj_dict_set(root, "Lang", om_text(lang)); else if (lang) pobj_dict_del(root, "Lang");
  vp = pdf_dget(d, root, "ViewerPreferences");
  if (!vp) { if (!(vp = pobj_new(PO_DICT))) return 0; pobj_dict_set(root, "ViewerPreferences", vp); }
  if (display_title) { PObj *t = pobj_new(PO_BOOL); if (t) { t->u.b = 1; pobj_dict_set(vp, "DisplayDocTitle", t); } }
  else pobj_dict_del(vp, "DisplayDocTitle");
  d->changed = 1;
  return 1;
}
int a11y_figures(PDoc *d, char ***alts)
{
  PObj *root = root_of(d), *st = root ? pdf_dget(d, root, "StructTreeRoot") : NULL;
  StructCount sc;
  int fig = 0, n = 0;
  *alts = NULL;
  memset(&sc, 0, sizeof sc);
  if (st) walk_struct(d, st, &sc, 0, NULL, &fig, -1, NULL, alts, &n);
  return n;
}
int a11y_set_alt(PDoc *d, int i, const char *alt)
{
  PObj *root = root_of(d), *st = root ? pdf_dget(d, root, "StructTreeRoot") : NULL;
  StructCount sc;
  int fig = 0;
  memset(&sc, 0, sizeof sc);
  if (!st) return 0;
  walk_struct(d, st, &sc, 0, NULL, &fig, i, alt, NULL, NULL);
  return i < fig;
}
int a11y_autotag(PDoc *d, int fields)
{
  PObj *root = root_of(d), *str, *doc, *dk, *nums, *pt, *mi;
  int sn, dn, p, n = 0, key = 0;
  if (!root || pdf_dget(d, root, "StructTreeRoot")) return 0;
  if (!(str = pobj_new(PO_DICT)) || !(sn = pdf_new_object(d, str))) return 0;
  if (!(doc = pobj_new(PO_DICT)) || !(dn = pdf_new_object(d, doc))) return 0;
  dk = pobj_new(PO_ARRAY); nums = pobj_new(PO_ARRAY); pt = pobj_new(PO_DICT);
  if (!dk || !nums || !pt) { pobj_free(dk); pobj_free(nums); pobj_free(pt); return 0; }
  pobj_dict_set(str, "Type", pobj_name("StructTreeRoot"));
  pobj_dict_set(str, "K", om_ref(dn));
  pobj_dict_set(doc, "Type", pobj_name("StructElem"));
  pobj_dict_set(doc, "S", pobj_name("Document"));
  pobj_dict_set(doc, "P", om_ref(sn));
  for (p = 0; p < d->npages; p++) {
    PObj *pg = d->pages[p], *e, *arr;
    int pn = om_num_of(d, pg), en;
    size_t len;
    u8 *c = pc_content(d, pg, &len);
    if (c && len && !content_tagged(d, p)) {
      Buf b;
      buf_init(&b);
      buf_adds(&b, "/P <</MCID 0>> BDC\n");
      buf_add(&b, c, len);
      buf_adds(&b, "\nEMC\n");
      if (!b.oom && (e = pobj_new(PO_DICT)) && (en = pdf_new_object(d, e))) {
        pc_set_content(d, pg, (const u8 *)b.data, b.len);
        pobj_dict_set(e, "Type", pobj_name("StructElem"));
        pobj_dict_set(e, "S", pobj_name("P"));
        pobj_dict_set(e, "P", om_ref(dn));
        if (pn) pobj_dict_set(e, "Pg", om_ref(pn));
        pobj_dict_set(e, "K", pobj_int(0));
        pobj_array_add(dk, om_ref(en));
        pobj_dict_set(pg, "StructParents", pobj_int(key));
        arr = pobj_new(PO_ARRAY);
        pobj_array_add(arr, om_ref(en));
        pobj_array_add(nums, pobj_int(key)); pobj_array_add(nums, arr);
        key++; n++;
      }
      buf_free(&b);
    }
    free(c);
    if (fields) {
      PObj *a = pdf_dget(d, pg, "Annots");
      int i;
      if (a && a->type == PO_ARRAY) for (i = 0; i < a->u.arr.n; i++) {
        PObj *an = pdf_resolve(d, a->u.arr.v[i]), *fe, *objr;
        int fen;
        if (!an || an->type != PO_DICT || !pobj_is_name(pdf_dget(d, an, "Subtype"), "Widget") || a->u.arr.v[i]->type != PO_REF || pdf_dget(d, an, "StructParent")) continue;
        if (!(fe = pobj_new(PO_DICT)) || !(fen = pdf_new_object(d, fe)) || !(objr = pobj_new(PO_DICT))) continue;
        pobj_dict_set(fe, "Type", pobj_name("StructElem"));
        pobj_dict_set(fe, "S", pobj_name("Form"));
        pobj_dict_set(fe, "P", om_ref(dn));
        if (pn) pobj_dict_set(fe, "Pg", om_ref(pn));
        pobj_dict_set(objr, "Type", pobj_name("OBJR"));
        pobj_dict_set(objr, "Obj", pobj_copy(a->u.arr.v[i]));
        pobj_dict_set(fe, "K", objr);
        pobj_array_add(dk, om_ref(fen));
        pobj_dict_set(an, "StructParent", pobj_int(key));
        pobj_array_add(nums, pobj_int(key)); pobj_array_add(nums, om_ref(fen));
        key++;
      }
      pobj_dict_set(pg, "Tabs", pobj_name("S"));
    }
  }
  pobj_dict_set(doc, "K", dk);
  pobj_dict_set(pt, "Nums", nums);
  pobj_dict_set(str, "ParentTree", pt);
  pobj_dict_set(str, "ParentTreeNextKey", pobj_int(key));
  pobj_dict_set(root, "StructTreeRoot", om_ref(sn));
  if ((mi = pobj_new(PO_DICT))) { PObj *t = pobj_new(PO_BOOL); if (t) t->u.b = 1; pobj_dict_set(mi, "Marked", t); pobj_dict_set(root, "MarkInfo", mi); }
  d->changed = 1;
  return n;
}
int a11y_untagged(PDoc *d, int **pages)
{
  int p, n = 0;
  *pages = (int *)malloc((size_t)(d->npages + 1) * sizeof(int));
  if (!*pages) return 0;
  for (p = 0; p < d->npages; p++) if (!content_tagged(d, p)) (*pages)[n++] = p;
  return n;
}
char *a11y_tags(PDoc *d)
{
  PObj *root = root_of(d), *st = root ? pdf_dget(d, root, "StructTreeRoot") : NULL;
  StructCount sc;
  Buf b;
  int fig = 0;
  buf_init(&b);
  memset(&sc, 0, sizeof sc);
  if (st) walk_struct(d, st, &sc, 0, &b, &fig, -1, NULL, NULL, NULL);
  else buf_adds(&b, "(no tags)\n");
  return b.oom ? (buf_free(&b), (char *)NULL) : buf_take(&b);
}

/* ------------------------------------------------------------------ signatures */
static void sig_walk(PDoc *d, PObj *f, SigInfo **out, int *n, int depth, const char *parent);
static const u8 *const *trust_c; static const size_t *trust_l; static int trust_n;
void sig_trust(const u8 *const *certs, const size_t *lens, int n) { trust_c = certs; trust_l = lens; trust_n = n; }
void sig_free(SigInfo *si, int n) { int i; for (i = 0; i < n; i++) free(si[i].cert); free(si); }
/* the signer's certificate from a CMS signature: the one its SignerInfo names (a copy) */
static u8 *signer_cert(const u8 *sig, size_t n, size_t *len)
{
  Der in, ci, w, ex, sd, set, c, wh, si, sid, iss, ser;
  int t, t2;
  Cert cert;
  u8 *r;
  in.p = sig; in.n = n;
  if (!der_expect(&in, 0x30, &ci) || !der_next(&ci, &t, &c, &w) || !der_expect(&ci, 0xA0, &ex) || !der_expect(&ex, 0x30, &sd)) return NULL;
  der_next(&sd, &t, &c, &w); der_next(&sd, &t, &c, &w); der_next(&sd, &t, &c, &w);
  if (!der_expect(&sd, 0xA0, &set)) return NULL;
  while (der_next(&sd, &t, &c, &w)) if (t == 0x31) break;
  if (t != 0x31 || !der_expect(&c, 0x30, &si) || !der_next(&si, &t, &c, &w) || !der_next(&si, &t, &sid, &w)) return NULL;
  while (set.n && der_next(&set, &t2, &c, &wh)) {
    if (!cert_parse(wh.p, wh.n, &cert)) continue;
    if (t == 0x30) {
      Der s2 = sid, wi;
      if (!der_next(&s2, &t2, &iss, &wi) || !der_next(&s2, &t2, &ser, &w)) continue;
      if (wi.n != cert.issuer.n || memcmp(wi.p, cert.issuer.p, wi.n) || ser.n != cert.serial.n || memcmp(ser.p, cert.serial.p, ser.n)) continue;
    } else if (t != 0x80 || sid.n != cert.ski.n || memcmp(sid.p, cert.ski.p, sid.n)) continue;
    if (!(r = (u8 *)malloc(wh.n))) return NULL;
    memcpy(r, wh.p, wh.n);
    *len = wh.n;
    return r;
  }
  return NULL;
}
static void sig_one(PDoc *d, PObj *field, PObj *v, const char *name, SigInfo **out, int *n)
{
  SigInfo *si, *ns;
  PObj *br = pdf_dget(d, v, "ByteRange"), *ct = pdf_dget(d, v, "Contents"), *s;
  char *t;
  if (!(ns = (SigInfo *)realloc(*out, (size_t)(*n + 1) * sizeof(SigInfo)))) return;
  *out = ns;
  si = &ns[(*n)++];
  memset(si, 0, sizeof *si);
  snprintf(si->field, sizeof si->field, "%.95s", name);
  if ((s = pdf_dget(d, v, "Name")) && (t = pdf_text_string(s))) { snprintf(si->name, sizeof si->name, "%s", t); free(t); }
  if ((s = pdf_dget(d, v, "Reason")) && (t = pdf_text_string(s))) { snprintf(si->reason, sizeof si->reason, "%s", t); free(t); }
  if ((s = pdf_dget(d, v, "M")) && (t = pdf_text_string(s))) { snprintf(si->date, sizeof si->date, "%s", t); free(t); }
  si->digest_ok = -1;
  {
    PObj *root = root_of(d), *perms = root ? pdf_dget(d, root, "Perms") : NULL;
    if (perms && pdf_dget(d, perms, "DocMDP") == v) si->certify = 1;
  }
  if (br && br->type == PO_ARRAY && br->u.arr.n >= 4 && br->u.arr.n % 2 == 0 && ct && ct->type == PO_STRING) {
    Buf data;
    int i, ok = 1;
    long end = 0;
    buf_init(&data);
    for (i = 0; i + 1 < br->u.arr.n; i += 2) {
      long o = dbl_long(pobj_num(pdf_resolve(d, br->u.arr.v[i]))), l = dbl_long(pobj_num(pdf_resolve(d, br->u.arr.v[i + 1])));
      if (o < 0 || l < 0 || (size_t)o > d->len || (size_t)l > d->len - (size_t)o) { ok = 0; break; }
      buf_add(&data, d->data + o, (size_t)l);
      if (o + l > end) end = o + l;
    }
    si->covers_all = ok && (size_t)end >= d->len - 2;   /* (a final end of line may follow %%EOF) */
    si->sig_ok = si->time_ok = -1;
    if (ok && !data.oom) {
      CmsResult r;
      char when[16] = "";
      int k2 = 0;
      const char *m = si->date;
      if (m[0] == 'D' && m[1] == ':') m += 2;
      while (k2 < 14 && m[k2] >= '0' && m[k2] <= '9') { when[k2] = m[k2]; k2++; }
      when[k2 < 14 ? 0 : 14] = 0;
      if (cms_verify((const u8 *)ct->u.str.s, (size_t)ct->u.str.len, (const u8 *)data.data, data.len, trust_c, trust_l, trust_n, when[0] ? when : NULL, &r)) {
        si->sig_ok = r.ok; si->chain = r.chain; si->time_ok = r.time_ok;
        snprintf(si->signer, sizeof si->signer, "%s", r.signer); snprintf(si->issuer, sizeof si->issuer, "%s", r.issuer);
        snprintf(si->valid_to, sizeof si->valid_to, "%s", r.not_after); snprintf(si->why, sizeof si->why, "%s", r.why);
        if (!si->name[0]) snprintf(si->name, sizeof si->name, "%s", r.signer);
        if (r.digest_ok >= 0) si->digest_ok = r.digest_ok;
        si->cert = signer_cert((const u8 *)ct->u.str.s, (size_t)ct->u.str.len, &si->certlen);
      }
    }
    if (ok && !data.oom && si->digest_ok < 0) {
      /* the messageDigest attribute (1.2.840.113549.1.9.4) in the CMS signed attributes */
      static const u8 oid[11] = { 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x04 };
      const u8 *c = (const u8 *)ct->u.str.s;
      int cl = ct->u.str.len, k;
      for (k = 0; k + 11 + 4 < cl; k++) if (!memcmp(c + k, oid, 11)) {
        const u8 *p = c + k + 11;
        int dl;
        u8 h[64];
        if (p[0] != 0x31 || p[2] != 0x04) break;
        dl = p[3];
        if (dl < 20 || dl > 64 || k + 11 + 4 + dl > cl) break;
        p += 4;
        si->digest_ok = 0;
        if (dl == 32) { sha256((const u8 *)data.data, data.len, h); si->digest_ok = !memcmp(h, p, 32); }
        else if (dl == 20) { sha1((const u8 *)data.data, data.len, h); si->digest_ok = !memcmp(h, p, 20); }
        else if (dl == 48) { sha384((const u8 *)data.data, data.len, h); si->digest_ok = !memcmp(h, p, 48); }
        else if (dl == 64) { sha512((const u8 *)data.data, data.len, h); si->digest_ok = !memcmp(h, p, 64); }
        else si->digest_ok = -1;
        break;
      }
    }
    buf_free(&data);
  }
  (void)field;
}
static void sig_walk(PDoc *d, PObj *f, SigInfo **out, int *n, int depth, const char *parent)
{
  PObj *k, *t, *v;
  char name[200], *pt;
  int i;
  f = pdf_resolve(d, f);
  if (!f || f->type != PO_DICT || depth > 16) return;
  t = pdf_dget(d, f, "T");
  pt = t ? pdf_text_string(t) : NULL;
  snprintf(name, sizeof name, "%s%s%s", parent, *parent && pt ? "." : "", pt ? pt : "");
  free(pt);
  v = pdf_dget(d, f, "V");
  if (pobj_is_name(pdf_dget(d, f, "FT"), "Sig") && v && v->type == PO_DICT) sig_one(d, f, v, name, out, n);
  k = pdf_dget(d, f, "Kids");
  if (k && k->type == PO_ARRAY) for (i = 0; i < k->u.arr.n; i++) sig_walk(d, k->u.arr.v[i], out, n, depth + 1, name);
}
int sig_list(PDoc *d, SigInfo **out)
{
  PObj *root = root_of(d), *af = root ? pdf_dget(d, root, "AcroForm") : NULL, *fields = af ? pdf_dget(d, af, "Fields") : NULL;
  int n = 0, i;
  *out = NULL;
  if (fields && fields->type == PO_ARRAY) for (i = 0; i < fields->u.arr.n; i++) sig_walk(d, fields->u.arr.v[i], out, &n, 0, "");
  return n;
}

/* ------------------------------------------------------------------ PDF/A */
static void be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 s15(double v) { return (u32)(i32)floor(v * 65536 + 0.5); }
/* a small sRGB ICC profile (version 2: D50 primaries, gamma 2.2 curves) */
static u8 *srgb_icc(size_t *len)
{
  static const char *const sig[9] = { "desc", "wtpt", "rXYZ", "gXYZ", "bXYZ", "rTRC", "gTRC", "bTRC", "cprt" };
  static const double xyz[4][3] = { { 0.9642, 1.0, 0.8249 }, { 0.4361, 0.2225, 0.0139 }, { 0.3851, 0.7169, 0.0971 }, { 0.1431, 0.0606, 0.7141 } };
  const char *desc = "sRGB IEC61966-2.1 (Folio)", *cprt = "No copyright, use freely";
  u8 *p;
  u32 off, sizes[9], offs[9], total;
  int i, k;
  sizes[0] = (u32)(12 + strlen(desc) + 1 + 4 + 4 + 2 + 1 + 67);
  for (i = 1; i < 5; i++) sizes[i] = 20;
  for (i = 5; i < 8; i++) sizes[i] = 14;
  sizes[8] = (u32)(8 + strlen(cprt) + 1);
  off = 128 + 4 + 9 * 12;
  for (i = 0; i < 9; i++) { offs[i] = off; off += (sizes[i] + 3) & ~3u; }
  total = off;
  if (!(p = (u8 *)calloc(1, total))) return NULL;
  be32(p, total);
  memcpy(p + 4, "none", 4);
  be32(p + 8, 0x02100000u);
  memcpy(p + 12, "mntr", 4); memcpy(p + 16, "RGB ", 4); memcpy(p + 20, "XYZ ", 4);
  p[24] = 0x07; p[25] = 0xD0; p[27] = 1; p[29] = 1;      /* (2000-01-01) */
  memcpy(p + 36, "acsp", 4);
  be32(p + 68, s15(0.9642)); be32(p + 72, s15(1.0)); be32(p + 76, s15(0.8249));
  be32(p + 128, 9);
  for (i = 0; i < 9; i++) { memcpy(p + 132 + i * 12, sig[i], 4); be32(p + 136 + i * 12, offs[i]); be32(p + 140 + i * 12, sizes[i]); }
  /* desc: textDescriptionType */
  memcpy(p + offs[0], "desc", 4); be32(p + offs[0] + 8, (u32)strlen(desc) + 1); memcpy(p + offs[0] + 12, desc, strlen(desc));
  for (i = 1; i < 5; i++) { memcpy(p + offs[i], "XYZ ", 4); for (k = 0; k < 3; k++) be32(p + offs[i] + 8 + k * 4, s15(xyz[i - 1][k])); }
  for (i = 5; i < 8; i++) { memcpy(p + offs[i], "curv", 4); be32(p + offs[i] + 8, 1); p[offs[i] + 12] = 2; p[offs[i] + 13] = 0x33; }
  memcpy(p + offs[8], "text", 4); memcpy(p + offs[8] + 8, cprt, strlen(cprt));
  *len = total;
  return p;
}
static void problem(char *out, int cap, const char *s)
{
  size_t l = strlen(out);
  if ((int)(l + strlen(s) + 2) < cap) { strcat(out, s); strcat(out, "\n"); }
}
int pdfa_convert(PDoc *d, char *problems, int cap)
{
  PObj *root = root_of(d), *md, *oi, *arr, *icc;
  size_t il, ml;
  u8 *ip, *x;
  int p, i, unemb = 0, noap = 0, inum;
  problems[0] = 0;
  if (!root) { problem(problems, cap, "no catalog"); return 0; }
  hidden_remove(d, HID_SCRIPTS | HID_ATTACH);
  /* XMP with the PDF/A identification */
  xmp_update(d);
  md = pdf_dget(d, root, "Metadata");
  if (md && md->type == PO_STREAM && md->sdata && (x = pdf_stream_data(d, md, &ml))) {
    const char *end = "</rdf:RDF>";
    char *pos = NULL;
    size_t k;
    for (k = 0; k + 10 <= ml; k++) if (!memcmp(x + k, end, 10)) pos = (char *)x + k;
    if (pos) {
      Buf b;
      buf_init(&b);
      buf_add(&b, x, (size_t)(pos - (char *)x));
      buf_adds(&b, "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"><pdfaid:part>2</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance></rdf:Description>\n");
      buf_add(&b, pos, ml - (size_t)(pos - (char *)x));
      if (!b.oom) { free(md->sdata); md->slen = (long)b.len; md->sdata = (u8 *)buf_take(&b); }
      buf_free(&b);
    }
    free(x);
  }
  /* the output intent: sRGB */
  if (!pdf_dget(d, root, "OutputIntents") && (ip = srgb_icc(&il))) {
    if ((icc = pobj_new(PO_DICT))) {
      pobj_dict_set(icc, "N", pobj_int(3));
      inum = om_stream(d, icc, ip, il);
      oi = pobj_new(PO_DICT); arr = pobj_new(PO_ARRAY);
      if (inum && oi && arr) {
        pobj_dict_set(oi, "Type", pobj_name("OutputIntent"));
        pobj_dict_set(oi, "S", pobj_name("GTS_PDFA1"));
        pobj_dict_set(oi, "OutputConditionIdentifier", pobj_string("sRGB IEC61966-2.1", 17));
        pobj_dict_set(oi, "Info", pobj_string("sRGB IEC61966-2.1", 17));
        pobj_dict_set(oi, "DestOutputProfile", om_ref(inum));
        pobj_array_add(arr, oi);
        pobj_dict_set(root, "OutputIntents", arr);
      } else { pobj_free(oi); pobj_free(arr); }
    }
    free(ip);
  }
  /* annotations: printed, and with appearances */
  for (p = 0; p < d->npages; p++) {
    PObj *a = pdf_dget(d, d->pages[p], "Annots"), *res, *fonts;
    if (a && a->type == PO_ARRAY) for (i = 0; i < a->u.arr.n; i++) {
      PObj *an = pdf_resolve(d, a->u.arr.v[i]);
      const char *st;
      if (!an || an->type != PO_DICT) continue;
      st = pdf_dname(d, an, "Subtype");
      if (st && !strcmp(st, "Popup")) continue;
      pobj_dict_set(an, "F", pobj_int((pdf_dint(d, an, "F", 0) | 4) & ~(1 | 2 | 32)));
      if (!pdf_dget(d, an, "AP") && !(st && !strcmp(st, "Link"))) noap++;
    }
    res = pdf_resolve(d, pdf_page_attr(d, d->pages[p], "Resources"));
    fonts = res ? pdf_dget(d, res, "Font") : NULL;
    if (fonts && fonts->type == PO_DICT) for (i = 0; i < fonts->u.dict.n; i++) {
      PObj *f = pdf_resolve(d, fonts->u.dict.v[i]), *fd;
      if (!f || f->type != PO_DICT || pobj_is_name(pdf_dget(d, f, "Subtype"), "Type3")) continue;
      if (pobj_is_name(pdf_dget(d, f, "Subtype"), "Type0")) { PObj *df = pdf_dget(d, f, "DescendantFonts"); f = df && df->type == PO_ARRAY && df->u.arr.n ? pdf_resolve(d, df->u.arr.v[0]) : f; }
      fd = pdf_dget(d, f, "FontDescriptor");
      if (!fd || !(pdf_dget(d, fd, "FontFile") || pdf_dget(d, fd, "FontFile2") || pdf_dget(d, fd, "FontFile3"))) unemb++;
    }
  }
  if (unemb) { char t[100]; snprintf(t, sizeof t, "%d font uses aren't embedded (PDF/A needs every font embedded)", unemb); problem(problems, cap, t); }
  if (noap) { char t[100]; snprintf(t, sizeof t, "%d annotations have no appearance", noap); problem(problems, cap, t); }
  if (pdf_dget(d, root, "OCProperties")) {
    PObj *ocp = pdf_dget(d, root, "OCProperties"), *dd = ocp ? pdf_dget(d, ocp, "D") : NULL;
    if (dd && pdf_dget(d, dd, "AS")) pobj_dict_del(dd, "AS");
  }
  d->changed = 1;
  return problems[0] == 0;
}
