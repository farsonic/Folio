/*
 * forms.c - interactive forms (see forms.h).
 *
 * The field tree is walked from the catalog's /AcroForm /Fields: a node with a /T whose kids have
 * none is a terminal field, its kids (or itself, when field and widget are merged) its widgets.
 * FT, Ff, V, DV, DA, Q, Opt and MaxLen are inherited down the tree. Appearances are made as the
 * values are set: text in the DA's size and colour, in Helvetica, Times or Courier by the DA
 * font's name (the standard fonts every viewer has, so any text can be drawn), check marks in
 * ZapfDingbats. Text goes out as WinAnsi (Latin-1): letters beyond it show as '?'.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "forms.h"
#include "pagecontent.h"
#include "xml.h"

/* ------------------------------------------------------------------ loading */
typedef struct { const char *ft; long ff; PObj *v, *dv, *da, *q, *opt, *maxlen; } Inh;
typedef struct { int num, page, index; } WPos;
typedef struct { PDoc *d; FForm *f; WPos *pos; int npos; } Ld;

static int cmp_wpos(const void *a, const void *b) { return ((const WPos *)a)->num - ((const WPos *)b)->num; }
static void widget_pos(Ld *l, PObj *w, int *page, int *index)
{
  int num = om_num_of(l->d, w), lo = 0, hi = l->npos - 1, p, i;
  *page = -1; *index = -1;
  if (num) while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (l->pos[m].num == num) { *page = l->pos[m].page; *index = l->pos[m].index; return; }
    if (l->pos[m].num < num) lo = m + 1; else hi = m - 1;
  }
  /* (a direct widget: looked for in the pages' lists) */
  for (p = 0; p < l->d->npages; p++) {
    PObj *a = pdf_dget(l->d, l->d->pages[p], "Annots");
    if (!a || a->type != PO_ARRAY) continue;
    for (i = 0; i < a->u.arr.n; i++) if (a->u.arr.v[i] == w) { *page = p; *index = i; return; }
  }
}
static char *text_of(PDoc *d, PObj *o)
{
  o = pdf_resolve(d, o);
  if (!o) return NULL;
  if (o->type == PO_STRING) return pdf_text_string(o);
  if (o->type == PO_NAME) return str_dup(o->u.str.s);
  if (o->type == PO_ARRAY && o->u.arr.n) return text_of(d, o->u.arr.v[0]);
  return NULL;
}
static int get_rect(PDoc *d, PObj *a, double r[4])
{
  int i;
  a = pdf_resolve(d, a);
  if (!a || a->type != PO_ARRAY || a->u.arr.n < 4) return 0;
  for (i = 0; i < 4; i++) r[i] = pobj_num(pdf_resolve(d, a->u.arr.v[i]));
  if (r[0] > r[2]) { double t = r[0]; r[0] = r[2]; r[2] = t; }
  if (r[1] > r[3]) { double t = r[1]; r[1] = r[3]; r[3] = t; }
  return 1;
}
/* a button widget's "on" state: the normal appearance's key that isn't Off */
static void on_state(PDoc *d, PObj *w, char *out, int cap)
{
  PObj *ap = pdf_dget(d, w, "AP"), *n = ap ? pdf_dget(d, ap, "N") : NULL;
  int i;
  out[0] = 0;
  if (n && n->type == PO_DICT) for (i = 0; i < n->u.dict.n; i++) if (strcmp(n->u.dict.k[i], "Off")) { strncpy(out, n->u.dict.k[i], (size_t)cap - 1); out[cap - 1] = 0; return; }
  {
    const char *as = pdf_dname(d, w, "AS");
    if (as && strcmp(as, "Off")) { strncpy(out, as, (size_t)cap - 1); out[cap - 1] = 0; return; }
  }
  strcpy(out, "Yes");
}
static void add_widget(Ld *l, FField *f, PObj *w)
{
  FWidget *nw = (FWidget *)realloc(f->w, (size_t)(f->nw + 1) * sizeof(FWidget)), *x;
  if (!nw) return;
  f->w = nw;
  x = &f->w[f->nw++];
  memset(x, 0, sizeof *x);
  x->dict = w;
  widget_pos(l, w, &x->page, &x->index);
  get_rect(l->d, pdf_dget(l->d, w, "Rect"), x->rect);
  if (f->type == FF_CHECK || f->type == FF_RADIO) on_state(l->d, w, x->on, sizeof x->on);
}
static void add_field(Ld *l, PObj *node, const char *name, const Inh *in)
{
  FForm *fm = l->f;
  FField *f, *nf;
  PObj *kids = pdf_dget(l->d, node, "Kids");
  int i;
  if (!(nf = (FField *)realloc(fm->f, (size_t)(fm->n + 1) * sizeof(FField)))) return;
  fm->f = nf;
  f = &fm->f[fm->n++];
  memset(f, 0, sizeof *f);
  f->dict = node;
  f->name = str_dup(name);
  f->tooltip = text_of(l->d, pdf_dget(l->d, node, "TU"));
  f->flags = in->ff;
  if (in->ft && !strcmp(in->ft, "Btn")) f->type = in->ff & FFL_PUSH ? FF_PUSH : in->ff & FFL_RADIO ? FF_RADIO : FF_CHECK;
  else if (in->ft && !strcmp(in->ft, "Ch")) f->type = in->ff & FFL_COMBO ? FF_COMBO : FF_LIST;
  else if (in->ft && !strcmp(in->ft, "Sig")) f->type = FF_SIG;
  else f->type = FF_TEXT;
  f->value = text_of(l->d, in->v);
  if ((f->type == FF_CHECK || f->type == FF_RADIO) && !f->value) f->value = str_dup("Off");
  f->maxlen = in->maxlen ? dbl_int(pobj_num(pdf_resolve(l->d, in->maxlen))) : 0;
  f->quad = in->q ? dbl_int(pobj_num(pdf_resolve(l->d, in->q))) : 0;
  if (in->opt) {
    PObj *o = pdf_resolve(l->d, in->opt);
    if (o && o->type == PO_ARRAY && o->u.arr.n > 0 && o->u.arr.n < 10000) {
      f->opts = (char **)calloc((size_t)o->u.arr.n, sizeof(char *));
      f->optx = (char **)calloc((size_t)o->u.arr.n, sizeof(char *));
      if (f->opts && f->optx) for (i = 0; i < o->u.arr.n; i++) {
        PObj *e = pdf_resolve(l->d, o->u.arr.v[i]);
        if (e && e->type == PO_ARRAY && e->u.arr.n >= 2) { f->optx[i] = text_of(l->d, e->u.arr.v[0]); f->opts[i] = text_of(l->d, e->u.arr.v[1]); }
        else { f->opts[i] = text_of(l->d, e); f->optx[i] = text_of(l->d, e); }
        if (!f->opts[i]) f->opts[i] = str_dup("");
        if (!f->optx[i]) f->optx[i] = str_dup("");
        f->nopts++;
      }
    }
  }
  if (kids && kids->type == PO_ARRAY) {
    for (i = 0; i < kids->u.arr.n; i++) { PObj *k = pdf_resolve(l->d, kids->u.arr.v[i]); if (k && k->type == PO_DICT) add_widget(l, f, k); }
  } else add_widget(l, f, node);
}
static void walk(Ld *l, PObj *node, const char *parent, Inh in, int depth)
{
  PObj *t, *kids, *v;
  char *name = NULL, *part;
  int i, has_t_kids = 0;
  if (!node || node->type != PO_DICT || depth > 32) return;
  if ((v = pdf_dget(l->d, node, "FT")) && v->type == PO_NAME) in.ft = v->u.str.s;
  if ((v = pdf_dget(l->d, node, "Ff"))) in.ff = dbl_long(pobj_num(v));
  if ((v = pobj_dict_get(node, "V"))) in.v = v;
  if ((v = pobj_dict_get(node, "DV"))) in.dv = v;
  if ((v = pobj_dict_get(node, "DA"))) in.da = v;
  if ((v = pobj_dict_get(node, "Q"))) in.q = v;
  if ((v = pobj_dict_get(node, "Opt"))) in.opt = v;
  if ((v = pobj_dict_get(node, "MaxLen"))) in.maxlen = v;
  t = pdf_dget(l->d, node, "T");
  part = t ? pdf_text_string(t) : NULL;
  if (part) {
    size_t n = strlen(parent) + strlen(part) + 2;
    if ((name = (char *)malloc(n))) snprintf(name, n, "%s%s%s", parent, *parent ? "." : "", part);
    free(part);
  } else name = str_dup(parent);
  if (!name) return;
  kids = pdf_dget(l->d, node, "Kids");
  if (kids && kids->type == PO_ARRAY)
    for (i = 0; i < kids->u.arr.n; i++) { PObj *k = pdf_resolve(l->d, kids->u.arr.v[i]); if (k && k->type == PO_DICT && pobj_dict_get(k, "T")) has_t_kids = 1; }
  if (has_t_kids) {
    for (i = 0; i < kids->u.arr.n; i++) walk(l, pdf_resolve(l->d, kids->u.arr.v[i]), name, in, depth + 1);
  } else add_field(l, node, name, &in);
  free(name);
}
static PObj *acroform(PDoc *d, int create)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *af;
  if (!root) return NULL;
  af = pdf_dget(d, root, "AcroForm");
  if (af && af->type == PO_DICT) return af;
  if (!create || !(af = pobj_new(PO_DICT))) return NULL;
  pobj_dict_set(af, "Fields", pobj_new(PO_ARRAY));
  pobj_dict_set(root, "AcroForm", af);
  return af;
}
int form_load(PDoc *d, FForm *f)
{
  Ld l;
  PObj *af = acroform(d, 0), *fields;
  int p, i, n = 0;
  memset(f, 0, sizeof *f);
  if (!af || !(fields = pdf_dget(d, af, "Fields")) || fields->type != PO_ARRAY) return 0;
  f->need_appearances = pdf_dget(d, af, "NeedAppearances") && pdf_dget(d, af, "NeedAppearances")->type == PO_BOOL && pdf_dget(d, af, "NeedAppearances")->u.b;
  memset(&l, 0, sizeof l);
  l.d = d; l.f = f;
  for (p = 0; p < d->npages; p++) { PObj *a = pdf_dget(d, d->pages[p], "Annots"); if (a && a->type == PO_ARRAY) n += a->u.arr.n; }
  if (n && (l.pos = (WPos *)malloc((size_t)n * sizeof(WPos)))) {
    for (p = 0; p < d->npages; p++) {
      PObj *a = pdf_dget(d, d->pages[p], "Annots");
      if (!a || a->type != PO_ARRAY) continue;
      for (i = 0; i < a->u.arr.n; i++) if (a->u.arr.v[i]->type == PO_REF) { l.pos[l.npos].num = a->u.arr.v[i]->u.ref.num; l.pos[l.npos].page = p; l.pos[l.npos].index = i; l.npos++; }
    }
    qsort(l.pos, (size_t)l.npos, sizeof(WPos), cmp_wpos);
  }
  {
    Inh in;
    memset(&in, 0, sizeof in);
    in.da = pobj_dict_get(af, "DA");
    for (i = 0; i < fields->u.arr.n; i++) walk(&l, pdf_resolve(d, fields->u.arr.v[i]), "", in, 0);
  }
  free(l.pos);
  return f->n;
}
void form_free(FForm *f)
{
  int i, k;
  for (i = 0; i < f->n; i++) {
    free(f->f[i].name); free(f->f[i].tooltip); free(f->f[i].value);
    for (k = 0; k < f->f[i].nopts; k++) { free(f->f[i].opts[k]); free(f->f[i].optx[k]); }
    free(f->f[i].opts); free(f->f[i].optx); free(f->f[i].w);
  }
  free(f->f);
  memset(f, 0, sizeof *f);
}
int form_field_at(FForm *f, int page, double x, double y, int *widget)
{
  int i, k, best = -1;
  for (i = 0; i < f->n; i++) for (k = 0; k < f->f[i].nw; k++) {
    FWidget *w = &f->f[i].w[k];
    if (w->page == page && x >= w->rect[0] && x <= w->rect[2] && y >= w->rect[1] && y <= w->rect[3]) { best = i; if (widget) *widget = k; }
  }
  return best;
}

