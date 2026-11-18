/*
 * pagetools.c - page and document tools (see pagetools.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "pagetools.h"
#include "pagecontent.h"
#include "pdfedit.h"
#include "image.h"
#include "filters.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int get_rect(PDoc *d, PObj *a, double r[4])
{
  int i;
  a = pdf_resolve(d, a);
  if (!a || a->type != PO_ARRAY || a->u.arr.n < 4) return 0;
  for (i = 0; i < 4; i++) r[i] = pobj_num(pdf_resolve(d, a->u.arr.v[i]));
  if (r[0] > r[2]) { double t = r[0]; r[0] = r[2]; r[2] = t; }
  if (r[1] > r[3]) { double t = r[1]; r[1] = r[3]; r[3] = t; }
  return r[2] - r[0] > 0.01 && r[3] - r[1] > 0.01;
}
static char *latin(const char *utf8, int *n)
{
  size_t l = utf8 ? strlen(utf8) : 0;
  char *s = (char *)malloc(l * 3 + 4);
  if (!s) { *n = 0; return NULL; }
  *n = (int)utf8_to_latin1(utf8 ? utf8 : "", l, s, l * 3 + 4);
  return s;
}
static const char *const fonts4[4] = { "Helvetica", "Helvetica-Bold", "Times-Roman", "Courier" };

/* ------------------------------------------------------------------ boxes */
static const char *const box_keys[5] = { "MediaBox", "CropBox", "BleedBox", "TrimBox", "ArtBox" };
int page_get_box(PDoc *d, int page, int which, double r[4])
{
  PObj *pg = pc_page(d, page);
  int own;
  if (!pg || which < 0 || which > 4) return 0;
  own = get_rect(d, which <= BOX_CROP ? pdf_page_attr(d, pg, box_keys[which]) : pdf_dget(d, pg, box_keys[which]), r);
  if (!own) {
    if (which == BOX_MEDIA) { r[0] = r[1] = 0; r[2] = 612; r[3] = 792; }
    else if (which == BOX_CROP) page_get_box(d, page, BOX_MEDIA, r);
    else page_get_box(d, page, BOX_CROP, r);
  }
  return own;
}
int page_set_box(PDoc *d, int page, int which, const double *r)
{
  PObj *pg = pc_page(d, page);
  double v[4];
  if (!pg || which < 0 || which > 4) return 0;
  if (!r) { if (which == BOX_MEDIA) return 0; pobj_dict_del(pg, box_keys[which]); d->changed = 1; return 1; }
  v[0] = r[0] < r[2] ? r[0] : r[2]; v[2] = r[0] < r[2] ? r[2] : r[0];
  v[1] = r[1] < r[3] ? r[1] : r[3]; v[3] = r[1] < r[3] ? r[3] : r[1];
  if (v[2] - v[0] < 1 || v[3] - v[1] < 1) return 0;
  pobj_dict_set(pg, box_keys[which], om_arr(v, 4));
  d->changed = 1;
  return 1;
}
int page_crop(PDoc *d, int first, int last, double l, double b, double r, double t)
{
  int p, n = 0;
  if (last < 0 || last >= d->npages) last = d->npages - 1;
  for (p = first < 0 ? 0 : first; p <= last; p++) {
    double box[4];
    int rot;
    pdf_page_box(d, p, box, &rot);
    {
      /* the margins are as the page is seen: turned with its rotation */
      double m[4], c[4];
      m[0] = l; m[1] = b; m[2] = r; m[3] = t;
      if (rot == 90) { double tl = m[0]; m[0] = m[1]; m[1] = m[2]; m[2] = m[3]; m[3] = tl; }
      else if (rot == 180) { double t0 = m[0], t1 = m[1]; m[0] = m[2]; m[1] = m[3]; m[2] = t0; m[3] = t1; }
      else if (rot == 270) { double tl = m[3]; m[3] = m[2]; m[2] = m[1]; m[1] = m[0]; m[0] = tl; }
      c[0] = box[0] + m[0]; c[1] = box[1] + m[1]; c[2] = box[2] - m[2]; c[3] = box[3] - m[3];
      if (c[2] - c[0] >= 1 && c[3] - c[1] >= 1 && page_set_box(d, p, BOX_CROP, c)) n++;
    }
  }
  return n;
}
void page_display_matrix(PDoc *d, int page, double m[6], double *w, double *h)
{
  double b[4];
  int rot;
  pdf_page_box(d, page, b, &rot);
  *w = b[2] - b[0]; *h = b[3] - b[1];
  switch (rot) {
  case 90: m[0] = 0; m[1] = 1; m[2] = -1; m[3] = 0; m[4] = b[2]; m[5] = b[1]; *w = b[3] - b[1]; *h = b[2] - b[0]; break;
  case 180: m[0] = -1; m[1] = 0; m[2] = 0; m[3] = -1; m[4] = b[2]; m[5] = b[3]; break;
  case 270: m[0] = 0; m[1] = -1; m[2] = 1; m[3] = 0; m[4] = b[0]; m[5] = b[3]; *w = b[3] - b[1]; *h = b[2] - b[0]; break;
  default: m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 1; m[4] = b[0]; m[5] = b[1]; break;
  }
}
static void put_cm(Buf *b, const double m[6])
{
  int i;
  for (i = 0; i < 6; i++) om_num(b, m[i]);
  buf_adds(b, "cm\n");
}

/* ------------------------------------------------------------------ page labels */
static void labels_walk(PDoc *d, PObj *node, PLabel **out, int *n, int *cap, int depth)
{
  PObj *nums, *kids;
  int i;
  if (!node || node->type != PO_DICT || depth > 16) return;
  if ((nums = pdf_dget(d, node, "Nums")) && nums->type == PO_ARRAY) {
    for (i = 0; i + 1 < nums->u.arr.n; i += 2) {
      PObj *k = pdf_resolve(d, nums->u.arr.v[i]), *v = pdf_resolve(d, nums->u.arr.v[i + 1]);
      PLabel *l;
      const char *s;
      PObj *p;
      if (!pobj_isnum(k) || !v || v->type != PO_DICT) continue;
      if (*n >= *cap) { int nc = *cap ? *cap * 2 : 8; PLabel *t = (PLabel *)realloc(*out, (size_t)nc * sizeof(PLabel)); if (!t) return; *out = t; *cap = nc; }
      l = &(*out)[(*n)++];
      memset(l, 0, sizeof *l);
      l->start = dbl_int(pobj_num(k));
      s = pdf_dname(d, v, "S");
      l->style = s ? s[0] : 0;
      l->first = (int)pdf_dint(d, v, "St", 1);
      if ((p = pdf_dget(d, v, "P")) && p->type == PO_STRING) { char *u = pdf_text_string(p); if (u) { strncpy(l->prefix, u, sizeof l->prefix - 1); free(u); } }
    }
  }
  if ((kids = pdf_dget(d, node, "Kids")) && kids->type == PO_ARRAY)
    for (i = 0; i < kids->u.arr.n; i++) labels_walk(d, pdf_resolve(d, kids->u.arr.v[i]), out, n, cap, depth + 1);
}
int page_labels_get(PDoc *d, PLabel **out)
{
  PObj *root = pdf_dget(d, d->trailer, "Root");
  int n = 0, cap = 0;
  *out = NULL;
  if (!root) return 0;
  labels_walk(d, pdf_dget(d, root, "PageLabels"), out, &n, &cap, 0);
  return n;
}
int page_labels_set(PDoc *d, const PLabel *l, int n)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *tree, *nums;
  int i;
  if (!root) return 0;
  if (!n) { pobj_dict_del(root, "PageLabels"); d->changed = 1; return 1; }
  if (!(tree = pobj_new(PO_DICT)) || !(nums = pobj_new(PO_ARRAY))) { pobj_free(tree); return 0; }
  for (i = 0; i < n; i++) {
    PObj *v = pobj_new(PO_DICT);
    char st[2];
    if (!v) continue;
    st[0] = l[i].style; st[1] = 0;
    if (l[i].style) pobj_dict_set(v, "S", pobj_name(st));
    if (l[i].prefix[0]) pobj_dict_set(v, "P", om_text(l[i].prefix));
    if (l[i].first > 1) pobj_dict_set(v, "St", pobj_int(l[i].first));
    pobj_array_add(nums, pobj_int(l[i].start));
    pobj_array_add(nums, v);
  }
  pobj_dict_set(tree, "Nums", nums);
  pobj_dict_set(root, "PageLabels", tree);
  d->changed = 1;
  return 1;
}
static void roman(int v, int lower, char *out)
{
  static const int val[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
  static const char *const sym[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
  int i;
  out[0] = 0;
  if (v <= 0 || v > 4999) { sprintf(out, "%d", v); return; }
  for (i = 0; i < 13; i++) while (v >= val[i]) { strcat(out, sym[i]); v -= val[i]; }
  if (lower) for (i = 0; out[i]; i++) out[i] = (char)(out[i] - 'A' + 'a');
}
void page_label(PDoc *d, int page, char *out, int cap)
{
  PLabel *l;
  int n = page_labels_get(d, &l), i, best = -1;
  char num[40] = "";
  for (i = 0; i < n; i++) if (l[i].start <= page && (best < 0 || l[i].start >= l[best].start)) best = i;
  if (best < 0) { snprintf(out, (size_t)cap, "%d", page + 1); free(l); return; }
  {
    int v = l[best].first + page - l[best].start;
    switch (l[best].style) {
    case 'D': sprintf(num, "%d", v); break;
    case 'R': roman(v, 0, num); break;
    case 'r': roman(v, 1, num); break;
    case 'A': case 'a': {
      int k = (v - 1) / 26 + 1, c = (v - 1) % 26, j;
      if (v < 1) { sprintf(num, "%d", v); break; }
      if (k > 30) k = 30;
      for (j = 0; j < k; j++) num[j] = (char)((l[best].style == 'A' ? 'A' : 'a') + c);
      num[k] = 0;
      break; }
    }
    snprintf(out, (size_t)cap, "%s%s", l[best].prefix, num);
  }
  free(l);
}

/* ------------------------------------------------------------------ pages */
int page_duplicate(PDoc *d, int page, int copies)
{
  int i;
  if (page < 0 || page >= d->npages || copies < 1 || copies > 999) return 0;
  for (i = 0; i < copies; i++) if (!pdf_insert_pages(d, page + 1, d, page, page)) return 0;
  return 1;
}
int page_reverse(PDoc *d, int first, int last)
{
  if (last < 0 || last >= d->npages) last = d->npages - 1;
  if (first < 0) first = 0;
  while (first < last) {
    PObj *t = d->pages[first];
    d->pages[first] = d->pages[last]; d->pages[last] = t;
    first++; last--;
  }
  d->changed = 1;
  return 1;
}
int page_replace(PDoc *d, int at, int n, PDoc *src, int first, int last)
{
  int i;
  if (at < 0 || at >= d->npages || n < 1 || at + n > d->npages) return 0;
  if (!pdf_insert_pages(d, at + n, src, first, last)) return 0;
  for (i = 0; i < n; i++) d->pages[at + i] = NULL;
  /* the replaced pages taken out of the list */
  {
    int k = 0;
    for (i = 0; i < d->npages; i++) if (d->pages[i]) d->pages[k++] = d->pages[i];
    d->npages = k;
  }
  d->changed = 1;
  return 1;
}
int split_points(PDoc *d, int mode, int value, int **starts)
{
  int *s = (int *)malloc((size_t)(d->npages + 1) * sizeof(int)), n = 0, p;
  *starts = s;
  if (!s) return 0;
  s[n++] = 0;
  if (mode == 0) { if (value < 1) value = 1; for (p = value; p < d->npages; p += value) s[n++] = p; }
  else if (mode == 2) {
    if (value < 1) value = 1;
    if (value > d->npages) value = d->npages;
    for (p = 1; p < value; p++) { int at = (int)((long)d->npages * p / value); if (at > s[n - 1]) s[n++] = at; }
  } else {
    POutline *o = pdf_outline(d), *it;
    for (it = o; it; it = it->next) if (it->page > s[n - 1] && it->page < d->npages) s[n++] = it->page;
    pdf_outline_free(o);
  }
  return n;
}

/* ------------------------------------------------------------------ headers, footers, Bates */
static void expand(const char *t, int page, int pages, int bates, const HFOpts *o, PDoc *d, char *out, size_t cap)
{
  size_t k = 0;
  out[0] = 0;
  while (*t && k + 1 < cap) {
    char rep[96];
    int len = 0;
    if (!strncmp(t, "<<page>>", 8)) { sprintf(rep, "%d", page + 1); len = 8; }
    else if (!strncmp(t, "<<pages>>", 9)) { sprintf(rep, "%d", pages); len = 9; }
    else if (!strncmp(t, "<<label>>", 9)) { page_label(d, page, rep, sizeof rep); len = 9; }
    else if (!strncmp(t, "<<date>>", 8)) {
      time_t now = time(NULL);
      struct tm *tm = localtime(&now);
      if (tm) sprintf(rep, "%02d/%02d/%04d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900); else strcpy(rep, "");
      len = 8;
    } else if (!strncmp(t, "<<bates>>", 9)) {
      snprintf(rep, sizeof rep, "%s%0*d%s", o->bates_prefix, clampi(o->bates_digits, 1, 12), bates, o->bates_suffix);
      len = 9;
    }
    if (len) {
      size_t rl = strlen(rep);
      if (k + rl >= cap) break;
      memcpy(out + k, rep, rl); k += rl; t += len;
    } else out[k++] = *t++;
  }
  out[k] = 0;
}
int hf_apply(PDoc *d, const HFOpts *o, const char *mark)
{
  int p, last = o->last < 0 || o->last >= d->npages ? d->npages - 1 : o->last, n = 0, i;
  double size = o->size > 0 ? o->size : 10;
  double top = o->top > 0 ? o->top : 36, bottom = o->bottom > 0 ? o->bottom : 36, side = o->side > 0 ? o->side : 36;
  for (p = o->first < 0 ? 0 : o->first; p <= last; p++) {
    PObj *pg = pc_page(d, p), *res;
    double m[6], w, h;
    char fname[32];
    Buf b;
    if (!pg || !(res = pc_resources(d, pg))) continue;
    if (!pc_add_resource(d, res, "Font", "FoHF", om_std_font(fonts4[clampi(o->font, 0, 3)]), fname, sizeof fname)) continue;
    page_display_matrix(d, p, m, &w, &h);
    buf_init(&b);
    buf_adds(&b, "/Artifact <</Type /Pagination>> BDC\n");
    put_cm(&b, m);
    om_num(&b, ((o->color >> 16) & 255) / 255.0); om_num(&b, ((o->color >> 8) & 255) / 255.0); om_num(&b, (o->color & 255) / 255.0); buf_adds(&b, "rg\n");
    for (i = 0; i < 6; i++) {
      char t[600];
      char *s;
      int len;
      double tw, x, y;
      if (!o->text[i][0]) continue;
      expand(o->text[i], p, d->npages, o->bates_start + p - (o->first < 0 ? 0 : o->first), o, d, t, sizeof t);
      if (!(s = latin(t, &len))) continue;
      tw = om_text_width(s, len, size, o->font);
      x = i % 3 == 0 ? side : i % 3 == 1 ? (w - tw) / 2 : w - side - tw;
      y = i < 3 ? h - top - size * 0.75 : bottom;
      buf_printf(&b, "BT /%s ", fname); om_num(&b, size); buf_adds(&b, "Tf "); om_num(&b, x); om_num(&b, y); buf_adds(&b, "Td ");
      om_str(&b, s, len); buf_adds(&b, " Tj ET\n");
      free(s);
    }
    buf_adds(&b, "EMC\n");
    if (!b.oom && pc_add_content(d, p, b.data, b.len, 0, mark)) n++;
    buf_free(&b);
  }
  return n;
}
int marks_remove(PDoc *d, const char *mark)
{
  int p, n = 0;
  for (p = 0; p < d->npages; p++) if (pc_remove_marked(d, p, mark)) n++;
  return n;
}
int marks_present(PDoc *d, const char *mark)
{
  int p, n = 0;
  for (p = 0; p < d->npages; p++) n += pc_has_marked(d, p, mark) ? 1 : 0;
  return n;
}
static int gs_alpha(PDoc *d, PObj *res, double a, char *name, int cap)
{
  PObj *g = pobj_new(PO_DICT);
  if (!g) return 0;
  pobj_dict_set(g, "Type", pobj_name("ExtGState"));
  pobj_dict_set(g, "ca", pobj_real(a));
  pobj_dict_set(g, "CA", pobj_real(a));
  return pc_add_resource(d, res, "ExtGState", "FoGS", g, name, cap);
}
int watermark_apply(PDoc *d, const WMOpts *o)
{
  int p, last = o->last < 0 || o->last >= d->npages ? d->npages - 1 : o->last, n = 0, img = 0, iw = 0, ih = 0;
  double op = o->opacity > 0 ? o->opacity : 0.3;
  if (o->image && !(img = image_xobject(d, o->image, o->image_len, &iw, &ih))) return 0;
  for (p = o->first < 0 ? 0 : o->first; p <= last; p++) {
    PObj *pg = pc_page(d, p), *res;
    double m[6], w, h, a = o->angle * M_PI / 180, ca = cos(a), sa = sin(a);
    char gs[32], nm[32];
    Buf b;
    if (!pg || !(res = pc_resources(d, pg))) continue;
    page_display_matrix(d, p, m, &w, &h);
    if (!gs_alpha(d, res, op, gs, sizeof gs)) continue;
    buf_init(&b);
    buf_adds(&b, "/Artifact <</Type /Pagination /Subtype /Watermark>> BDC\n");
    put_cm(&b, m);
    buf_printf(&b, "/%s gs\n", gs);
    /* turned about the page's centre */
    om_num(&b, ca); om_num(&b, sa); om_num(&b, -sa); om_num(&b, ca); om_num(&b, w / 2); om_num(&b, h / 2); buf_adds(&b, "cm\n");
    if (img) {
      double sc = o->size > 0 ? o->size : 0.5, dw = w * sc, dh = iw ? dw * ih / iw : dw;
      if (!pc_add_resource(d, res, "XObject", "FoWM", om_ref(img), nm, sizeof nm)) { buf_free(&b); continue; }
      om_num(&b, dw); buf_adds(&b, "0 0 "); om_num(&b, dh); om_num(&b, -dw / 2); om_num(&b, -dh / 2); buf_printf(&b, "cm /%s Do\n", nm);
    } else {
      double size = o->size > 0 ? o->size : 72, tw;
      int len;
      char *s = latin(o->text, &len);
      if (!s || !pc_add_resource(d, res, "Font", "FoWM", om_std_font(fonts4[clampi(o->font, 0, 3)]), nm, sizeof nm)) { free(s); buf_free(&b); continue; }
      tw = om_text_width(s, len, size, o->font);
      om_num(&b, ((o->color >> 16) & 255) / 255.0); om_num(&b, ((o->color >> 8) & 255) / 255.0); om_num(&b, (o->color & 255) / 255.0); buf_adds(&b, "rg\n");
      buf_printf(&b, "BT /%s ", nm); om_num(&b, size); buf_adds(&b, "Tf "); om_num(&b, -tw / 2); om_num(&b, -size * 0.35); buf_adds(&b, "Td ");
      om_str(&b, s, len); buf_adds(&b, " Tj ET\n");
      free(s);
    }
    buf_adds(&b, "EMC\n");
    if (!b.oom && pc_add_content(d, p, b.data, b.len, o->behind, "watermark")) n++;
    buf_free(&b);
  }
  return n;
}
int background_apply(PDoc *d, u32 color, const u8 *image, size_t image_len, int first, int last)
{
  int p, n = 0, img = 0, iw, ih;
  if (last < 0 || last >= d->npages) last = d->npages - 1;
  if (image && !(img = image_xobject(d, image, image_len, &iw, &ih))) return 0;
  for (p = first < 0 ? 0 : first; p <= last; p++) {
    PObj *pg = pc_page(d, p), *res;
    double m[6], w, h;
    char nm[32];
    Buf b;
    if (!pg || !(res = pc_resources(d, pg))) continue;
    page_display_matrix(d, p, m, &w, &h);
    buf_init(&b);
    buf_adds(&b, "/Artifact <</Type /Pagination /Subtype /Background>> BDC\n");
    put_cm(&b, m);
    if (img) {
      if (!pc_add_resource(d, res, "XObject", "FoBG", om_ref(img), nm, sizeof nm)) { buf_free(&b); continue; }
      om_num(&b, w); buf_adds(&b, "0 0 "); om_num(&b, h); buf_printf(&b, "0 0 cm /%s Do\n", nm);
    } else {
      om_num(&b, ((color >> 16) & 255) / 255.0); om_num(&b, ((color >> 8) & 255) / 255.0); om_num(&b, (color & 255) / 255.0); buf_adds(&b, "rg\n");
      buf_adds(&b, "0 0 "); om_num(&b, w); om_num(&b, h); buf_adds(&b, "re f\n");
    }
    buf_adds(&b, "EMC\n");
    if (!b.oom && pc_add_content(d, p, b.data, b.len, 1, "background")) n++;
    buf_free(&b);
  }
  return n;
}

/* ------------------------------------------------------------------ text, pictures, links */
int page_add_text(PDoc *d, int page, double x, double y, const char *utf8, double size, u32 color, int font)
{
  PObj *pg = pc_page(d, page), *res;
  char nm[32], *s;
  int len, start = 0, i, r;
  Buf b;
  if (!pg || !utf8 || !(res = pc_resources(d, pg))) return 0;
  if (size <= 0) size = 12;
  if (!pc_add_resource(d, res, "Font", "FoTx", om_std_font(fonts4[clampi(font, 0, 3)]), nm, sizeof nm)) return 0;
  if (!(s = latin(utf8, &len))) return 0;
  buf_init(&b);
  om_num(&b, ((color >> 16) & 255) / 255.0); om_num(&b, ((color >> 8) & 255) / 255.0); om_num(&b, (color & 255) / 255.0); buf_adds(&b, "rg\n");
  buf_printf(&b, "BT /%s ", nm); om_num(&b, size); buf_adds(&b, "Tf "); om_num(&b, size * 1.2); buf_adds(&b, "TL ");
  om_num(&b, x); om_num(&b, y - size * 0.8); buf_adds(&b, "Td\n");
  for (i = 0; i <= len; i++) if (i == len || s[i] == '\n') {
    if (start) buf_adds(&b, "T* ");
    om_str(&b, s + start, i - start); buf_adds(&b, " Tj\n");
    start = i + 1;
  }
  buf_adds(&b, "ET\n");
  free(s);
  r = !b.oom && pc_add_content(d, page, b.data, b.len, 0, "text");
  buf_free(&b);
  return r;
}
/* Adobe's APP14 marker: a CMYK JPEG stored inverted */
static int jpeg_adobe(const u8 *f, size_t n)
{
  size_t i;
  for (i = 2; i + 4 < n; ) {
    unsigned len;
    if (f[i] != 0xFF) return 0;
    if (f[i + 1] == 0xDA) return 0;
    len = ((unsigned)f[i + 2] << 8) | f[i + 3];
    if (f[i + 1] == 0xEE && i + 9 < n && !memcmp(f + i + 4, "Adobe", 5)) return 1;
    i += 2 + len;
  }
  return 0;
}
int image_xobject(PDoc *d, const u8 *file, size_t n, int *w, int *h)
{
  u32 iw, ih;
  int kind = img_info(file, (u32)n, &iw, &ih);
  PObj *dict;
  if (kind == IMG_NONE || !iw || !ih || iw > 30000 || ih > 30000) return 0;
  *w = (int)iw; *h = (int)ih;
  if (!(dict = pobj_new(PO_DICT))) return 0;
  pobj_dict_set(dict, "Type", pobj_name("XObject"));
  pobj_dict_set(dict, "Subtype", pobj_name("Image"));
  pobj_dict_set(dict, "Width", pobj_int((long)iw));
  pobj_dict_set(dict, "Height", pobj_int((long)ih));
  pobj_dict_set(dict, "BitsPerComponent", pobj_int(8));
  if (kind == IMG_JPEG) {
    int ok = 0, comps = img_jpeg_components(file, (u32)n, &ok);
    pobj_dict_set(dict, "ColorSpace", pobj_name(comps == 1 ? "DeviceGray" : comps == 4 ? "DeviceCMYK" : "DeviceRGB"));
    if (comps == 4 && jpeg_adobe(file, n)) { double dec[8] = { 1, 0, 1, 0, 1, 0, 1, 0 }; pobj_dict_set(dict, "Decode", om_arr(dec, 8)); }
    pobj_dict_set(dict, "Filter", pobj_name("DCTDecode"));
    return om_stream(d, dict, file, n);
  } else {
    u8 *rgb = img_decode(file, (u32)n, iw, ih), *z;
    size_t zl;
    int num;
    if (!rgb) { pobj_free(dict); return 0; }
    z = flt_deflate(rgb, (size_t)iw * ih * 3, 6, &zl);
    free(rgb);
    if (!z) { pobj_free(dict); return 0; }
    pobj_dict_set(dict, "ColorSpace", pobj_name("DeviceRGB"));
    pobj_dict_set(dict, "Filter", pobj_name("FlateDecode"));
    num = om_stream(d, dict, z, zl);
    free(z);
    return num;
  }
}
int page_add_image(PDoc *d, int page, const double *rect, const u8 *file, size_t n)
{
  PObj *pg = pc_page(d, page), *res;
  int num, iw, ih, r;
  double x0, y0, rw, rh, sw, sh;
  char nm[32];
  Buf b;
  if (!pg || !(res = pc_resources(d, pg)) || !(num = image_xobject(d, file, n, &iw, &ih))) return 0;
  if (!pc_add_resource(d, res, "XObject", "FoIm", om_ref(num), nm, sizeof nm)) return 0;
  x0 = rect[0] < rect[2] ? rect[0] : rect[2]; y0 = rect[1] < rect[3] ? rect[1] : rect[3];
  rw = fabs(rect[2] - rect[0]); rh = fabs(rect[3] - rect[1]);
  /* in proportion, centred */
  sw = rw; sh = rw * ih / iw;
  if (sh > rh) { sh = rh; sw = rh * iw / ih; }
  buf_init(&b);
  om_num(&b, sw); buf_adds(&b, "0 0 "); om_num(&b, sh); om_num(&b, x0 + (rw - sw) / 2); om_num(&b, y0 + (rh - sh) / 2);
  buf_printf(&b, "cm /%s Do\n", nm);
  r = !b.oom && pc_add_content(d, page, b.data, b.len, 0, "image");
  buf_free(&b);
  return r;
}
static int add_annot_obj(PDoc *d, int page, PObj *a)
{
  PObj *pg = pc_page(d, page), *arr;
  int num;
  if (!pg || !a) { pobj_free(a); return 0; }
  arr = pdf_dget(d, pg, "Annots");
  if (!arr || arr->type != PO_ARRAY) { if (!(arr = pobj_new(PO_ARRAY))) { pobj_free(a); return 0; } pobj_dict_set(pg, "Annots", arr); }
  else if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *c = pobj_copy(arr); if (!c) { pobj_free(a); return 0; } pobj_dict_set(pg, "Annots", c); arr = c; }
  if (!(num = pdf_new_object(d, a))) return 0;
  pobj_array_add(arr, om_ref(num));
  d->changed = 1;
  return num;
}
int page_add_link(PDoc *d, int page, const double *rect, int dest_page, double top, const char *uri)
{
  PObj *a = pobj_new(PO_DICT), *bs = pobj_new(PO_DICT);
  double r[4], bw[3] = { 0, 0, 0 };
  if (!a || !bs) { pobj_free(a); pobj_free(bs); return 0; }
  r[0] = rect[0] < rect[2] ? rect[0] : rect[2]; r[2] = rect[0] < rect[2] ? rect[2] : rect[0];
  r[1] = rect[1] < rect[3] ? rect[1] : rect[3]; r[3] = rect[1] < rect[3] ? rect[3] : rect[1];
  pobj_dict_set(a, "Type", pobj_name("Annot"));
  pobj_dict_set(a, "Subtype", pobj_name("Link"));
  pobj_dict_set(a, "Rect", om_arr(r, 4));
  pobj_dict_set(a, "Border", om_arr(bw, 3));
  pobj_free(bs);
  if (uri && *uri) {
    PObj *act = pobj_new(PO_DICT);
    pobj_dict_set(act, "S", pobj_name("URI"));
    pobj_dict_set(act, "URI", pobj_string(uri, (int)strlen(uri)));
    pobj_dict_set(a, "A", act);
  } else {
    PObj *dest = pobj_new(PO_ARRAY);
    int pn = dest_page >= 0 && dest_page < d->npages ? om_num_of(d, d->pages[dest_page]) : 0;
    if (!pn) { pobj_free(dest); pobj_free(a); return 0; }
    pobj_array_add(dest, om_ref(pn));
    pobj_array_add(dest, pobj_name("XYZ"));
    pobj_array_add(dest, pobj_new(PO_NULL));
    pobj_array_add(dest, top >= 0 ? pobj_real(top) : pobj_new(PO_NULL));
    pobj_array_add(dest, pobj_new(PO_NULL));
    pobj_dict_set(a, "Dest", dest);
  }
  return add_annot_obj(d, page, a) != 0;
}

/* ------------------------------------------------------------------ attachments */
typedef struct { char *name; PObj *spec; } NPair;
static void names_walk(PDoc *d, PObj *node, NPair **out, int *n, int *cap, int depth)
{
  PObj *names, *kids;
  int i;
  if (!node || node->type != PO_DICT || depth > 16) return;
  if ((names = pdf_dget(d, node, "Names")) && names->type == PO_ARRAY)
    for (i = 0; i + 1 < names->u.arr.n; i += 2) {
      PObj *k = pdf_resolve(d, names->u.arr.v[i]);
      if (!k || k->type != PO_STRING) continue;
      if (*n >= *cap) { int nc = *cap ? *cap * 2 : 8; NPair *t = (NPair *)realloc(*out, (size_t)nc * sizeof(NPair)); if (!t) return; *out = t; *cap = nc; }
      (*out)[*n].name = pdf_text_string(k);
      (*out)[*n].spec = names->u.arr.v[i + 1];
      (*n)++;
    }
  if ((kids = pdf_dget(d, node, "Kids")) && kids->type == PO_ARRAY)
    for (i = 0; i < kids->u.arr.n; i++) names_walk(d, pdf_resolve(d, kids->u.arr.v[i]), out, n, cap, depth + 1);
}
static PObj *ef_root(PDoc *d, int create)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *names, *ef;
  if (!root) return NULL;
  names = pdf_dget(d, root, "Names");
  if (!names) { if (!create || !(names = pobj_new(PO_DICT))) return NULL; pobj_dict_set(root, "Names", names); }
  ef = pdf_dget(d, names, "EmbeddedFiles");
  if (!ef) { if (!create || !(ef = pobj_new(PO_DICT))) return NULL; pobj_dict_set(names, "EmbeddedFiles", ef); }
  return ef;
}
static int pairs_get(PDoc *d, NPair **out)
{
  int n = 0, cap = 0;
  *out = NULL;
  names_walk(d, ef_root(d, 0), out, &n, &cap, 0);
  return n;
}
static void pairs_free(NPair *p, int n) { int i; for (i = 0; i < n; i++) free(p[i].name); free(p); }
int attach_list(PDoc *d, PAttach **out)
{
  NPair *p;
  int n = pairs_get(d, &p), i;
  *out = n ? (PAttach *)calloc((size_t)n, sizeof(PAttach)) : NULL;
  if (n && !*out) { pairs_free(p, n); return 0; }
  for (i = 0; i < n; i++) {
    PObj *spec = pdf_resolve(d, p[i].spec), *ef = spec ? pdf_dget(d, spec, "EF") : NULL, *f = ef ? pdf_dget(d, ef, "UF") : NULL, *par;
    PObj *ds = spec ? pdf_dget(d, spec, "Desc") : NULL;
    if (!f && ef) f = pdf_dget(d, ef, "F");
    (*out)[i].name = p[i].name; p[i].name = NULL;
    (*out)[i].desc = ds && ds->type == PO_STRING ? pdf_text_string(ds) : NULL;
    par = f ? pdf_dget(d, f, "Params") : NULL;
    (*out)[i].size = par ? dbl_long(pdf_dnum(d, par, "Size", -1)) : -1;
    if ((*out)[i].size < 0 && f && f->type == PO_STREAM) { size_t k; u8 *dt = pdf_stream_data(d, f, &k); (*out)[i].size = dt ? (long)k : -1; free(dt); }
  }
  pairs_free(p, n);
  return n;
}
void attach_free(PAttach *a, int n) { int i; for (i = 0; i < n; i++) { free(a[i].name); free(a[i].desc); } free(a); }
/* the tree rewritten as one sorted /Names array */
static int pairs_put(PDoc *d, NPair *p, int n)
{
  PObj *ef = ef_root(d, 1), *arr;
  int i, j;
  if (!ef || !(arr = pobj_new(PO_ARRAY))) return 0;
  for (i = 1; i < n; i++) for (j = i; j > 0 && strcmp(p[j - 1].name ? p[j - 1].name : "", p[j].name ? p[j].name : "") > 0; j--) { NPair t = p[j]; p[j] = p[j - 1]; p[j - 1] = t; }
  for (i = 0; i < n; i++) { pobj_array_add(arr, om_text(p[i].name ? p[i].name : "")); pobj_array_add(arr, pobj_copy(p[i].spec)); }
  pobj_dict_del(ef, "Kids");
  pobj_dict_set(ef, "Names", arr);
  d->changed = 1;
  return 1;
}
int attach_add(PDoc *d, const char *name, const u8 *data, size_t n, const char *desc)
{
  NPair *p, *np;
  int cnt = pairs_get(d, &p), i, sn, fn, r;
  PObj *sd = pobj_new(PO_DICT), *par = pobj_new(PO_DICT), *spec, *ef;
  if (!sd || !par) { pobj_free(sd); pobj_free(par); pairs_free(p, cnt); return 0; }
  pobj_dict_set(sd, "Type", pobj_name("EmbeddedFile"));
  pobj_dict_set(par, "Size", pobj_int((long)n));
  pobj_dict_set(par, "ModDate", om_date());
  pobj_dict_set(sd, "Params", par);
  if (!(sn = om_stream(d, sd, data, n))) { pairs_free(p, cnt); return 0; }
  spec = pobj_new(PO_DICT); ef = pobj_new(PO_DICT);
  if (!spec || !ef) { pobj_free(spec); pobj_free(ef); pairs_free(p, cnt); return 0; }
  pobj_dict_set(spec, "Type", pobj_name("Filespec"));
  pobj_dict_set(spec, "F", om_text(name));
  pobj_dict_set(spec, "UF", om_text(name));
  if (desc && *desc) pobj_dict_set(spec, "Desc", om_text(desc));
  pobj_dict_set(ef, "F", om_ref(sn));
  pobj_dict_set(ef, "UF", om_ref(sn));
  pobj_dict_set(spec, "EF", ef);
  if (!(fn = pdf_new_object(d, spec))) { pairs_free(p, cnt); return 0; }
  /* one of the same name is replaced */
  for (i = 0; i < cnt; i++) if (p[i].name && !strcmp(p[i].name, name)) break;
  if (i == cnt) {
    if (!(np = (NPair *)realloc(p, (size_t)(cnt + 1) * sizeof(NPair)))) { pairs_free(p, cnt); return 0; }
    p = np; p[cnt].name = str_dup(name); cnt++;
  }
  {
    PObj *ref = om_ref(fn);
    PObj *keep[1];
    keep[0] = ref;
    p[i].spec = ref;
    r = pairs_put(d, p, cnt);
    pobj_free(keep[0]);
  }
  pairs_free(p, cnt);
  return r;
}
u8 *attach_data(PDoc *d, const char *name, size_t *n)
{
  NPair *p;
  int cnt = pairs_get(d, &p), i;
  u8 *res = NULL;
  for (i = 0; i < cnt && !res; i++) if (p[i].name && !strcmp(p[i].name, name)) {
    PObj *spec = pdf_resolve(d, p[i].spec), *ef = spec ? pdf_dget(d, spec, "EF") : NULL, *f = ef ? pdf_dget(d, ef, "UF") : NULL;
    if (!f && ef) f = pdf_dget(d, ef, "F");
    if (f && f->type == PO_STREAM) res = pdf_stream_data(d, f, n);
  }
  pairs_free(p, cnt);
  return res;
}
int attach_delete(PDoc *d, const char *name)
{
  NPair *p;
  int cnt = pairs_get(d, &p), i, k = 0, found = 0, r;
  for (i = 0; i < cnt; i++) {
    if (p[i].name && !strcmp(p[i].name, name) && !found) { free(p[i].name); found = 1; continue; }
    p[k++] = p[i];
  }
  r = found ? pairs_put(d, p, k) : 0;
  pairs_free(p, k);
  return r;
}

/* ------------------------------------------------------------------ bookmarks */
POutline *outline_item(const char *title, int page, double top)
{
  POutline *o = (POutline *)calloc(1, sizeof *o);
  if (!o) return NULL;
  o->title = str_dup(title ? title : "");
  o->page = page; o->top = top;
  return o;
}
/* the visible items under an open item (for /Count) */
static int visible(const POutline *o)
{
  int n = 0;
  for (; o; o = o->next) { n++; if (o->open) n += visible(o->child); }
  return n;
}
static int count_all(const POutline *o) { int n = 0; for (; o; o = o->next) n += 1 + count_all(o->child); return n; }
/* the items of a list written (their numbers given out first); returns the first's number, *last the last's */
static int write_items(PDoc *d, const POutline *list, int parent, int *last, int depth)
{
  int n = 0, i, *nums, first = 0;
  const POutline *o;
  for (o = list; o; o = o->next) n++;
  if (!n || depth > 32 || !(nums = (int *)malloc((size_t)n * sizeof(int)))) return 0;
  for (i = 0; i < n; i++) nums[i] = pdf_new_object(d, pobj_new(PO_DICT));
  for (o = list, i = 0; o; o = o->next, i++) {
    PObj *it = pdf_get(d, nums[i]);
    if (!it) continue;
    pobj_dict_set(it, "Title", om_text(o->title));
    pobj_dict_set(it, "Parent", om_ref(parent));
    if (i > 0) pobj_dict_set(it, "Prev", om_ref(nums[i - 1]));
    if (i + 1 < n) pobj_dict_set(it, "Next", om_ref(nums[i + 1]));
    if (o->uri) {
      PObj *a = pobj_new(PO_DICT);
      pobj_dict_set(a, "S", pobj_name("URI"));
      pobj_dict_set(a, "URI", pobj_string(o->uri, (int)strlen(o->uri)));
      pobj_dict_set(it, "A", a);
    } else if (o->page >= 0 && o->page < d->npages) {
      PObj *dest = pobj_new(PO_ARRAY);
      int pn = om_num_of(d, d->pages[o->page]);
      if (dest && pn) {
        pobj_array_add(dest, om_ref(pn));
        pobj_array_add(dest, pobj_name("XYZ"));
        pobj_array_add(dest, pobj_new(PO_NULL));
        pobj_array_add(dest, o->top >= 0 ? pobj_real(o->top) : pobj_new(PO_NULL));
        pobj_array_add(dest, pobj_new(PO_NULL));
        pobj_dict_set(it, "Dest", dest);
      } else pobj_free(dest);
    }
    if (o->child) {
      int cl = 0, cf = write_items(d, o->child, nums[i], &cl, depth + 1);
      if (cf) {
        pobj_dict_set(it, "First", om_ref(cf));
        pobj_dict_set(it, "Last", om_ref(cl));
        pobj_dict_set(it, "Count", pobj_int(o->open ? visible(o->child) : -count_all(o->child)));
      }
    }
  }
  first = nums[0];
  *last = nums[n - 1];
  free(nums);
  return first;
}
int outline_write(PDoc *d, const POutline *tree)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *ol;
  int num, first, last = 0;
  if (!root) return 0;
  d->changed = 1;
  if (!tree) { pobj_dict_del(root, "Outlines"); return 1; }
  if (!(ol = pobj_new(PO_DICT)) || !(num = pdf_new_object(d, ol))) return 0;
  pobj_dict_set(ol, "Type", pobj_name("Outlines"));
  first = write_items(d, tree, num, &last, 0);
  if (first) {
    pobj_dict_set(ol, "First", om_ref(first));
    pobj_dict_set(ol, "Last", om_ref(last));
    pobj_dict_set(ol, "Count", pobj_int(visible(tree)));
  }
  pobj_dict_set(root, "Outlines", om_ref(num));
  return 1;
}
typedef struct { float size; int chars; } SizeCount;
POutline *outline_from_styles(PDoc *d, TextPage *(*get)(void *ud, int page), void *ud)
{
  SizeCount sc[64];
  int nsc = 0, p, i, k;
  float body = 0, lev[3] = { 0, 0, 0 };
  POutline *head = NULL, *cur[3] = { NULL, NULL, NULL }, *tail = NULL;
  /* the sizes used, by how many characters */
  for (p = 0; p < d->npages; p++) {
    TextPage *t = get(ud, p);
    if (!t) continue;
    for (i = 0; i < t->no; i++) {
      float s;
      if (t->o[i].u <= 32) continue;
      s = (float)floor(t->o[i].size * 2 + 0.5) / 2;
      for (k = 0; k < nsc && sc[k].size != s; k++) ;
      if (k == nsc) { if (nsc == 64) continue; sc[nsc].size = s; sc[nsc].chars = 0; nsc++; }
      sc[k].chars++;
    }
  }
  for (k = 0, i = 0; k < nsc; k++) if (sc[k].chars > i) { i = sc[k].chars; body = sc[k].size; }
  /* the three largest sizes clearly above the body */
  for (k = 0; k < nsc; k++) {
    float s = sc[k].size;
    if (s < body * 1.15f || sc[k].chars < 2) continue;
    if (s > lev[0]) { lev[2] = lev[1]; lev[1] = lev[0]; lev[0] = s; }
    else if (s > lev[1] && s != lev[0]) { lev[2] = lev[1]; lev[1] = s; }
    else if (s > lev[2] && s != lev[1] && s != lev[0]) lev[2] = s;
  }
  if (lev[0] <= 0) return NULL;
  for (p = 0; p < d->npages; p++) {
    TextPage *t = get(ud, p);
    int a = 0;
    if (!t) continue;
    for (i = 0; i <= t->no; i++) {
      if (i < t->no && t->o[i].u != 10) continue;
      /* a line a..i-1 */
      if (i > a) {
        float s = 0;
        int level = -1, letters = 0, j;
        for (j = a; j < i; j++) if (t->o[j].u > 32) { s = (float)floor(t->o[j].size * 2 + 0.5) / 2; letters++; break; }
        for (k = 0; k < 3; k++) if (lev[k] > 0 && s == lev[k]) level = k;
        if (level >= 0 && i - a < 160) {
          char *txt = textpage_text(t, a, i - 1);
          POutline *o;
          size_t l = txt ? strlen(txt) : 0;
          while (l && (txt[l - 1] == ' ' || txt[l - 1] == '\n')) txt[--l] = 0;
          if (txt && l && letters && (o = outline_item(txt, p, t->o[a].y1 + 4))) {
            o->open = 1;
            if (level == 0 || !cur[level - 1]) {
              /* (a lower level with no heading above it goes at the top level) */
              if (tail) tail->next = o; else head = o;
              tail = o;
              cur[0] = o; cur[1] = cur[2] = NULL;
              if (level > 0) { cur[0] = o; }
            } else {
              POutline *par = cur[level - 1], *c = par->child;
              if (!c) par->child = o; else { while (c->next) c = c->next; c->next = o; }
              cur[level] = o;
              if (level < 2) cur[level + 1] = NULL;
            }
          }
          free(txt);
        }
      }
      a = i + 1;
    }
  }
  return head;
}