/* ------------------------------------------------------------------ appearances */
/* an inherited attribute of a field node (or of its widget) */
static PObj *inherited(PDoc *d, PObj *node, const char *key)
{
  int n = 0;
  while (node && n++ < 32) {
    PObj *v = pdf_dget(d, node, key);
    if (v) return v;
    node = pdf_dget(d, node, "Parent");
  }
  return NULL;
}
typedef struct { double size; double col[4]; int ncol; int font; char fname[32]; } DA;
static void parse_da(PDoc *d, FField *f, PObj *w, DA *da)
{
  PObj *s = inherited(d, w, "DA");
  Lex lx;
  double nums[8];
  int nn = 0;
  char last_name[32] = "";
  memset(da, 0, sizeof *da);
  da->ncol = 1;
  strcpy(da->fname, "Helv");
  if (!s) { PObj *af = acroform(d, 0); s = af ? pdf_dget(d, af, "DA") : NULL; }
  if (!s || s->type != PO_STRING) return;
  lex_init(&lx, (const u8 *)s->u.str.s, (size_t)s->u.str.len, d);
  for (;;) {
    Token t;
    int ty = lex_next(&lx, &t);
    if (ty == TK_EOF || ty == TK_ERROR) { tok_free(&t); break; }
    if (ty == TK_INT || ty == TK_REAL) { if (nn < 8) nums[nn++] = ty == TK_INT ? (double)t.i : t.r; }
    else if (ty == TK_NAME) { strncpy(last_name, t.s ? t.s : "", sizeof last_name - 1); last_name[sizeof last_name - 1] = 0; nn = 0; }
    else if (ty == TK_KEYWORD && t.s) {
      if (!strcmp(t.s, "Tf") && nn >= 1) { da->size = nums[nn - 1]; if (last_name[0]) strcpy(da->fname, last_name); }
      else if (!strcmp(t.s, "g") && nn >= 1) { da->ncol = 1; da->col[0] = nums[nn - 1]; }
      else if (!strcmp(t.s, "rg") && nn >= 3) { da->ncol = 3; memcpy(da->col, nums + nn - 3, 3 * sizeof(double)); }
      else if (!strcmp(t.s, "k") && nn >= 4) { da->ncol = 4; memcpy(da->col, nums + nn - 4, 4 * sizeof(double)); }
      nn = 0;
    }
    tok_free(&t);
  }
  /* the standard font that stands for the DA's: by the DR font's base name (or the resource name) */
  {
    PObj *af = acroform(d, 0), *dr = af ? pdf_dget(d, af, "DR") : NULL, *fonts = dr ? pdf_dget(d, dr, "Font") : NULL;
    PObj *fd = fonts ? pdf_dget(d, fonts, da->fname) : NULL;
    const char *base = fd ? pdf_dname(d, fd, "BaseFont") : NULL;
    const char *nm = base ? base : da->fname;
    if (strstr(nm, "Cour") || strstr(nm, "Mono")) da->font = 3;
    else if (strstr(nm, "Times") || strstr(nm, "Serif") || strstr(nm, "TiRo")) da->font = 2;
    else if (strstr(nm, "Bold") || strstr(nm, "HeBo")) da->font = 1;
    else da->font = 0;
  }
}
static const char *const std_names[4] = { "Helvetica", "Helvetica-Bold", "Times-Roman", "Courier" };
static void put_color(Buf *b, const double *c, int n, int stroke)
{
  int i;
  for (i = 0; i < n; i++) om_num(b, c[i]);
  buf_adds(b, n == 1 ? (stroke ? "G\n" : "g\n") : n == 3 ? (stroke ? "RG\n" : "rg\n") : (stroke ? "K\n" : "k\n"));
}
/* the MK colours: background and border, n 0 when absent */
static int mk_color(PDoc *d, PObj *w, const char *key, double *c)
{
  PObj *mk = pdf_dget(d, w, "MK"), *a = mk ? pdf_dget(d, mk, key) : NULL;
  int i, n;
  if (!a || a->type != PO_ARRAY || (a->u.arr.n != 1 && a->u.arr.n != 3 && a->u.arr.n != 4)) return 0;
  n = a->u.arr.n;
  for (i = 0; i < n; i++) c[i] = pobj_num(pdf_resolve(d, a->u.arr.v[i]));
  return n;
}
/* the widget's box size and its rotation (MK /R) */
static void widget_box(PDoc *d, PObj *w, double *bw, double *bh, int *rot)
{
  double r[4];
  PObj *mk = pdf_dget(d, w, "MK");
  if (!get_rect(d, pdf_dget(d, w, "Rect"), r)) { r[0] = r[1] = 0; r[2] = 100; r[3] = 20; }
  *rot = mk ? ((int)pdf_dint(d, mk, "R", 0) % 360 + 360) % 360 : 0;
  *bw = r[2] - r[0]; *bh = r[3] - r[1];
  if (*rot == 90 || *rot == 270) { double t = *bw; *bw = *bh; *bh = t; }
}
/* the box's background and border drawn */
static double put_frame(PDoc *d, PObj *w, Buf *b, double bw, double bh)
{
  double bg[4], bc[4], lw = 1;
  int nbg = mk_color(d, w, "BG", bg), nbc = mk_color(d, w, "BC", bc);
  PObj *bs = pdf_dget(d, w, "BS");
  if (bs) lw = pdf_dnum(d, bs, "W", 1);
  if (nbg) { put_color(b, bg, nbg, 0); buf_adds(b, "0 0 "); om_num(b, bw); om_num(b, bh); buf_adds(b, "re f\n"); }
  if (nbc && lw > 0) {
    put_color(b, bc, nbc, 1);
    om_num(b, lw); buf_adds(b, "w ");
    om_num(b, lw / 2); om_num(b, lw / 2); om_num(b, bw - lw); om_num(b, bh - lw); buf_adds(b, "re S\n");
    return lw;
  }
  return nbc ? 0 : 0;
}
/* the appearance form set as the widget's normal appearance (state: under that name, with an Off) */
static int set_ap(PDoc *d, PObj *w, Buf *b, double bw, double bh, int rot, PObj *res, const char *state, Buf *off)
{
  double bb[4];
  int num, offn = 0;
  PObj *ap, *n;
  bb[0] = bb[1] = 0; bb[2] = bw; bb[3] = bh;
  if (b->oom) { pobj_free(res); return 0; }
  if (state && off) {
    PObj *r2 = res ? pobj_copy(res) : NULL;
    offn = om_form(d, off->data ? off->data : "", off->len, bb, r2);
  }
  if (!(num = om_form(d, b->data ? b->data : "", b->len, bb, res))) return 0;
  if (rot) {
    PObj *f = pdf_get(d, num);
    double m[6], c = rot == 90 ? 0 : rot == 180 ? -1 : 0, s = rot == 90 ? 1 : rot == 270 ? -1 : 0;
    if (rot == 180) s = 0;
    m[0] = c; m[1] = s; m[2] = -s; m[3] = c; m[4] = 0; m[5] = 0;
    if (f) pobj_dict_set(f, "Matrix", om_arr(m, 6));
    if (offn && (f = pdf_get(d, offn))) pobj_dict_set(f, "Matrix", om_arr(m, 6));
  }
  if (!(ap = pobj_new(PO_DICT))) return 0;
  if (state) {
    if (!(n = pobj_new(PO_DICT))) { pobj_free(ap); return 0; }
    pobj_dict_set(n, state, om_ref(num));
    if (offn) pobj_dict_set(n, "Off", om_ref(offn));
    pobj_dict_set(ap, "N", n);
  } else pobj_dict_set(ap, "N", om_ref(num));
  pobj_dict_set(w, "AP", ap);
  d->changed = 1;
  return 1;
}
static PObj *font_res(int font)
{
  PObj *res = pobj_new(PO_DICT), *fonts = pobj_new(PO_DICT);
  if (!res || !fonts) { pobj_free(res); pobj_free(fonts); return NULL; }
  pobj_dict_set(fonts, "FoF", om_std_font(font < 0 ? "ZapfDingbats" : std_names[font]));
  pobj_dict_set(res, "Font", fonts);
  return res;
}
static char *latin(const char *utf8, int *n)
{
  size_t l = utf8 ? strlen(utf8) : 0;
  char *s = (char *)malloc(l * 3 + 4);
  if (!s) { *n = 0; return NULL; }
  *n = (int)utf8_to_latin1(utf8 ? utf8 : "", l, s, l * 3 + 4);
  return s;
}
/* a text field's (or a combo box's) appearance for one widget */
static int text_ap(PDoc *d, FField *f, PObj *w, const char *val)
{
  DA da;
  double bw, bh, size, pad = 2, lw;
  int rot, n, i;
  char *s = latin(val, &n);
  Buf b;
  if (!s) return 0;
  if (f->flags & FFL_PASSWORD) for (i = 0; i < n; i++) s[i] = '*';
  parse_da(d, f, w, &da);
  widget_box(d, w, &bw, &bh, &rot);
  buf_init(&b);
  lw = put_frame(d, w, &b, bw, bh);
  if (lw > 0) pad = lw * 2;
  buf_adds(&b, "/Tx BMC\nq\n");
  om_num(&b, pad / 2); om_num(&b, pad / 2); om_num(&b, bw - pad); om_num(&b, bh - pad); buf_adds(&b, "re W n\n");
  size = da.size;
  if ((f->flags & FFL_MULTILINE) && f->type == FF_TEXT) {
    double y, maxw = bw - 2 * pad;
    int start = 0;
    if (size <= 0) {
      /* the largest size (12 down to 4) at which the text fits */
      for (size = 12; size > 4; size -= 0.5) {
        int lines = 0; start = 0;
        while (start < n) {
          int j, last = -1;
          for (j = start; j < n && s[j] != '\n'; j++) { if (s[j] == ' ') last = j; if (om_text_width(s + start, j - start + 1, size, da.font) > maxw && j > start) break; }
          if (j < n && s[j] != '\n' && last > start) j = last;
          start = j + (j < n);
          lines++;
        }
        if (lines * size * 1.15 <= bh - 2 * pad) break;
      }
      start = 0;
    }
    buf_adds(&b, "BT\n/FoF "); om_num(&b, size); buf_adds(&b, "Tf\n");
    put_color(&b, da.col, da.ncol, 0);
    y = bh - pad - size * 0.9;
    while (start < n && y > -size) {
      int j, last = -1, end;
      double x, lwid;
      for (j = start; j < n && s[j] != '\n'; j++) { if (s[j] == ' ') last = j; if (om_text_width(s + start, j - start + 1, size, da.font) > maxw && j > start) break; }
      end = j;
      if (j < n && s[j] != '\n' && last > start) end = last;
      lwid = om_text_width(s + start, end - start, size, da.font);
      x = f->quad == 1 ? (bw - lwid) / 2 : f->quad == 2 ? bw - pad - lwid : pad;
      buf_adds(&b, "1 0 0 1 "); om_num(&b, x); om_num(&b, y); buf_adds(&b, "Tm ");
      om_str(&b, s + start, end - start); buf_adds(&b, " Tj\n");
      start = end;
      if (start < n && (s[start] == ' ' || s[start] == '\n')) start++;
      y -= size * 1.15;
    }
    buf_adds(&b, "ET\n");
  } else {
    double tw, x, y;
    if (size <= 0) {
      size = (bh - 2 * pad) / 1.15;
      if (size > 12) size = 12;
      if (n) { tw = om_text_width(s, n, size, da.font); if (tw > bw - 2 * pad && tw > 0) size *= (bw - 2 * pad) / tw; }
      if (size < 4) size = 4;
    }
    y = (bh - size * 0.7) / 2;
    buf_adds(&b, "BT\n/FoF "); om_num(&b, size); buf_adds(&b, "Tf\n");
    put_color(&b, da.col, da.ncol, 0);
    if ((f->flags & FFL_COMB) && f->maxlen > 0) {
      double cell = bw / f->maxlen;
      for (i = 0; i < n && i < f->maxlen; i++) {
        x = cell * i + (cell - om_text_width(s + i, 1, size, da.font)) / 2;
        buf_adds(&b, "1 0 0 1 "); om_num(&b, x); om_num(&b, y); buf_adds(&b, "Tm ");
        om_str(&b, s + i, 1); buf_adds(&b, " Tj\n");
      }
    } else {
      tw = om_text_width(s, n, size, da.font);
      x = f->quad == 1 ? (bw - tw) / 2 : f->quad == 2 ? bw - pad - tw : pad;
      buf_adds(&b, "1 0 0 1 "); om_num(&b, x); om_num(&b, y); buf_adds(&b, "Tm ");
      om_str(&b, s, n); buf_adds(&b, " Tj\n");
    }
    buf_adds(&b, "ET\n");
  }
  buf_adds(&b, "Q\nEMC\n");
  free(s);
  i = set_ap(d, w, &b, bw, bh, rot, font_res(da.font), NULL, NULL);
  buf_free(&b);
  return i;
}
/* a list box's appearance: the options, the chosen one on a highlight */
static int list_ap(PDoc *d, FField *f, PObj *w, int chosen)
{
  DA da;
  double bw, bh, size, y, pad = 2;
  int rot, i, top = (int)pdf_dint(d, f->dict, "TI", 0), r;
  Buf b;
  parse_da(d, f, w, &da);
  widget_box(d, w, &bw, &bh, &rot);
  size = da.size > 0 ? da.size : 10;
  buf_init(&b);
  put_frame(d, w, &b, bw, bh);
  buf_adds(&b, "/Tx BMC\nq\n");
  om_num(&b, 1); om_num(&b, 1); om_num(&b, bw - 2); om_num(&b, bh - 2); buf_adds(&b, "re W n\n");
  if (top < 0 || top >= f->nopts) top = 0;
  if (chosen >= 0 && chosen < top) top = chosen;
  y = bh - pad;
  for (i = top; i < f->nopts && y > 0; i++) {
    int n;
    char *s = latin(f->opts[i], &n);
    if (i == chosen) { buf_adds(&b, "0.6 0.75 0.86 rg\n1 "); om_num(&b, y - size * 1.15); om_num(&b, bw - 2); om_num(&b, size * 1.15); buf_adds(&b, "re f\n"); }
    buf_adds(&b, "BT\n/FoF "); om_num(&b, size); buf_adds(&b, "Tf\n");
    put_color(&b, da.col, da.ncol, 0);
    buf_adds(&b, "1 0 0 1 "); om_num(&b, pad); om_num(&b, y - size * 0.9); buf_adds(&b, "Tm ");
    if (s) om_str(&b, s, n);
    else buf_adds(&b, "()");
    buf_adds(&b, " Tj\nET\n");
    free(s);
    y -= size * 1.15;
  }
  buf_adds(&b, "Q\nEMC\n");
  r = set_ap(d, w, &b, bw, bh, rot, font_res(da.font), NULL, NULL);
  buf_free(&b);
  return r;
}
/* a check box's or radio button's appearance, both states, unless it has its own already */
static int button_ap(PDoc *d, FField *f, FWidget *fw)
{
  PObj *ap = pdf_dget(d, fw->dict, "AP"), *n = ap ? pdf_dget(d, ap, "N") : NULL, *mk, *ca;
  double bw, bh, size;
  int rot, r;
  char ch = f->type == FF_RADIO ? 'l' : '4';
  Buf on, off;
  DA da;
  if (n && n->type == PO_DICT && pobj_dict_get(n, fw->on)) return 1;
  mk = pdf_dget(d, fw->dict, "MK");
  ca = mk ? pdf_dget(d, mk, "CA") : NULL;
  if (ca && ca->type == PO_STRING && ca->u.str.len) ch = ca->u.str.s[0];
  parse_da(d, f, fw->dict, &da);
  widget_box(d, fw->dict, &bw, &bh, &rot);
  size = (bw < bh ? bw : bh) * 0.8;
  buf_init(&on); buf_init(&off);
  put_frame(d, fw->dict, &on, bw, bh);
  put_frame(d, fw->dict, &off, bw, bh);
  buf_adds(&on, "q BT\n/FoF "); om_num(&on, size); buf_adds(&on, "Tf\n");
  put_color(&on, da.col, da.ncol, 0);
  /* (the ZapfDingbats marks are about 0.75 to 0.9 em wide; centred roughly) */
  om_num(&on, (bw - size * 0.8) / 2); om_num(&on, (bh - size * 0.7) / 2); buf_adds(&on, "Td ");
  om_str(&on, &ch, 1); buf_adds(&on, " Tj\nET Q\n");
  if (!fw->on[0]) strcpy(fw->on, "Yes");
  r = set_ap(d, fw->dict, &on, bw, bh, rot, font_res(-1), fw->on, &off);
  buf_free(&on); buf_free(&off);
  return r;
}
/* the index of the option whose export value (or shown text) is val, -1 none */
static int option_of(FField *f, const char *val)
{
  int i;
  if (!val) return -1;
  for (i = 0; i < f->nopts; i++) if (!strcmp(f->optx[i], val)) return i;
  for (i = 0; i < f->nopts; i++) if (!strcmp(f->opts[i], val)) return i;
  return -1;
}
static int field_ap(PDoc *d, FField *f)
{
  int k, ok = 1;
  for (k = 0; k < f->nw; k++) {
    FWidget *w = &f->w[k];
    switch (f->type) {
    case FF_TEXT: ok &= text_ap(d, f, w->dict, f->value ? f->value : ""); break;
    case FF_COMBO: {
      int o = option_of(f, f->value);
      ok &= text_ap(d, f, w->dict, o >= 0 ? f->opts[o] : f->value ? f->value : "");
      break; }
    case FF_LIST: ok &= list_ap(d, f, w->dict, option_of(f, f->value)); break;
    case FF_CHECK: case FF_RADIO:
      ok &= button_ap(d, f, w);
      pobj_dict_set(w->dict, "AS", pobj_name(f->value && !strcmp(f->value, w->on) ? w->on : "Off"));
      break;
    }
  }
  return ok;
}
static void no_need_appearances(PDoc *d)
{
  PObj *af = acroform(d, 0);
  if (af && pobj_dict_get(af, "NeedAppearances")) pobj_dict_del(af, "NeedAppearances");
}