/* ------------------------------------------------------------------ annotations */
int annots_flatten(PDoc *d, int page)
{
  int p, i, n = 0;
  for (p = page < 0 ? 0 : page; p < d->npages && (page < 0 || p == page); p++) {
    PObj *pg = d->pages[p], *a = pdf_dget(d, pg, "Annots"), *res;
    Buf ops;
    if (!a || a->type != PO_ARRAY) continue;
    if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *c = pobj_copy(a); if (!c) continue; pobj_dict_set(pg, "Annots", c); a = c; }
    if (!(res = pc_resources(d, pg))) continue;
    buf_init(&ops);
    for (i = 0; i < a->u.arr.n; ) {
      PObj *an = pdf_resolve(d, a->u.arr.v[i]);
      const char *st = an && an->type == PO_DICT ? pdf_dname(d, an, "Subtype") : NULL;
      if (st && (!strcmp(st, "Link") || !strcmp(st, "Widget"))) { i++; continue; }
      if (st && !strcmp(st, "Popup")) { pobj_free(a->u.arr.v[i]); memmove(a->u.arr.v + i, a->u.arr.v + i + 1, (size_t)(a->u.arr.n - i - 1) * sizeof(PObj *)); a->u.arr.n--; continue; }
      if (an && pc_flatten_annot(d, p, an, &ops, res)) {
        n++;
        pobj_free(a->u.arr.v[i]);
        memmove(a->u.arr.v + i, a->u.arr.v + i + 1, (size_t)(a->u.arr.n - i - 1) * sizeof(PObj *));
        a->u.arr.n--;
      } else i++;
    }
    if (ops.len) pc_add_content(d, p, ops.data, ops.len, 0, "flatten");
    buf_free(&ops);
  }
  if (n) d->changed = 1;
  return n;
}
static PObj *markup_annot(PDoc *d, int page, const char *subtype, const double *r, u32 color, const char *author, const char *text)
{
  PObj *a = pobj_new(PO_DICT);
  int pn = page >= 0 && page < d->npages ? om_num_of(d, d->pages[page]) : 0;
  if (!a) return NULL;
  pobj_dict_set(a, "Type", pobj_name("Annot"));
  pobj_dict_set(a, "Subtype", pobj_name(subtype));
  pobj_dict_set(a, "Rect", om_arr(r, 4));
  pobj_dict_set(a, "F", pobj_int(4));
  if (pn) pobj_dict_set(a, "P", om_ref(pn));
  pobj_dict_set(a, "C", om_rgb(color));
  if (author && *author) pobj_dict_set(a, "T", om_text(author));
  if (text) pobj_dict_set(a, "Contents", om_text(text));
  pobj_dict_set(a, "M", om_date());
  pobj_dict_set(a, "CreationDate", om_date());
  return a;
}
static void rgb(Buf *b, u32 c, int stroke)
{
  om_num(b, ((c >> 16) & 255) / 255.0); om_num(b, ((c >> 8) & 255) / 255.0); om_num(b, (c & 255) / 255.0);
  buf_adds(b, stroke ? "RG\n" : "rg\n");
}
static void set_ap(PDoc *d, PObj *a, Buf *b, const double *r, PObj *res)
{
  int f = om_form(d, b->data ? b->data : "", b->len, r, res);
  PObj *ap;
  if (!f || !(ap = pobj_new(PO_DICT))) return;
  pobj_dict_set(ap, "N", om_ref(f));
  pobj_dict_set(a, "AP", ap);
}
static PObj *one_font(const char *name)
{
  PObj *res = pobj_new(PO_DICT), *f = pobj_new(PO_DICT);
  if (!res || !f) { pobj_free(res); pobj_free(f); return NULL; }
  pobj_dict_set(f, "FoF", om_std_font(name));
  pobj_dict_set(res, "Font", f);
  return res;
}
int annot_stamp(PDoc *d, int page, double x, double y, const char *text, u32 color, const char *author)
{
  int len;
  char *s = latin(text, &len);
  double size = 18, tw, r[4], w, h, rad = 6;
  PObj *a;
  Buf b;
  if (!s) return 0;
  tw = om_text_width(s, len, size, 1);
  w = tw + 24; h = size + 16;
  r[0] = x - w / 2; r[1] = y - h / 2; r[2] = x + w / 2; r[3] = y + h / 2;
  if (!(a = markup_annot(d, page, "Stamp", r, color, author, text))) { free(s); return 0; }
  {
    /* (Folio's own stamp: named for other readers) */
    char nm[64];
    int i, k = 0;
    for (i = 0; text[i] && k < 60; i++) if ((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z')) nm[k++] = text[i];
    nm[k] = 0;
    pobj_dict_set(a, "Name", pobj_name(k ? nm : "Draft"));
  }
  buf_init(&b);
  rgb(&b, color, 1); rgb(&b, color, 0);
  buf_adds(&b, "2 w\n");
  /* a rounded rectangle, inset by the line */
  {
    double x0 = r[0] + 1.5, y0 = r[1] + 1.5, x1 = r[2] - 1.5, y1 = r[3] - 1.5, k = rad * 0.5523;
    om_num(&b, x0 + rad); om_num(&b, y0); buf_adds(&b, "m ");
    om_num(&b, x1 - rad); om_num(&b, y0); buf_adds(&b, "l ");
    om_num(&b, x1 - rad + k); om_num(&b, y0); om_num(&b, x1); om_num(&b, y0 + rad - k); om_num(&b, x1); om_num(&b, y0 + rad); buf_adds(&b, "c ");
    om_num(&b, x1); om_num(&b, y1 - rad); buf_adds(&b, "l ");
    om_num(&b, x1); om_num(&b, y1 - rad + k); om_num(&b, x1 - rad + k); om_num(&b, y1); om_num(&b, x1 - rad); om_num(&b, y1); buf_adds(&b, "c ");
    om_num(&b, x0 + rad); om_num(&b, y1); buf_adds(&b, "l ");
    om_num(&b, x0 + rad - k); om_num(&b, y1); om_num(&b, x0); om_num(&b, y1 - rad + k); om_num(&b, x0); om_num(&b, y1 - rad); buf_adds(&b, "c ");
    om_num(&b, x0); om_num(&b, y0 + rad); buf_adds(&b, "l ");
    om_num(&b, x0); om_num(&b, y0 + rad - k); om_num(&b, x0 + rad - k); om_num(&b, y0); om_num(&b, x0 + rad); om_num(&b, y0); buf_adds(&b, "c S\n");
  }
  buf_adds(&b, "BT /FoF "); om_num(&b, size); buf_adds(&b, "Tf "); om_num(&b, x - tw / 2); om_num(&b, y - size * 0.35); buf_adds(&b, "Td ");
  om_str(&b, s, len); buf_adds(&b, " Tj ET\n");
  free(s);
  set_ap(d, a, &b, r, one_font("Helvetica-Bold"));
  buf_free(&b);
  return add_annot_obj(d, page, a) != 0;
}
int annot_arrow(PDoc *d, int page, const double *c, double width, u32 color, const char *author)
{
  double r[4], dx = c[2] - c[0], dy = c[3] - c[1], len = sqrt(dx * dx + dy * dy), hl = 6 + width * 3, ux, uy, m = hl + width;
  PObj *a, *le, *bs;
  Buf b;
  if (len < 1) return 0;
  ux = dx / len; uy = dy / len;
  r[0] = (c[0] < c[2] ? c[0] : c[2]) - m; r[2] = (c[0] < c[2] ? c[2] : c[0]) + m;
  r[1] = (c[1] < c[3] ? c[1] : c[3]) - m; r[3] = (c[1] < c[3] ? c[3] : c[1]) + m;
  if (!(a = markup_annot(d, page, "Line", r, color, author, NULL))) return 0;
  pobj_dict_set(a, "L", om_arr(c, 4));
  le = pobj_new(PO_ARRAY);
  pobj_array_add(le, pobj_name("None")); pobj_array_add(le, pobj_name("OpenArrow"));
  pobj_dict_set(a, "LE", le);
  if ((bs = pobj_new(PO_DICT))) { pobj_dict_set(bs, "W", pobj_real(width)); pobj_dict_set(a, "BS", bs); }
  buf_init(&b);
  rgb(&b, color, 1);
  om_num(&b, width); buf_adds(&b, "w 1 J 1 j\n");
  om_num(&b, c[0]); om_num(&b, c[1]); buf_adds(&b, "m "); om_num(&b, c[2]); om_num(&b, c[3]); buf_adds(&b, "l S\n");
  om_num(&b, c[2] - hl * (ux * 0.866 - uy * 0.5)); om_num(&b, c[3] - hl * (uy * 0.866 + ux * 0.5)); buf_adds(&b, "m ");
  om_num(&b, c[2]); om_num(&b, c[3]); buf_adds(&b, "l ");
  om_num(&b, c[2] - hl * (ux * 0.866 + uy * 0.5)); om_num(&b, c[3] - hl * (uy * 0.866 - ux * 0.5)); buf_adds(&b, "l S\n");
  set_ap(d, a, &b, r, NULL);
  buf_free(&b);
  return add_annot_obj(d, page, a) != 0;
}
int annot_cloud(PDoc *d, int page, const double *rect, double width, u32 color, const char *author)
{
  double x0 = rect[0] < rect[2] ? rect[0] : rect[2], x1 = rect[0] < rect[2] ? rect[2] : rect[0];
  double y0 = rect[1] < rect[3] ? rect[1] : rect[3], y1 = rect[1] < rect[3] ? rect[3] : rect[1];
  double rad = 8, r[4], v[8], cx[4], cy[4];
  int side, k;
  PObj *a, *be, *bs;
  Buf b;
  if (x1 - x0 < 4 * rad || y1 - y0 < 4 * rad) return 0;
  r[0] = x0 - rad - width; r[1] = y0 - rad - width; r[2] = x1 + rad + width; r[3] = y1 + rad + width;
  if (!(a = markup_annot(d, page, "Polygon", r, color, author, NULL))) return 0;
  v[0] = x0; v[1] = y0; v[2] = x1; v[3] = y0; v[4] = x1; v[5] = y1; v[6] = x0; v[7] = y1;
  pobj_dict_set(a, "Vertices", om_arr(v, 8));
  if ((be = pobj_new(PO_DICT))) { pobj_dict_set(be, "S", pobj_name("C")); pobj_dict_set(be, "I", pobj_int(1)); pobj_dict_set(a, "BE", be); }
  if ((bs = pobj_new(PO_DICT))) { pobj_dict_set(bs, "W", pobj_real(width)); pobj_dict_set(a, "BS", bs); }
  buf_init(&b);
  rgb(&b, color, 1);
  om_num(&b, width); buf_adds(&b, "w 1 j\n");
  /* scallops: half circles bulging outwards along each side */
  cx[0] = x0; cy[0] = y0; cx[1] = x1; cy[1] = y0; cx[2] = x1; cy[2] = y1; cx[3] = x0; cy[3] = y1;
  om_num(&b, x0); om_num(&b, y0); buf_adds(&b, "m\n");
  for (side = 0; side < 4; side++) {
    double ax = cx[side], ay = cy[side], bx = cx[(side + 1) % 4], by = cy[(side + 1) % 4];
    double L = sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    int nseg = (int)(L / (2 * rad));
    double ux, uy, nx, ny, seg;
    if (nseg < 1) nseg = 1;
    seg = L / nseg; ux = (bx - ax) / L; uy = (by - ay) / L;
    nx = uy; ny = -ux;                      /* (outwards for an anticlockwise walk) */
    for (k = 0; k < nseg; k++) {
      double sx = ax + ux * seg * k, sy = ay + uy * seg * k, ex = sx + ux * seg, ey = sy + uy * seg, h = seg / 2 * 1.33;
      om_num(&b, sx + nx * h); om_num(&b, sy + ny * h); om_num(&b, ex + nx * h); om_num(&b, ey + ny * h); om_num(&b, ex); om_num(&b, ey); buf_adds(&b, "c\n");
    }
  }
  buf_adds(&b, "S\n");
  set_ap(d, a, &b, r, NULL);
  buf_free(&b);
  return add_annot_obj(d, page, a) != 0;
}
static PObj *measure_dict(double scale, const char *unit)
{
  PObj *m = pobj_new(PO_DICT), *nf = pobj_new(PO_DICT), *arr = pobj_new(PO_ARRAY), *arr2, *nf2;
  char r[64];
  if (!m || !nf || !arr) { pobj_free(m); pobj_free(nf); pobj_free(arr); return NULL; }
  snprintf(r, sizeof r, "1 pt = %g %s", 1 / scale, unit);
  pobj_dict_set(m, "Type", pobj_name("Measure"));
  pobj_dict_set(m, "Subtype", pobj_name("RL"));
  pobj_dict_set(m, "R", pobj_string(r, (int)strlen(r)));
  pobj_dict_set(nf, "Type", pobj_name("NumberFormat"));
  pobj_dict_set(nf, "U", om_text(unit));
  pobj_dict_set(nf, "C", pobj_real(1 / scale));
  pobj_dict_set(nf, "D", pobj_int(100));
  arr2 = pobj_copy(nf);
  pobj_array_add(arr, nf);
  pobj_dict_set(m, "X", arr);
  nf2 = pobj_new(PO_ARRAY);
  if (nf2 && arr2) { pobj_dict_set(arr2, "C", pobj_real(1 / (scale * scale))); pobj_array_add(nf2, arr2); pobj_dict_set(m, "A", nf2); pobj_dict_set(m, "D", pobj_copy(arr)); }
  else { pobj_free(nf2); pobj_free(arr2); }
  return m;
}
static void caption(Buf *b, double x, double y, const char *text, double size)
{
  int len;
  char *s = latin(text, &len);
  double tw;
  if (!s) return;
  tw = om_text_width(s, len, size, 0);
  buf_adds(b, "BT /FoF "); om_num(b, size); buf_adds(b, "Tf "); om_num(b, x - tw / 2); om_num(b, y); buf_adds(b, "Td ");
  om_str(b, s, len); buf_adds(b, " Tj ET\n");
  free(s);
}
int annot_measure(PDoc *d, int page, const double *c, double scale, const char *unit, u32 color, const char *author)
{
  double dx = c[2] - c[0], dy = c[3] - c[1], len = sqrt(dx * dx + dy * dy), r[4];
  char txt[64];
  PObj *a, *le;
  Buf b;
  if (scale <= 0) scale = 1;
  if (!unit) unit = "pt";
  snprintf(txt, sizeof txt, "%.2f %s", len / scale, unit);
  r[0] = (c[0] < c[2] ? c[0] : c[2]) - 30; r[2] = (c[0] < c[2] ? c[2] : c[0]) + 30;
  r[1] = (c[1] < c[3] ? c[1] : c[3]) - 8; r[3] = (c[1] < c[3] ? c[3] : c[1]) + 16;
  if (!(a = markup_annot(d, page, "Line", r, color, author, txt))) return 0;
  pobj_dict_set(a, "L", om_arr(c, 4));
  le = pobj_new(PO_ARRAY);
  pobj_array_add(le, pobj_name("OpenArrow")); pobj_array_add(le, pobj_name("OpenArrow"));
  pobj_dict_set(a, "LE", le);
  pobj_dict_set(a, "IT", pobj_name("LineDimension"));
  pobj_dict_set(a, "Cap", pobj_new(PO_BOOL));
  pdf_resolve(d, pobj_dict_get(a, "Cap"))->u.b = 1;
  pobj_dict_set(a, "Measure", measure_dict(scale, unit));
  buf_init(&b);
  rgb(&b, color, 1); rgb(&b, color, 0);
  buf_adds(&b, "1 w\n");
  om_num(&b, c[0]); om_num(&b, c[1]); buf_adds(&b, "m "); om_num(&b, c[2]); om_num(&b, c[3]); buf_adds(&b, "l S\n");
  caption(&b, (c[0] + c[2]) / 2, (c[1] + c[3]) / 2 + 4, txt, 9);
  set_ap(d, a, &b, r, one_font("Helvetica"));
  buf_free(&b);
  return add_annot_obj(d, page, a) != 0;
}
int annot_measure_area(PDoc *d, int page, const double *pts, int n, int area, double scale, const char *unit, u32 color, const char *author)
{
  double r[4] = { 1e30, 1e30, -1e30, -1e30 }, v = 0, cx = 0, cy = 0;
  char txt[80];
  int i;
  PObj *a;
  Buf b;
  if (n < 2 || n > 1000) return 0;
  if (scale <= 0) scale = 1;
  if (!unit) unit = "pt";
  for (i = 0; i < n; i++) {
    double x = pts[i * 2], y = pts[i * 2 + 1], x2 = pts[((i + 1) % n) * 2], y2 = pts[((i + 1) % n) * 2 + 1];
    if (x < r[0]) r[0] = x;
    if (y < r[1]) r[1] = y;
    if (x > r[2]) r[2] = x;
    if (y > r[3]) r[3] = y;
    if (area) v += x * y2 - x2 * y;
    else if (i + 1 < n || n > 2) v += sqrt((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y));
    cx += x; cy += y;
  }
  cx /= n; cy /= n;
  if (area) snprintf(txt, sizeof txt, "%.2f sq %s", fabs(v) / 2 / (scale * scale), unit);
  else snprintf(txt, sizeof txt, "%.2f %s", v / scale, unit);
  r[0] -= 30; r[1] -= 4; r[2] += 30; r[3] += 4;
  if (!(a = markup_annot(d, page, area ? "Polygon" : "PolyLine", r, color, author, txt))) return 0;
  pobj_dict_set(a, "Vertices", om_arr(pts, n * 2));
  pobj_dict_set(a, "IT", pobj_name(area ? "PolygonDimension" : "PolyLineDimension"));
  pobj_dict_set(a, "Measure", measure_dict(scale, unit));
  buf_init(&b);
  rgb(&b, color, 1); rgb(&b, color, 0);
  buf_adds(&b, "1 w\n");
  for (i = 0; i < n; i++) { om_num(&b, pts[i * 2]); om_num(&b, pts[i * 2 + 1]); buf_adds(&b, i ? "l " : "m "); }
  buf_adds(&b, area ? "s\n" : "S\n");
  caption(&b, cx, cy, txt, 9);
  set_ap(d, a, &b, r, one_font("Helvetica"));
  buf_free(&b);
  return add_annot_obj(d, page, a) != 0;
}

/* ------------------------------------------------------------------ new documents */
PDoc *doc_new_blank(double w, double h, int pages)
{
  Buf b;
  long off[4];
  long xref;
  PDoc *d;
  char err[80];
  int i;
  buf_init(&b);
  buf_adds(&b, "%PDF-1.7\n");
  off[1] = (long)b.len; buf_adds(&b, "1 0 obj\n<</Type /Catalog /Pages 2 0 R>>\nendobj\n");
  off[2] = (long)b.len; buf_adds(&b, "2 0 obj\n<</Type /Pages /Kids [3 0 R] /Count 1>>\nendobj\n");
  off[3] = (long)b.len; buf_adds(&b, "3 0 obj\n<</Type /Page /Parent 2 0 R /MediaBox [0 0 ");
  om_num(&b, w); om_num(&b, h); buf_adds(&b, "] /Resources <<>>>>\nendobj\n");
  xref = (long)b.len;
  buf_adds(&b, "xref\n0 4\n0000000000 65535 f \n");
  for (i = 1; i < 4; i++) buf_printf(&b, "%010ld 00000 n \n", off[i]);
  buf_printf(&b, "trailer\n<</Size 4 /Root 1 0 R>>\nstartxref\n%ld\n%%%%EOF\n", xref);
  if (b.oom) { buf_free(&b); return NULL; }
  d = pdf_open((const u8 *)b.data, b.len, err, sizeof err);
  buf_free(&b);
  if (!d) return NULL;
  for (i = 1; i < pages; i++) pdf_insert_blank(d, d->npages, w, h);
  d->changed = 1;
  return d;
}
PDoc *doc_from_images(const u8 *const *files, const size_t *lens, int n, double w, double h)
{
  PDoc *d = NULL;
  int i, made = 0;
  for (i = 0; i < n; i++) {
    u32 iw, ih;
    double pw, ph, r[4];
    if (img_info(files[i], (u32)lens[i], &iw, &ih) == IMG_NONE || !iw || !ih) continue;
    pw = w > 0 ? w : iw; ph = h > 0 ? h : ih;
    if (w > 0 && h > 0 && (iw > ih) != (w > h)) { pw = h; ph = w; }   /* (the page turned to suit the picture) */
    if (!d) { if (!(d = doc_new_blank(pw, ph, 1))) return NULL; }
    else if (!pdf_insert_blank(d, d->npages, pw, ph)) break;
    r[0] = 0; r[1] = 0; r[2] = pw; r[3] = ph;
    if (page_add_image(d, d->npages - 1, r, files[i], lens[i])) made++;
  }
  if (d && !made) { pdf_close(d); return NULL; }
  return d;
}
PDoc *doc_from_text(const char *utf8, size_t n, double w, double h, double size, int font)
{
  PDoc *d;
  char *s, *z;
  int len, i = 0, page = 0;
  double margin = 54, lh, maxw;
  if (size <= 0) size = 10;
  lh = size * 1.25; maxw = w - 2 * margin;
  if (!(z = str_ndup(utf8 ? utf8 : "", n))) return NULL;
  s = latin(z, &len);
  free(z);
  if (!s || !(d = doc_new_blank(w, h, 1))) { free(s); return NULL; }
  for (;;) {
    Buf b;
    double y = h - margin - size;
    PObj *res = pc_resources(d, d->pages[page]);
    char nm[32];
    buf_init(&b);
    if (!res || !pc_add_resource(d, res, "Font", "FoTx", om_std_font(font == 3 ? "Courier" : fonts4[clampi(font, 0, 3)]), nm, sizeof nm)) { buf_free(&b); break; }
    buf_printf(&b, "BT /%s ", nm); om_num(&b, size); buf_adds(&b, "Tf\n");
    while (i < len && y >= margin) {
      int j, last = -1, end;
      for (j = i; j < len && s[j] != '\n'; j++) {
        if (s[j] == '\t') s[j] = ' ';
        if (s[j] == ' ') last = j;
        if (om_text_width(s + i, j - i + 1, size, font) > maxw && j > i) break;
      }
      end = j;
      if (j < len && s[j] != '\n' && last > i) end = last;
      if (end > i && s[end - 1] == '\r') end--;
      buf_adds(&b, "1 0 0 1 "); om_num(&b, margin); om_num(&b, y); buf_adds(&b, "Tm ");
      om_str(&b, s + i, end - i); buf_adds(&b, " Tj\n");
      i = end;
      if (i < len && s[i] == '\r') i++;
      if (i < len && (s[i] == '\n' || s[i] == ' ')) i++;
      y -= lh;
    }
    buf_adds(&b, "ET\n");
    if (!b.oom) pc_set_content(d, d->pages[page], (const u8 *)b.data, b.len);
    buf_free(&b);
    if (i >= len) break;
    if (!pdf_insert_blank(d, d->npages, w, h)) break;
    page = d->npages - 1;
  }
  free(s);
  return d;
}

/* ------------------------------------------------------------------ metadata */
static void xesc(Buf *b, const char *s)
{
  for (; s && *s; s++) {
    if (*s == '<') buf_adds(b, "&lt;"); else if (*s == '>') buf_adds(b, "&gt;"); else if (*s == '&') buf_adds(b, "&amp;"); else buf_addc(b, *s);
  }
}
/* a PDF date (D:YYYYMMDDHHmmSS...) as XMP's (YYYY-MM-DDTHH:mm:ss) */
static void xmp_date(const char *pd, char *out)
{
  const char *s = pd && !strncmp(pd, "D:", 2) ? pd + 2 : pd;
  int i;
  for (i = 0; s && i < 14 && s[i] >= '0' && s[i] <= '9'; i++) ;
  if (!s || i < 8) { out[0] = 0; return; }
  sprintf(out, "%.4s-%.2s-%.2s", s, s + 4, s + 6);
  if (i >= 14) sprintf(out + 10, "T%.2s:%.2s:%.2s", s + 8, s + 10, s + 12);
}
int xmp_update(PDoc *d)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *dict;
  char *t = pdf_info(d, "Title"), *a = pdf_info(d, "Author"), *su = pdf_info(d, "Subject"), *k = pdf_info(d, "Keywords");
  char *pr = pdf_info(d, "Producer"), *cd = pdf_info(d, "CreationDate"), *md = pdf_info(d, "ModDate"), *cr = pdf_info(d, "Creator");
  char dt[40];
  Buf b;
  int num, ok = 0;
  if (!root) goto done;
  buf_init(&b);
  buf_adds(&b, "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
               "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
               "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">\n");
  if (t) { buf_adds(&b, "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">"); xesc(&b, t); buf_adds(&b, "</rdf:li></rdf:Alt></dc:title>\n"); }
  if (a) { buf_adds(&b, "<dc:creator><rdf:Seq><rdf:li>"); xesc(&b, a); buf_adds(&b, "</rdf:li></rdf:Seq></dc:creator>\n"); }
  if (su) { buf_adds(&b, "<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">"); xesc(&b, su); buf_adds(&b, "</rdf:li></rdf:Alt></dc:description>\n"); }
  if (k) { buf_adds(&b, "<pdf:Keywords>"); xesc(&b, k); buf_adds(&b, "</pdf:Keywords>\n"); }
  if (pr) { buf_adds(&b, "<pdf:Producer>"); xesc(&b, pr); buf_adds(&b, "</pdf:Producer>\n"); }
  if (cr) { buf_adds(&b, "<xmp:CreatorTool>"); xesc(&b, cr); buf_adds(&b, "</xmp:CreatorTool>\n"); }
  xmp_date(cd, dt); if (dt[0]) buf_printf(&b, "<xmp:CreateDate>%s</xmp:CreateDate>\n", dt);
  xmp_date(md, dt); if (dt[0]) buf_printf(&b, "<xmp:ModifyDate>%s</xmp:ModifyDate>\n", dt);
  buf_adds(&b, "</rdf:Description>\n</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n");
  if (!b.oom && (dict = pobj_new(PO_DICT))) {
    pobj_dict_set(dict, "Type", pobj_name("Metadata"));
    pobj_dict_set(dict, "Subtype", pobj_name("XML"));
    if ((num = om_stream(d, dict, b.data, b.len))) { pobj_dict_set(root, "Metadata", om_ref(num)); d->changed = 1; ok = 1; }
  }
  buf_free(&b);
done:
  free(t); free(a); free(su); free(k); free(pr); free(cd); free(md); free(cr);
  return ok;
}