/* ------------------------------------------------------------------ filling in */
static void set_value(FField *f, const char *v)
{
  char *c = str_dup(v ? v : "");
  if (!c) return;
  free(f->value);
  f->value = c;
}
int form_set_text(PDoc *d, FField *f, const char *utf8)
{
  if (f->flags & FFL_READONLY) return 0;
  if (f->type == FF_COMBO || f->type == FF_LIST) {
    int o = option_of(f, utf8);
    if (o >= 0 || f->type == FF_COMBO) {
      pobj_dict_set(f->dict, "V", om_text(o >= 0 ? f->optx[o] : utf8));
      set_value(f, o >= 0 ? f->optx[o] : utf8);
      if (f->type == FF_LIST) { PObj *ia = pobj_new(PO_ARRAY); if (ia && o >= 0) pobj_array_add(ia, pobj_int(o)); pobj_dict_set(f->dict, "I", ia); }
      d->changed = 1;
      return field_ap(d, f);
    }
    return 0;
  }
  if (f->type != FF_TEXT) return 0;
  {
    char *v = str_dup(utf8 ? utf8 : "");
    if (!v) return 0;
    /* MaxLen: characters, not bytes */
    if (f->maxlen > 0) {
      size_t off = 0, len = strlen(v);
      int c = 0;
      while (off < len && c < f->maxlen) { off = utf8_next(v, len, off); c++; }
      v[off] = 0;
    }
    pobj_dict_set(f->dict, "V", om_text(v));
    set_value(f, v);
    free(v);
  }
  d->changed = 1;
  no_need_appearances(d);
  return field_ap(d, f);
}
int form_set_check(PDoc *d, FField *f, int widget, int on)
{
  const char *st;
  if (f->flags & FFL_READONLY) return 0;
  if (f->type != FF_CHECK && f->type != FF_RADIO) return 0;
  if (widget < 0 || widget >= f->nw) widget = 0;
  if (!f->nw) return 0;
  if (!f->w[widget].on[0]) strcpy(f->w[widget].on, "Yes");
  st = on ? f->w[widget].on : "Off";
  if (f->type == FF_RADIO && !on && (f->flags & FFL_NOTOGGLE) && f->value && !strcmp(f->value, f->w[widget].on)) return 1;
  pobj_dict_set(f->dict, "V", pobj_name(st));
  set_value(f, st);
  d->changed = 1;
  return field_ap(d, f);
}
int form_set_choice(PDoc *d, FField *f, int option)
{
  if (f->type != FF_COMBO && f->type != FF_LIST) return 0;
  if (f->flags & FFL_READONLY) return 0;
  if (option < 0 || option >= f->nopts) {
    pobj_dict_del(f->dict, "V"); pobj_dict_del(f->dict, "I");
    set_value(f, "");
    d->changed = 1;
    return field_ap(d, f);
  }
  return form_set_text(d, f, f->optx[option]);
}
int form_reset(PDoc *d, FForm *fm)
{
  int i, n = 0;
  for (i = 0; i < fm->n; i++) {
    FField *f = &fm->f[i];
    PObj *dv = inherited(d, f->dict, "DV");
    char *v = text_of(d, dv);
    if (f->type == FF_PUSH || f->type == FF_SIG) { free(v); continue; }
    if (dv) pobj_dict_set(f->dict, "V", pobj_copy(dv)); else pobj_dict_del(f->dict, "V");
    pobj_dict_del(f->dict, "I");
    set_value(f, v ? v : (f->type == FF_CHECK || f->type == FF_RADIO) ? "Off" : "");
    free(v);
    field_ap(d, f);
    n++;
  }
  if (n) { d->changed = 1; no_need_appearances(d); }
  return n;
}
int form_refresh(PDoc *d, FForm *fm, int all)
{
  int i, k, n = 0;
  for (i = 0; i < fm->n; i++) {
    FField *f = &fm->f[i];
    int missing = 0;
    if (f->type == FF_PUSH || f->type == FF_SIG) continue;
    for (k = 0; k < f->nw; k++) if (!pdf_dget(d, f->w[k].dict, "AP")) missing = 1;
    if (all || missing) { field_ap(d, f); n++; }
  }
  if (n) no_need_appearances(d);
  return n;
}

/* ------------------------------------------------------------------ flattening */
static void array_remove(PObj *arr, int i)
{
  pobj_free(arr->u.arr.v[i]);
  memmove(arr->u.arr.v + i, arr->u.arr.v + i + 1, (size_t)(arr->u.arr.n - i - 1) * sizeof(PObj *));
  arr->u.arr.n--;
}
int form_flatten(PDoc *d, FForm *fm)
{
  int p, i, n = 0;
  PObj *root = pdf_dget(d, d->trailer, "Root");
  if (fm->need_appearances) form_refresh(d, fm, 1);
  for (p = 0; p < d->npages; p++) {
    PObj *pg = d->pages[p], *a = pdf_dget(d, pg, "Annots"), *res;
    Buf ops;
    if (!a || a->type != PO_ARRAY) continue;
    if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *c = pobj_copy(a); if (!c) continue; pobj_dict_set(pg, "Annots", c); a = c; }
    if (!(res = pc_resources(d, pg))) continue;
    buf_init(&ops);
    for (i = 0; i < a->u.arr.n; ) {
      PObj *an = pdf_resolve(d, a->u.arr.v[i]);
      if (an && an->type == PO_DICT && pobj_is_name(pdf_dget(d, an, "Subtype"), "Widget")) {
        if (pc_flatten_annot(d, p, an, &ops, res)) n++;
        array_remove(a, i);
      } else i++;
    }
    if (ops.len) pc_add_content(d, p, ops.data, ops.len, 0, "flatten");
    buf_free(&ops);
  }
  if (root) pobj_dict_del(root, "AcroForm");
  d->changed = 1;
  return n;
}

/* ------------------------------------------------------------------ export and import */
static void xml_esc(Buf *b, const char *s)
{
  for (; s && *s; s++) {
    if (*s == '<') buf_adds(b, "&lt;"); else if (*s == '>') buf_adds(b, "&gt;"); else if (*s == '&') buf_adds(b, "&amp;");
    else if (*s == '"') buf_adds(b, "&quot;"); else buf_addc(b, *s);
  }
}
/* the number of dot-separated parts a and b share */
static int common_parts(const char *a, const char *b)
{
  int n = 0;
  for (;;) {
    const char *ea = strchr(a, '.'), *eb = strchr(b, '.');
    size_t la = ea ? (size_t)(ea - a) : strlen(a), lb = eb ? (size_t)(eb - b) : strlen(b);
    if (!ea || !eb || la != lb || memcmp(a, b, la)) return n;
    n++; a = ea + 1; b = eb + 1;
  }
}
static int parts(const char *s) { int n = 1; for (; *s; s++) if (*s == '.') n++; return n; }
static const char *part_at(const char *s, int k, size_t *len)
{
  const char *e;
  while (k-- > 0) { e = strchr(s, '.'); if (!e) break; s = e + 1; }
  e = strchr(s, '.');
  *len = e ? (size_t)(e - s) : strlen(s);
  return s;
}
static void pdf_lit(Buf *b, const char *utf8)
{
  PObj *t = om_text(utf8);
  int i;
  if (!t) return;
  buf_addc(b, '(');
  for (i = 0; i < t->u.str.len; i++) {
    u8 c = (u8)t->u.str.s[i];
    if (c == '(' || c == ')' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
    else if (c < 32 || c >= 127) buf_printf(b, "\\%03o", c);
    else buf_addc(b, c);
  }
  buf_addc(b, ')');
  pobj_free(t);
}
char *form_export(PDoc *d, FForm *f, int fdf, const char *pdfname, size_t *len)
{
  Buf b;
  int i, open = 0, k;
  const char *prev = "";
  buf_init(&b);
  if (fdf) {
    buf_adds(&b, "%FDF-1.2\n%\xE2\xE3\xCF\xD3\n1 0 obj\n<</FDF <<");
    if (pdfname) { buf_adds(&b, "/F "); pdf_lit(&b, pdfname); buf_addc(&b, ' '); }
    buf_adds(&b, "/Fields [\n");
    for (i = 0; i < f->n; i++) {
      FField *x = &f->f[i];
      if (x->type == FF_PUSH || x->type == FF_SIG || (x->flags & FFL_NOEXPORT)) continue;
      buf_adds(&b, "<</T "); pdf_lit(&b, x->name); buf_adds(&b, " /V ");
      if (x->type == FF_CHECK || x->type == FF_RADIO) {
        PObj *nm = pobj_name(x->value ? x->value : "Off");
        Buf t; buf_init(&t);
        buf_addc(&b, '/');
        for (k = 0; nm && k < nm->u.str.len; k++) { u8 c = (u8)nm->u.str.s[k]; if (c > 32 && c < 127 && !strchr("()<>[]{}/%#", c)) buf_addc(&b, c); else buf_printf(&b, "#%02X", c); }
        pobj_free(nm); buf_free(&t);
      } else pdf_lit(&b, x->value ? x->value : "");
      buf_adds(&b, ">>\n");
    }
    buf_adds(&b, "]>>>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n");
  } else {
    buf_adds(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xfdf xmlns=\"http://ns.adobe.com/xfdf/\" xml:space=\"preserve\">\n");
    if (pdfname) { buf_adds(&b, "<f href=\""); xml_esc(&b, pdfname); buf_adds(&b, "\"/>\n"); }
    buf_adds(&b, "<fields>\n");
    for (i = 0; i < f->n; i++) {
      FField *x = &f->f[i];
      int np, c;
      if (x->type == FF_PUSH || x->type == FF_SIG || (x->flags & FFL_NOEXPORT)) continue;
      np = parts(x->name);
      c = *prev ? common_parts(prev, x->name) : 0;
      if (c > np - 1) c = np - 1;
      while (open > c) { buf_adds(&b, "</field>\n"); open--; }
      while (open < np - 1) {
        size_t l;
        const char *s = part_at(x->name, open, &l);
        char *t = str_ndup(s, l);
        buf_adds(&b, "<field name=\""); xml_esc(&b, t); buf_adds(&b, "\">\n");
        free(t); open++;
      }
      {
        size_t l;
        const char *s = part_at(x->name, np - 1, &l);
        char *t = str_ndup(s, l);
        buf_adds(&b, "<field name=\""); xml_esc(&b, t); buf_adds(&b, "\"><value>");
        xml_esc(&b, x->value ? x->value : ""); buf_adds(&b, "</value></field>\n");
        free(t);
      }
      prev = x->name;
    }
    while (open-- > 0) buf_adds(&b, "</field>\n");
    buf_adds(&b, "</fields>\n</xfdf>\n");
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
static int set_by_name(PDoc *d, FForm *f, const char *name, const char *val)
{
  int i;
  for (i = 0; i < f->n; i++) if (!strcmp(f->f[i].name, name)) {
    FField *x = &f->f[i];
    if (x->type == FF_CHECK || x->type == FF_RADIO) {
      int k, on = val && strcmp(val, "Off") && *val;
      for (k = 0; k < x->nw; k++) if (val && !strcmp(x->w[k].on, val)) return form_set_check(d, x, k, 1);
      return form_set_check(d, x, 0, on && x->type == FF_CHECK);
    }
    return form_set_text(d, x, val);
  }
  return 0;
}
/* FDF: its /Fields, nested by /Kids */
static int fdf_fields(PDoc *d, FForm *f, PObj *arr, const char *parent, int depth)
{
  int i, n = 0;
  if (!arr || arr->type != PO_ARRAY || depth > 16) return 0;
  for (i = 0; i < arr->u.arr.n; i++) {
    PObj *e = arr->u.arr.v[i], *t, *v, *k;
    char *name, *part, *val;
    size_t l;
    if (!e || e->type != PO_DICT) continue;
    t = pobj_dict_get(e, "T");
    part = t ? pdf_text_string(t) : str_dup("");
    if (!part) continue;
    l = strlen(parent) + strlen(part) + 2;
    if (!(name = (char *)malloc(l))) { free(part); continue; }
    snprintf(name, l, "%s%s%s", parent, *parent && *part ? "." : "", part);
    free(part);
    v = pobj_dict_get(e, "V");
    k = pobj_dict_get(e, "Kids");
    if (v) { val = text_of(d, v); n += set_by_name(d, f, name, val ? val : ""); free(val); }
    if (k) n += fdf_fields(d, f, k, name, depth + 1);
    free(name);
  }
  return n;
}
int form_import(PDoc *d, FForm *f, const u8 *data, size_t len)
{
  size_t i;
  if (len >= 4 && !memcmp(data, "%FDF", 4)) {
    /* the /Fields array, read with the lexer */
    for (i = 0; i + 7 < len; i++) if (!memcmp(data + i, "/Fields", 7)) {
      Lex l;
      char *kw = NULL;
      PObj *arr;
      int n;
      lex_init(&l, data + i + 7, len - i - 7, NULL);
      arr = lex_object(&l, &kw);
      free(kw);
      n = fdf_fields(d, f, arr, "", 0);
      pobj_free(arr);
      return n;
    }
    return 0;
  }
  {
    Xml x;
    char *names[32];
    int depth = 0, in_value = 0, n = 0, k, is_xfdf = 0;
    Buf val;
    xml_init(&x, (const char *)data, len);
    buf_init(&val);
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_START) {
        if (!strcmp(x.name, "xfdf")) is_xfdf = 1;
        if (!strcmp(x.name, "field") && depth < 32) {
          const char *nm = "";
          for (k = 0; k < x.nattr; k++) if (!strcmp(x.an[k], "name")) nm = x.av[k];
          names[depth++] = str_dup(nm);
        } else if (!strcmp(x.name, "value")) { in_value = 1; val.len = 0; }
      } else if (x.type == XT_TEXT && in_value) buf_add(&val, x.text.data, x.text.len);
      else if (x.type == XT_END) {
        if (!strcmp(x.name, "value") && in_value) {
          Buf full;
          in_value = 0;
          buf_init(&full);
          for (k = 0; k < depth; k++) { if (k) buf_addc(&full, '.'); buf_adds(&full, names[k] ? names[k] : ""); }
          buf_addc(&val, 0);
          if (full.data) n += set_by_name(d, f, full.data, val.data);
          buf_free(&full);
        } else if (!strcmp(x.name, "field") && depth > 0) free(names[--depth]);
      }
    }
    while (depth > 0) free(names[--depth]);
    buf_free(&val);
    xml_free(&x);
    return is_xfdf ? n : -1;
  }
}

/* ------------------------------------------------------------------ preparing a form */
static PObj *dr_fonts(PDoc *d, PObj *af)
{
  PObj *dr = pdf_dget(d, af, "DR"), *fonts;
  if (!dr) { if (!(dr = pobj_new(PO_DICT))) return NULL; pobj_dict_set(af, "DR", dr); }
  fonts = pdf_dget(d, dr, "Font");
  if (!fonts) { if (!(fonts = pobj_new(PO_DICT))) return NULL; pobj_dict_set(dr, "Font", fonts); }
  if (!pobj_dict_get(fonts, "Helv")) pobj_dict_set(fonts, "Helv", om_std_font("Helvetica"));
  if (!pobj_dict_get(fonts, "ZaDb")) pobj_dict_set(fonts, "ZaDb", om_std_font("ZapfDingbats"));
  if (!pobj_dict_get(af, "DA")) pobj_dict_set(af, "DA", pobj_string("/Helv 0 Tf 0 g", 14));
  return fonts;
}
/* whether a field of this full name exists */
static int name_used(PDoc *d, const char *name)
{
  FForm f;
  int i, used = 0;
  form_load(d, &f);
  for (i = 0; i < f.n && !used; i++) if (!strcmp(f.f[i].name, name)) used = 1;
  form_free(&f);
  return used;
}
/* the top-level field called name, NULL none */
static PObj *top_field(PDoc *d, PObj *fields, const char *name)
{
  int i;
  for (i = 0; i < fields->u.arr.n; i++) {
    PObj *f = pdf_resolve(d, fields->u.arr.v[i]);
    char *t = f ? text_of(d, pdf_dget(d, f, "T")) : NULL;
    int same = t && !strcmp(t, name);
    free(t);
    if (same) return f;
  }
  return NULL;
}
static int add_to_page(PDoc *d, int page, int num)
{
  PObj *pg = pc_page(d, page), *a;
  if (!pg) return 0;
  a = pdf_dget(d, pg, "Annots");
  if (!a || a->type != PO_ARRAY) { if (!(a = pobj_new(PO_ARRAY))) return 0; pobj_dict_set(pg, "Annots", a); }
  else if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *c = pobj_copy(a); if (!c) return 0; pobj_dict_set(pg, "Annots", c); a = c; }
  pobj_array_add(a, om_ref(num));
  return 1;
}
static const char *const kind_names[] = { "Text", "Check Box", "Radio", "Button", "Dropdown", "List Box", "Signature" };
int form_add_field(PDoc *d, int page, int type, const double *rect, const char *name, const char *const *opts, int nopts, int multiline)
{
  PObj *af = acroform(d, 1), *fields, *w, *mk, *parent = NULL;
  PObj *pg = pc_page(d, page);
  char nm[128];
  int num, i, pn;
  double r[4];
  FForm f;
  if (!af || !pg || !dr_fonts(d, af)) return 0;
  fields = pdf_dget(d, af, "Fields");
  if (!fields || fields->type != PO_ARRAY) { if (!(fields = pobj_new(PO_ARRAY))) return 0; pobj_dict_set(af, "Fields", fields); }
  r[0] = rect[0] < rect[2] ? rect[0] : rect[2]; r[2] = rect[0] < rect[2] ? rect[2] : rect[0];
  r[1] = rect[1] < rect[3] ? rect[1] : rect[3]; r[3] = rect[1] < rect[3] ? rect[3] : rect[1];
  /* the name: as given (a radio button joins its group), else the kind's with the first free number */
  if (name && *name && type == FF_RADIO) parent = top_field(d, fields, name);
  if (name && *name) { strncpy(nm, name, sizeof nm - 1); nm[sizeof nm - 1] = 0; }
  else snprintf(nm, sizeof nm, "%s", kind_names[type >= 0 && type <= FF_SIG ? type : 0]);
  if (!parent && name_used(d, nm)) {
    char base[110];
    strncpy(base, nm, sizeof base - 1); base[sizeof base - 1] = 0;
    for (i = 2; i < 10000; i++) { snprintf(nm, sizeof nm, "%s %d", base, i); if (!name_used(d, nm)) break; }
  }
  if (!(w = pobj_new(PO_DICT)) || !(mk = pobj_new(PO_DICT))) { pobj_free(w); return 0; }
  pobj_dict_set(w, "Type", pobj_name("Annot"));
  pobj_dict_set(w, "Subtype", pobj_name("Widget"));
  pobj_dict_set(w, "Rect", om_arr(r, 4));
  pobj_dict_set(w, "F", pobj_int(4));
  if ((pn = om_num_of(d, pg))) pobj_dict_set(w, "P", om_ref(pn));
  {
    double bc[3] = { 0, 0, 0 }, bg[3] = { 1, 1, 1 };
    if (type != FF_PUSH) pobj_dict_set(mk, "BG", om_arr(bg, 3)); else { bg[0] = bg[1] = bg[2] = 0.85; pobj_dict_set(mk, "BG", om_arr(bg, 3)); }
    pobj_dict_set(mk, "BC", om_arr(bc, 3));
  }
  pobj_dict_set(w, "DA", pobj_string("/Helv 0 Tf 0 g", 14));
  switch (type) {
  case FF_CHECK: pobj_dict_set(w, "FT", pobj_name("Btn")); pobj_dict_set(mk, "CA", pobj_string("4", 1)); pobj_dict_set(w, "V", pobj_name("Off")); pobj_dict_set(w, "AS", pobj_name("Off")); break;
  case FF_RADIO: pobj_dict_set(mk, "CA", pobj_string("l", 1)); pobj_dict_set(w, "AS", pobj_name("Off")); break;
  case FF_PUSH: pobj_dict_set(w, "FT", pobj_name("Btn")); pobj_dict_set(w, "Ff", pobj_int(FFL_PUSH)); pobj_dict_set(mk, "CA", om_text(nm)); break;
  case FF_COMBO: case FF_LIST: {
    PObj *o = pobj_new(PO_ARRAY);
    pobj_dict_set(w, "FT", pobj_name("Ch"));
    if (type == FF_COMBO) pobj_dict_set(w, "Ff", pobj_int(FFL_COMBO));
    for (i = 0; o && i < nopts; i++) pobj_array_add(o, om_text(opts[i]));
    pobj_dict_set(w, "Opt", o);
    break; }
  case FF_SIG: pobj_dict_set(w, "FT", pobj_name("Sig")); break;
  default:
    pobj_dict_set(w, "FT", pobj_name("Tx"));
    if (multiline) pobj_dict_set(w, "Ff", pobj_int(FFL_MULTILINE));
    break;
  }
  pobj_dict_set(w, "MK", mk);
  if (type == FF_RADIO) {
    /* the group: a field of its own with the button as a kid; its state named after its place */
    char st[24];
    PObj *kids;
    int gnum;
    if (!parent) {
      PObj *g = pobj_new(PO_DICT);
      if (!g) { pobj_free(w); return 0; }
      pobj_dict_set(g, "FT", pobj_name("Btn"));
      pobj_dict_set(g, "Ff", pobj_int(FFL_RADIO | FFL_NOTOGGLE));
      pobj_dict_set(g, "T", om_text(nm));
      pobj_dict_set(g, "V", pobj_name("Off"));
      pobj_dict_set(g, "Kids", pobj_new(PO_ARRAY));
      if (!(gnum = pdf_new_object(d, g))) { pobj_free(w); return 0; }
      pobj_array_add(fields, om_ref(gnum));
      parent = g;
    } else gnum = om_num_of(d, parent);
    kids = pdf_dget(d, parent, "Kids");
    if (!kids || kids->type != PO_ARRAY) { kids = pobj_new(PO_ARRAY); pobj_dict_set(parent, "Kids", kids); }
    snprintf(st, sizeof st, "Choice%d", kids ? kids->u.arr.n + 1 : 1);
    {
      PObj *ap = pobj_new(PO_DICT), *n = pobj_new(PO_DICT);
      /* (placeholder states so the "on" name is known; the real appearances follow) */
      pobj_dict_set(n, st, pobj_new(PO_NULL));
      pobj_dict_set(ap, "N", n);
      pobj_dict_set(w, "AP", ap);
    }
    if (gnum) pobj_dict_set(w, "Parent", om_ref(gnum));
    if (!(num = pdf_new_object(d, w))) return 0;
    if (kids) pobj_array_add(kids, om_ref(num));
  } else {
    pobj_dict_set(w, "T", om_text(nm));
    if (!(num = pdf_new_object(d, w))) return 0;
    pobj_array_add(fields, om_ref(num));
  }
  if (!add_to_page(d, page, num)) return 0;
  d->changed = 1;
  /* its appearance */
  form_load(d, &f);
  for (i = 0; i < f.n; i++) {
    int k;
    for (k = 0; k < f.f[i].nw; k++) if (f.f[i].w[k].dict == w) {
      if (type == FF_RADIO) {
        /* the placeholder taken away so the real states are made */
        PObj *ap = pdf_dget(d, w, "AP");
        if (ap) pobj_dict_del(w, "AP");
        (void)ap;
      }
      if (type == FF_PUSH) {
        /* a button: its caption centred on its face */
        double bw, bh;
        int rot;
        Buf b;
        int n;
        char *s = latin(nm, &n);
        widget_box(d, w, &bw, &bh, &rot);
        buf_init(&b);
        put_frame(d, w, &b, bw, bh);
        buf_adds(&b, "BT\n/FoF 10 Tf 0 g\n");
        om_num(&b, (bw - om_text_width(s ? s : "", n, 10, 0)) / 2); om_num(&b, (bh - 7) / 2); buf_adds(&b, "Td ");
        om_str(&b, s ? s : "", n); buf_adds(&b, " Tj\nET\n");
        set_ap(d, w, &b, bw, bh, rot, font_res(0), NULL, NULL);
        buf_free(&b); free(s);
      } else if (type == FF_SIG) {
        double bw, bh;
        int rot;
        Buf b;
        widget_box(d, w, &bw, &bh, &rot);
        buf_init(&b);
        put_frame(d, w, &b, bw, bh);
        set_ap(d, w, &b, bw, bh, rot, NULL, NULL, NULL);
        buf_free(&b);
      } else field_ap(d, &f.f[i]);
    }
  }
  form_free(&f);
  return 1;
}
int form_add_table(PDoc *d, int page, const double *rect, int rows, int cols, const char *name)
{
  double cw, ch, x0, y1;
  int r, c, ok = 1;
  char nm[160];
  if (rows < 1 || cols < 1 || rows > 100 || cols > 50) return 0;
  x0 = rect[0] < rect[2] ? rect[0] : rect[2]; y1 = rect[1] > rect[3] ? rect[1] : rect[3];
  cw = fabs(rect[2] - rect[0]) / cols; ch = fabs(rect[3] - rect[1]) / rows;
  for (r = 0; r < rows; r++) for (c = 0; c < cols; c++) {
    double cr[4];
    cr[0] = x0 + c * cw; cr[2] = cr[0] + cw; cr[3] = y1 - r * ch; cr[1] = cr[3] - ch;
    snprintf(nm, sizeof nm, "%s.%d.%d", name && *name ? name : "Table", r + 1, c + 1);
    ok &= form_add_field(d, page, FF_TEXT, cr, nm, NULL, 0, 0);
  }
  return ok;
}
int form_set_props(PDoc *d, FField *f, const char *name, const char *tooltip, long set, long clear, int maxlen, int quad, double size)
{
  long ff = dbl_long(pdf_dnum(d, f->dict, "Ff", (double)f->flags));
  if (name && *name) {
    const char *dot = strrchr(f->name, '.');
    size_t pl = dot ? (size_t)(dot - f->name) + 1 : 0, l = pl + strlen(name) + 1;
    char *full = (char *)malloc(l);
    pobj_dict_set(f->dict, "T", om_text(name));
    if (full) { memcpy(full, f->name, pl); strcpy(full + pl, name); free(f->name); f->name = full; }
  }
  if (tooltip) {
    if (*tooltip) pobj_dict_set(f->dict, "TU", om_text(tooltip)); else pobj_dict_del(f->dict, "TU");
    free(f->tooltip); f->tooltip = *tooltip ? str_dup(tooltip) : NULL;
  }
  if (set || clear) { ff = (ff | set) & ~clear; pobj_dict_set(f->dict, "Ff", pobj_int(ff)); f->flags = ff; }
  if (maxlen >= 0) { if (maxlen) pobj_dict_set(f->dict, "MaxLen", pobj_int(maxlen)); else pobj_dict_del(f->dict, "MaxLen"); f->maxlen = maxlen; }
  if (quad >= 0 && quad <= 2) { pobj_dict_set(f->dict, "Q", pobj_int(quad)); f->quad = quad; }
  if (size >= 0) {
    char da[64];
    int k;
    DA cur;
    parse_da(d, f, f->nw ? f->w[0].dict : f->dict, &cur);
    snprintf(da, sizeof da, "/%s %g Tf %g g", cur.fname, size, cur.ncol == 1 ? cur.col[0] : 0.0);
    pobj_dict_set(f->dict, "DA", pobj_string(da, (int)strlen(da)));
    for (k = 0; k < f->nw; k++) if (f->w[k].dict != f->dict) pobj_dict_del(f->w[k].dict, "DA");
  }
  d->changed = 1;
  return field_ap(d, f);
}
int form_delete_field(PDoc *d, FField *f)
{
  PObj *af = acroform(d, 0), *fields = af ? pdf_dget(d, af, "Fields") : NULL, *par;
  int k, i, p;
  /* the widgets off their pages */
  for (k = 0; k < f->nw; k++) for (p = 0; p < d->npages; p++) {
    PObj *a = pdf_dget(d, d->pages[p], "Annots");
    if (!a || a->type != PO_ARRAY) continue;
    for (i = 0; i < a->u.arr.n; i++) if (pdf_resolve(d, a->u.arr.v[i]) == f->w[k].dict) { array_remove(a, i); break; }
  }
  /* the field out of its parent's kids, or the form's fields */
  par = pdf_dget(d, f->dict, "Parent");
  {
    PObj *list = par ? pdf_dget(d, par, "Kids") : fields;
    if (list && list->type == PO_ARRAY) for (i = 0; i < list->u.arr.n; i++) if (pdf_resolve(d, list->u.arr.v[i]) == f->dict) { array_remove(list, i); break; }
  }
  d->changed = 1;
  return 1;
}
typedef struct { PObj *ref; double x, y; int widget; int i; } Ord;
static int by_rows(const void *a, const void *b)
{
  const Ord *p = (const Ord *)a, *q = (const Ord *)b;
  if (p->widget != q->widget) return p->widget ? 1 : -1;
  if (!p->widget) return p->i - q->i;
  if (fabs(p->y - q->y) > 4) return p->y > q->y ? -1 : 1;
  if (p->x != q->x) return p->x < q->x ? -1 : 1;
  return p->i - q->i;
}
static int by_cols(const void *a, const void *b)
{
  const Ord *p = (const Ord *)a, *q = (const Ord *)b;
  if (p->widget != q->widget) return p->widget ? 1 : -1;
  if (!p->widget) return p->i - q->i;
  if (fabs(p->x - q->x) > 4) return p->x < q->x ? -1 : 1;
  if (p->y != q->y) return p->y > q->y ? -1 : 1;
  return p->i - q->i;
}
int form_tab_order(PDoc *d, int by_columns)
{
  int p, i, n = 0;
  for (p = 0; p < d->npages; p++) {
    PObj *pg = d->pages[p], *a = pdf_dget(d, pg, "Annots");
    Ord *o;
    if (!a || a->type != PO_ARRAY || !a->u.arr.n) continue;
    if (pobj_dict_get(pg, "Annots")->type == PO_REF) { PObj *c = pobj_copy(a); if (!c) continue; pobj_dict_set(pg, "Annots", c); a = c; }
    if (!(o = (Ord *)malloc((size_t)a->u.arr.n * sizeof(Ord)))) continue;
    for (i = 0; i < a->u.arr.n; i++) {
      PObj *an = pdf_resolve(d, a->u.arr.v[i]);
      double r[4] = { 0, 0, 0, 0 };
      o[i].ref = a->u.arr.v[i]; o[i].i = i;
      o[i].widget = an && an->type == PO_DICT && pobj_is_name(pdf_dget(d, an, "Subtype"), "Widget");
      if (an) get_rect(d, pdf_dget(d, an, "Rect"), r);
      o[i].x = r[0]; o[i].y = r[3];
      n += o[i].widget;
    }
    qsort(o, (size_t)a->u.arr.n, sizeof(Ord), by_columns ? by_cols : by_rows);
    for (i = 0; i < a->u.arr.n; i++) a->u.arr.v[i] = o[i].ref;
    free(o);
    pobj_dict_set(pg, "Tabs", pobj_name(by_columns ? "C" : "R"));
  }
  d->changed = 1;
  return n;
}
int form_calc_order(PDoc *d, FForm *f, const int *fields, int n)
{
  PObj *af = acroform(d, 0), *co;
  int i;
  if (!af || !(co = pobj_new(PO_ARRAY))) return 0;
  for (i = 0; i < n; i++) if (fields[i] >= 0 && fields[i] < f->n) {
    int num = om_num_of(d, f->f[fields[i]].dict);
    if (num) pobj_array_add(co, om_ref(num));
  }
  pobj_dict_set(af, "CO", co);
  d->changed = 1;
  return 1;
}
int form_detect(PDoc *d, int page, TextPage *t)
{
  int i, n = 0;
  if (!t || !t->finished) return 0;
  for (i = 0; i < t->no; ) {
    TChar *c = &t->o[i];
    if (c->u == '_') {
      int j = i;
      double r[4];
      while (j + 1 < t->no && t->o[j + 1].u == '_') j++;
      if (j - i + 1 >= 4) {
        r[0] = t->o[i].x0; r[2] = t->o[j].x1; r[1] = t->o[i].y0; r[3] = t->o[i].y0 + t->o[i].size * 1.25;
        if (r[2] - r[0] > 10 && form_add_field(d, page, FF_TEXT, r, NULL, NULL, 0, 0)) n++;
      }
      i = j + 1;
      continue;
    }
    if (c->u == 0x2610 || (c->u == '[' && i + 2 < t->no && t->o[i + 1].u == ' ' && t->o[i + 2].u == ']')) {
      double r[4], s = c->size > 4 ? c->size : 10;
      int last = c->u == '[' ? i + 2 : i;
      r[0] = c->x0; r[2] = c->u == '[' ? t->o[last].x1 : c->x1; r[1] = c->y0; r[3] = c->y0 + s * 0.8;
      if (r[2] - r[0] < s * 0.5) r[2] = r[0] + s * 0.7;
      if (form_add_field(d, page, FF_CHECK, r, NULL, NULL, 0, 0)) n++;
      i = last + 1;
      continue;
    }
    i++;
  }
  return n;
}
