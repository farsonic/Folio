/*
 * contentedit.c - content streams rewritten operator by operator (see contentedit.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "contentedit.h"
#include "pagecontent.h"
#include "image.h"
#include "filters.h"
#include "jpegout.h"

#define MAXARGS 64
#define MAXGS 64
typedef struct { char name[64]; PdfFont *f; } FCache;
typedef struct {
  PDoc *d;
  const CEOps *ops;
  PObj *res;                /* the resources (a copy: new names go into it) */
  int changed;
  Buf out;
  CEState gs[MAXGS]; int ngs;
  double tm[6], tlm[6];
  FCache *fc; int nfc;
  int depth;
  int mc, hide_at;          /* marked-content depth; the depth hiding began at (-1 none) */
} CE;

static void mul(const double a[6], const double b[6], double r[6])
{
  double t[6];
  t[0] = a[0] * b[0] + a[1] * b[2]; t[1] = a[0] * b[1] + a[1] * b[3];
  t[2] = a[2] * b[0] + a[3] * b[2]; t[3] = a[2] * b[1] + a[3] * b[3];
  t[4] = a[4] * b[0] + a[5] * b[2] + b[4]; t[5] = a[4] * b[1] + a[5] * b[3] + b[5];
  memcpy(r, t, sizeof t);
}
static void apply(const double m[6], double x, double y, double *ox, double *oy) { *ox = m[0] * x + m[2] * y + m[4]; *oy = m[1] * x + m[3] * y + m[5]; }

/* ------------------------------------------------------------------ writing operands */
static void put_num(Buf *b, double v)
{
  char t[40];
  int n;
  if (fabs(v) < 1e-9) v = 0;
  if (v == floor(v) && fabs(v) < 2e9) { buf_printf(b, "%ld", (long)v); return; }
  n = snprintf(t, sizeof t, "%.5f", v);
  while (n > 0 && t[n - 1] == '0') t[--n] = 0;
  if (n > 0 && t[n - 1] == '.') t[--n] = 0;
  buf_adds(b, t);
}
static void put_obj(Buf *b, const PObj *o, int depth)
{
  int i;
  if (!o || depth > 32) { buf_adds(b, "null"); return; }
  switch (o->type) {
  case PO_NULL: buf_adds(b, "null"); break;
  case PO_BOOL: buf_adds(b, o->u.b ? "true" : "false"); break;
  case PO_INT: buf_printf(b, "%ld", o->u.i); break;
  case PO_REAL: put_num(b, o->u.r); break;
  case PO_NAME:
    buf_addc(b, '/');
    for (i = 0; i < o->u.str.len; i++) { u8 c = (u8)o->u.str.s[i]; if (c > 32 && c < 127 && !strchr("()<>[]{}/%#", c)) buf_addc(b, c); else buf_printf(b, "#%02X", c); }
    break;
  case PO_STRING:
    buf_addc(b, '<');
    for (i = 0; i < o->u.str.len; i++) buf_printf(b, "%02X", (u8)o->u.str.s[i]);
    buf_addc(b, '>');
    break;
  case PO_ARRAY:
    buf_addc(b, '[');
    for (i = 0; i < o->u.arr.n; i++) { if (i) buf_addc(b, ' '); put_obj(b, o->u.arr.v[i], depth + 1); }
    buf_addc(b, ']');
    break;
  case PO_DICT: case PO_STREAM:
    buf_adds(b, "<<");
    for (i = 0; i < o->u.dict.n; i++) {
      PObj nm;
      memset(&nm, 0, sizeof nm);
      nm.type = PO_NAME; nm.u.str.s = o->u.dict.k[i]; nm.u.str.len = (int)strlen(o->u.dict.k[i]);
      put_obj(b, &nm, depth + 1); buf_addc(b, ' ');
      put_obj(b, o->u.dict.v[i], depth + 1); buf_addc(b, ' ');
    }
    buf_adds(b, ">>");
    break;
  case PO_REF: buf_printf(b, "%d %d R", o->u.ref.num, o->u.ref.gen); break;
  }
}
static void emit(CE *c, PObj **args, int n, const char *kw)
{
  int i;
  for (i = 0; i < n; i++) { put_obj(&c->out, args[i], 0); buf_addc(&c->out, ' '); }
  buf_adds(&c->out, kw);
  buf_addc(&c->out, '\n');
}

/* ------------------------------------------------------------------ resources */
static PObj *res_get(CE *c, const char *cat, const char *name)
{
  PObj *d = c->res ? pdf_dget(c->d, c->res, cat) : NULL;
  return d ? pdf_dget(c->d, d, name) : NULL;
}
static PdfFont *font_of(CE *c, const char *name)
{
  int i;
  PObj *fd;
  PdfFont *f;
  FCache *n;
  for (i = 0; i < c->nfc; i++) if (!strcmp(c->fc[i].name, name)) return c->fc[i].f;
  fd = res_get(c, "Font", name);
  f = fd ? font_load(c->d, fd) : NULL;
  if ((n = (FCache *)realloc(c->fc, (size_t)(c->nfc + 1) * sizeof(FCache)))) {
    c->fc = n;
    strncpy(c->fc[c->nfc].name, name, sizeof c->fc[0].name - 1);
    c->fc[c->nfc].name[sizeof c->fc[0].name - 1] = 0;
    c->fc[c->nfc].f = f;
    c->nfc++;
  }
  return f;
}
/* whether an optional content group (or membership dictionary) is off */
static int ocg_off(PDoc *d, PObj *g)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *ocp = root ? pdf_dget(d, root, "OCProperties") : NULL, *def = ocp ? pdf_dget(d, ocp, "D") : NULL;
  PObj *off = def ? pdf_dget(d, def, "OFF") : NULL;
  int i, num;
  g = pdf_resolve(d, g);
  if (!g || g->type != PO_DICT || !off || off->type != PO_ARRAY) return 0;
  if (pobj_is_name(pdf_dget(d, g, "Type"), "OCMD")) {
    PObj *gs = pdf_dget(d, g, "OCGs");
    if (!gs) return 0;
    if (gs->type == PO_DICT) return ocg_off(d, gs);
    if (gs->type != PO_ARRAY) return 0;
    for (i = 0; i < gs->u.arr.n; i++) if (!ocg_off(d, gs->u.arr.v[i])) return 0;
    return gs->u.arr.n > 0;
  }
  num = om_num_of(d, g);
  for (i = 0; i < off->u.arr.n; i++) if (off->u.arr.v[i]->type == PO_REF ? off->u.arr.v[i]->u.ref.num == num : pdf_resolve(d, off->u.arr.v[i]) == g) return 1;
  return 0;
}
/* an XObject replaced: a new name in the resources for object num */
static int new_xobject(CE *c, int num, char *name, int cap)
{
  if (!c->res && !(c->res = pobj_new(PO_DICT))) return 0;
  return pc_add_resource(c->d, c->res, "XObject", "FoCE", om_ref(num), name, cap);
}

/* ------------------------------------------------------------------ text */
static CEState *gst(CE *c) { return &c->gs[c->ngs - 1]; }
/* a string's glyphs walked: kept bytes into the TJ being built (seg), removed ones as spacing */
static void show_string(CE *c, PObj *str, Buf *tj, int *removed)
{
  CEState *s = gst(c);
  PdfFont *f = s->font;
  const u8 *p = (const u8 *)str->u.str.s;
  int len = str->u.str.len, open = 0;
  while (len > 0) {
    u32 code = 0;
    int nb = f ? font_next(f, p, len, &code) : 1, rm = 0, k;
    double w, tx, trm[6], sm[6], box[4];
    if (nb < 1) nb = 1;
    if (nb > len) nb = len;
    if (!f) code = p[0];
    w = f ? font_width(f, code, nb) / 1000.0 : 0.5;
    tx = (w * s->size + s->tc + (nb == 1 && code == 32 ? s->tw : 0)) * s->th;
    /* the glyph's box: glyph space 0..w across, -0.2..0.8 up (an em's usual descent and ascent) */
    sm[0] = s->size * s->th; sm[1] = 0; sm[2] = 0; sm[3] = s->size; sm[4] = 0; sm[5] = s->rise;
    mul(sm, c->tm, trm); mul(trm, s->ctm, trm);
    {
      double xs[4], ys[4], gw = w > 0.05 ? w : 0.3;
      apply(trm, 0, -0.2, &xs[0], &ys[0]); apply(trm, gw, -0.2, &xs[1], &ys[1]);
      apply(trm, 0, 0.8, &xs[2], &ys[2]); apply(trm, gw, 0.8, &xs[3], &ys[3]);
      box[0] = box[2] = xs[0]; box[1] = box[3] = ys[0];
      for (k = 1; k < 4; k++) { if (xs[k] < box[0]) box[0] = xs[k]; if (xs[k] > box[2]) box[2] = xs[k]; if (ys[k] < box[1]) box[1] = ys[k]; if (ys[k] > box[3]) box[3] = ys[k]; }
    }
    if (c->ops->drop_invisible && s->tmode == 3) rm = 1;
    else if (c->ops->glyph) rm = c->ops->glyph(c->ops->ud, box, code, s);
    if (rm) {
      if (open) { buf_adds(tj, "> "); open = 0; }
      if (fabs(s->size * s->th) > 1e-9) { put_num(tj, -tx * 1000 / (s->size * s->th)); buf_addc(tj, ' '); }
      (*removed)++;
    } else {
      if (!open) { buf_addc(tj, '<'); open = 1; }
      for (k = 0; k < nb; k++) buf_printf(tj, "%02X", p[k]);
    }
    /* the text matrix moves on */
    { double t[6] = { 1, 0, 0, 1, 0, 0 }; t[4] = tx; mul(t, c->tm, c->tm); }
    p += nb; len -= nb;
  }
  if (open) buf_adds(tj, "> ");
}
static void show(CE *c, PObj *arg, int is_array, PObj **args, int nargs, const char *kw)
{
  Buf tj;
  int removed = 0, i;
  CEState *s = gst(c);
  buf_init(&tj);
  buf_addc(&tj, '[');
  if (!is_array) { if (arg && arg->type == PO_STRING) show_string(c, arg, &tj, &removed); }
  else if (arg && arg->type == PO_ARRAY) {
    for (i = 0; i < arg->u.arr.n; i++) {
      PObj *e = arg->u.arr.v[i];
      if (e->type == PO_STRING) show_string(c, e, &tj, &removed);
      else if (pobj_isnum(e)) {
        double v = pobj_num(e), t[6] = { 1, 0, 0, 1, 0, 0 };
        put_num(&tj, v); buf_addc(&tj, ' ');
        t[4] = -v / 1000 * s->size * s->th;
        mul(t, c->tm, c->tm);
      }
    }
  }
  buf_addc(&tj, ']');
  if (removed) { buf_add(&c->out, tj.data, tj.len); buf_adds(&c->out, " TJ\n"); c->changed = 1; }
  else emit(c, args, nargs, kw);
  buf_free(&tj);
}

/* ------------------------------------------------------------------ the walk */
static void run(CE *c, const u8 *data, size_t len);
static void do_xobject(CE *c, PObj **args, int nargs)
{
  PObj *x;
  const char *st;
  if (nargs < 1 || args[0]->type != PO_NAME || !(x = res_get(c, "XObject", args[0]->u.str.s)) || x->type != PO_STREAM) { emit(c, args, nargs, "Do"); return; }
  st = pdf_dname(c->d, x, "Subtype");
  if (st && !strcmp(st, "Image")) {
    int nn = 0, r = 0;
    if (c->ops->image) r = c->ops->image(c->ops->ud, x, 0, gst(c)->ctm, &nn);
    if (r == 1) { c->changed = 1; return; }
    if (r == 2 && nn) {
      char name[40];
      if (new_xobject(c, nn, name, sizeof name)) { buf_printf(&c->out, "/%s Do\n", name); c->changed = 1; return; }
    }
    emit(c, args, nargs, "Do");
    return;
  }
  if (st && !strcmp(st, "Form") && c->depth < 12) {
    size_t len, nl;
    u8 *data = pdf_stream_data(c->d, x, &len), *nd;
    double m[6] = { 1, 0, 0, 1, 0, 0 }, ctm[6];
    PObj *fm = pdf_dget(c->d, x, "Matrix"), *fres = pdf_dget(c->d, x, "Resources"), *ro = NULL;
    int i;
    if (fm && fm->type == PO_ARRAY && fm->u.arr.n >= 6) for (i = 0; i < 6; i++) m[i] = pobj_num(pdf_resolve(c->d, fm->u.arr.v[i]));
    mul(m, gst(c)->ctm, ctm);
    if (!data) { emit(c, args, nargs, "Do"); return; }
    nd = ce_rewrite(c->d, data, len, fres ? fres : c->res, ctm, c->ops, &nl, &ro);
    free(data);
    if (nd) {
      /* a copy of the form with the new content */
      PObj *dict = pobj_new(PO_DICT);
      int k, num;
      char name[40];
      for (k = 0; dict && k < x->u.dict.n; k++) {
        const char *key = x->u.dict.k[k];
        if (!strcmp(key, "Length") || !strcmp(key, "Filter") || !strcmp(key, "DecodeParms") || !strcmp(key, "Resources")) continue;
        pobj_dict_set(dict, key, pobj_copy(x->u.dict.v[k]));
      }
      if (dict) pobj_dict_set(dict, "Resources", ro ? ro : pobj_new(PO_DICT)); else pobj_free(ro);
      num = om_stream(c->d, dict, nd, nl);
      free(nd);
      if (num && new_xobject(c, num, name, sizeof name)) { buf_printf(&c->out, "/%s Do\n", name); c->changed = 1; return; }
    } else pobj_free(ro);
  }
  emit(c, args, nargs, "Do");
}
/* an inline image: BI, its parameters, ID, the data, EI (copied as it is, or left out) */
static void inline_image(CE *c, Lex *l)
{
  PObj *dict = pobj_new(PO_DICT);
  const u8 *start, *p;
  char *kw = NULL;
  int keep = 1;
  for (;;) {
    PObj *k = lex_object(l, &kw), *v;
    if (!k) break;
    if (k->type != PO_NAME) { pobj_free(k); continue; }
    v = lex_object(l, &kw);
    if (!v) { pobj_free(k); break; }
    if (dict) pobj_dict_set(dict, k->u.str.s, v); else pobj_free(v);
    pobj_free(k);
  }
  if (!kw || strcmp(kw, "ID")) { free(kw); pobj_free(dict); return; }
  free(kw);
  start = l->p;
  if (start < l->end && (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t')) start++;
  for (p = start; p + 1 < l->end; p++)
    if (p[0] == 'E' && p[1] == 'I' && (p == start || p[-1] <= 32) && (p + 2 >= l->end || p[2] <= 32 || p[2] == '%')) break;
  if (p + 1 >= l->end) p = l->end;
  if (c->ops->image) {
    int nn = 0;
    if (c->ops->image(c->ops->ud, dict, 1, gst(c)->ctm, &nn) == 1) keep = 0;
  }
  if (keep) {
    buf_adds(&c->out, "BI ");
    if (dict) {
      int i;
      for (i = 0; i < dict->u.dict.n; i++) {
        PObj nm;
        memset(&nm, 0, sizeof nm);
        nm.type = PO_NAME; nm.u.str.s = dict->u.dict.k[i]; nm.u.str.len = (int)strlen(dict->u.dict.k[i]);
        put_obj(&c->out, &nm, 0); buf_addc(&c->out, ' ');
        put_obj(&c->out, dict->u.dict.v[i], 0); buf_addc(&c->out, ' ');
      }
    }
    buf_adds(&c->out, "ID ");
    buf_add(&c->out, start, (size_t)(p - start));
    buf_adds(&c->out, "\nEI\n");
  } else c->changed = 1;
  l->p = p + 2 <= l->end ? p + 2 : l->end;
  pobj_free(dict);
}
static void run(CE *c, const u8 *data, size_t len)
{
  Lex l;
  PObj *args[MAXARGS + 8];
  int nargs = 0, i;
  lex_init(&l, data, len, c->d);
  for (;;) {
    char *kwp = NULL, kw[8];
    PObj *o = lex_object(&l, &kwp);
    CEState *s = gst(c);
    if (o) { if (nargs < MAXARGS) args[nargs++] = o; else pobj_free(o); continue; }
    if (!kwp) break;
    strncpy(kw, kwp, sizeof kw - 1); kw[sizeof kw - 1] = 0;
    free(kwp);
    /* hidden marked content: everything up to its EMC left out */
    if (c->hide_at >= 0) {
      if (!strcmp(kw, "BDC") || !strcmp(kw, "BMC")) c->mc++;
      else if (!strcmp(kw, "EMC")) { c->mc--; if (c->mc < c->hide_at) c->hide_at = -1; }
      else if (!strcmp(kw, "BI")) inline_image(c, &l);
      /* (the graphics state still followed: q/Q must balance with what is kept) */
      if (!strcmp(kw, "q") && c->ngs < MAXGS) { c->gs[c->ngs] = c->gs[c->ngs - 1]; c->ngs++; }
      else if (!strcmp(kw, "Q") && c->ngs > 1) c->ngs--;
      for (i = 0; i < nargs; i++) pobj_free(args[i]);
      nargs = 0;
      c->changed = 1;
      continue;
    }
    if (!strcmp(kw, "BI")) { inline_image(c, &l); for (i = 0; i < nargs; i++) pobj_free(args[i]); nargs = 0; continue; }
    if (!strcmp(kw, "q")) { if (c->ngs < MAXGS) { c->gs[c->ngs] = c->gs[c->ngs - 1]; c->ngs++; } emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "Q")) { if (c->ngs > 1) c->ngs--; emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "cm") && nargs >= 6) {
      double m[6];
      for (i = 0; i < 6; i++) m[i] = pobj_num(args[nargs - 6 + i]);
      mul(m, s->ctm, s->ctm);
      emit(c, args, nargs, kw);
    } else if (!strcmp(kw, "BT")) { double id[6] = { 1, 0, 0, 1, 0, 0 }; memcpy(c->tm, id, sizeof id); memcpy(c->tlm, id, sizeof id); emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "Tf") && nargs >= 2) {
      if (args[nargs - 2]->type == PO_NAME) s->font = font_of(c, args[nargs - 2]->u.str.s);
      s->size = pobj_num(args[nargs - 1]);
      emit(c, args, nargs, kw);
    } else if (!strcmp(kw, "Tc") && nargs) { s->tc = pobj_num(args[nargs - 1]); emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "Tw") && nargs) { s->tw = pobj_num(args[nargs - 1]); emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "Tz") && nargs) { s->th = pobj_num(args[nargs - 1]) / 100; emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "TL") && nargs) { s->tl = pobj_num(args[nargs - 1]); emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "Ts") && nargs) { s->rise = pobj_num(args[nargs - 1]); emit(c, args, nargs, kw); }
    else if (!strcmp(kw, "Tr") && nargs) { s->tmode = (int)pobj_num(args[nargs - 1]); emit(c, args, nargs, kw); }
    else if ((!strcmp(kw, "Td") || !strcmp(kw, "TD")) && nargs >= 2) {
      double t[6] = { 1, 0, 0, 1, 0, 0 };
      t[4] = pobj_num(args[nargs - 2]); t[5] = pobj_num(args[nargs - 1]);
      if (kw[1] == 'D') s->tl = -t[5];
      mul(t, c->tlm, c->tlm); memcpy(c->tm, c->tlm, sizeof c->tm);
      emit(c, args, nargs, kw);
    } else if (!strcmp(kw, "Tm") && nargs >= 6) {
      for (i = 0; i < 6; i++) c->tlm[i] = pobj_num(args[nargs - 6 + i]);
      memcpy(c->tm, c->tlm, sizeof c->tm);
      emit(c, args, nargs, kw);
    } else if (!strcmp(kw, "T*")) {
      double t[6] = { 1, 0, 0, 1, 0, 0 };
      t[5] = -s->tl; mul(t, c->tlm, c->tlm); memcpy(c->tm, c->tlm, sizeof c->tm);
      emit(c, args, nargs, kw);
    } else if (!strcmp(kw, "Tj") && nargs) show(c, args[nargs - 1], 0, args, nargs, kw);
    else if (!strcmp(kw, "TJ") && nargs) show(c, args[nargs - 1], 1, args, nargs, kw);
    else if ((!strcmp(kw, "'") || !strcmp(kw, "\"")) && nargs) {
      double t[6] = { 1, 0, 0, 1, 0, 0 };
      if (kw[0] == '"' && nargs >= 3) {
        s->tw = pobj_num(args[nargs - 3]); s->tc = pobj_num(args[nargs - 2]);
        put_num(&c->out, s->tw); buf_adds(&c->out, " Tw "); put_num(&c->out, s->tc); buf_adds(&c->out, " Tc\n");
      }
      t[5] = -s->tl; mul(t, c->tlm, c->tlm); memcpy(c->tm, c->tlm, sizeof c->tm);
      buf_adds(&c->out, "T*\n");
      {
        PObj *str = args[nargs - 1];
        show(c, str, 0, &str, 1, "Tj");
      }
    } else if (!strcmp(kw, "Do")) do_xobject(c, args, nargs);
    else if (!strcmp(kw, "BDC") || !strcmp(kw, "BMC")) {
      c->mc++;
      if (c->ops->drop_hidden && !strcmp(kw, "BDC") && nargs >= 2 && pobj_is_name(args[nargs - 2], "OC")) {
        PObj *g = args[nargs - 1]->type == PO_NAME ? res_get(c, "Properties", args[nargs - 1]->u.str.s) : args[nargs - 1];
        if (g && ocg_off(c->d, g)) { c->hide_at = c->mc; c->changed = 1; for (i = 0; i < nargs; i++) pobj_free(args[i]); nargs = 0; continue; }
      }
      emit(c, args, nargs, kw);
    } else if (!strcmp(kw, "EMC")) { c->mc--; emit(c, args, nargs, kw); }
    else {
      int drop = 0, na = nargs;
      if (!strcmp(kw, "w") && nargs) s->lw = pobj_num(args[nargs - 1]);
      if (c->ops->op) {
        char before[8];
        strcpy(before, kw);
        drop = c->ops->op(c->ops->ud, kw, args, &na, s);
        if (drop || na != nargs || strcmp(before, kw)) c->changed = 1;
        if (drop == 2) drop = 0;
        if (na > MAXARGS + 8) na = MAXARGS + 8;
        nargs = na;
      }
      if (!drop) emit(c, args, nargs, kw);
      else c->changed = 1;
    }
    for (i = 0; i < nargs; i++) pobj_free(args[i]);
    nargs = 0;
  }
  for (i = 0; i < nargs; i++) pobj_free(args[i]);
}
u8 *ce_rewrite(PDoc *d, const u8 *data, size_t len, PObj *res, const double *ctm, const CEOps *ops, size_t *n, PObj **res_out)
{
  CE c;
  int i;
  u8 *r = NULL;
  memset(&c, 0, sizeof c);
  c.d = d; c.ops = ops;
  res = pdf_resolve(d, res);
  c.res = res && res->type == PO_DICT ? pobj_copy(res) : NULL;
  buf_init(&c.out);
  c.ngs = 1;
  c.gs[0].th = 1; c.gs[0].lw = 1;
  if (ctm) memcpy(c.gs[0].ctm, ctm, 6 * sizeof(double));
  else { c.gs[0].ctm[0] = c.gs[0].ctm[3] = 1; }
  c.hide_at = -1;
  {
    static int depth;
    c.depth = ++depth;
    run(&c, data, len);
    depth--;
  }
  for (i = 0; i < c.nfc; i++) if (c.fc[i].f) font_free(c.fc[i].f);
  free(c.fc);
  if (c.changed && !c.out.oom) { *n = c.out.len; r = (u8 *)buf_take(&c.out); }
  buf_free(&c.out);
  if (res_out) *res_out = c.res; else pobj_free(c.res);
  return r;
}
int ce_rewrite_page(PDoc *d, int page, const CEOps *ops)
{
  PObj *pg = pc_page(d, page), *ro = NULL;
  size_t len, n;
  u8 *data, *nd;
  if (!pg || !(data = pc_content(d, pg, &len))) return 0;
  nd = ce_rewrite(d, data, len, pdf_page_attr(d, pg, "Resources"), NULL, ops, &n, &ro);
  free(data);
  if (!nd) { pobj_free(ro); return 0; }
  pc_set_content(d, pg, nd, n);
  free(nd);
  pobj_dict_set(pg, "Resources", ro ? ro : pobj_new(PO_DICT));
  return 1;
}

/* ------------------------------------------------------------------ image samples */
static int cs_comps(PDoc *d, PObj *cs, int depth)
{
  const char *n;
  cs = pdf_resolve(d, cs);
  if (!cs || depth > 4) return 0;
  if (cs->type == PO_NAME) {
    n = cs->u.str.s;
    if (!strcmp(n, "DeviceGray") || !strcmp(n, "G") || !strcmp(n, "CalGray")) return 1;
    if (!strcmp(n, "DeviceRGB") || !strcmp(n, "RGB")) return 3;
    if (!strcmp(n, "DeviceCMYK") || !strcmp(n, "CMYK")) return 4;
    return 0;
  }
  if (cs->type != PO_ARRAY || !cs->u.arr.n) return 0;
  {
    PObj *f = pdf_resolve(d, cs->u.arr.v[0]);
    if (!f || f->type != PO_NAME) return 0;
    n = f->u.str.s;
    if (!strcmp(n, "ICCBased") && cs->u.arr.n > 1) { PObj *s = pdf_resolve(d, cs->u.arr.v[1]); return s ? (int)pdf_dint(d, s, "N", 0) : 0; }
    if (!strcmp(n, "Indexed") || !strcmp(n, "I") || !strcmp(n, "Separation") || !strcmp(n, "CalGray")) return 1;
    if (!strcmp(n, "CalRGB") || !strcmp(n, "Lab")) return 3;
    if (!strcmp(n, "DeviceN") && cs->u.arr.n > 1) { PObj *a = pdf_resolve(d, cs->u.arr.v[1]); return a && a->type == PO_ARRAY ? a->u.arr.n : 0; }
    if (cs->u.arr.n == 1) return cs_comps(d, f, depth + 1);
  }
  return 0;
}
u8 *ce_image_samples(PDoc *d, PObj *img, int *w, int *h, int *comps, int *bpc, int *as_rgb)
{
  const char *imf = NULL;
  PObj *parms = NULL;
  size_t len, need;
  u8 *data;
  int mask = pdf_dget(d, img, "ImageMask") && pdf_dget(d, img, "ImageMask")->type == PO_BOOL && pdf_dget(d, img, "ImageMask")->u.b;
  *w = (int)pdf_dint(d, img, "Width", 0); *h = (int)pdf_dint(d, img, "Height", 0);
  *bpc = mask ? 1 : (int)pdf_dint(d, img, "BitsPerComponent", 8);
  *comps = mask ? 1 : cs_comps(d, pdf_dget(d, img, "ColorSpace"), 0);
  *as_rgb = 0;
  if (*w <= 0 || *h <= 0 || *w > 20000 || *h > 20000) return NULL;
  if (!(data = pdf_stream_data_raw(d, img, &len, &imf, &parms))) return NULL;
  if (imf) {
    u8 *rgb;
    if (strcmp(imf, "DCTDecode") && strcmp(imf, "DCT")) { free(data); return NULL; }
    rgb = img_decode(data, (u32)len, (u32)*w, (u32)*h);
    free(data);
    if (!rgb) return NULL;
    *comps = 3; *bpc = 8; *as_rgb = 1;
    return rgb;
  }
  if (*comps < 1 || *comps > 32 || (*bpc != 1 && *bpc != 2 && *bpc != 4 && *bpc != 8 && *bpc != 16)) { free(data); return NULL; }
  need = ((size_t)*w * *comps * *bpc + 7) / 8 * (size_t)*h;
  if (len < need) { free(data); return NULL; }
  return data;
}
int ce_image_store(PDoc *d, PObj *orig, const u8 *samples, int w, int h, int comps, int bpc, int as_rgb, int quality)
{
  PObj *dict = pobj_new(PO_DICT);
  int i, num;
  size_t n = ((size_t)w * comps * bpc + 7) / 8 * (size_t)h, zl;
  u8 *z;
  if (!dict) return 0;
  for (i = 0; i < orig->u.dict.n; i++) {
    const char *k = orig->u.dict.k[i];
    if (!strcmp(k, "Length") || !strcmp(k, "Filter") || !strcmp(k, "DecodeParms") || !strcmp(k, "Width") || !strcmp(k, "Height")) continue;
    if (as_rgb && (!strcmp(k, "ColorSpace") || !strcmp(k, "Decode") || !strcmp(k, "BitsPerComponent"))) continue;
    pobj_dict_set(dict, k, pobj_copy(orig->u.dict.v[i]));
  }
  pobj_dict_set(dict, "Width", pobj_int(w));
  pobj_dict_set(dict, "Height", pobj_int(h));
  if (as_rgb) { pobj_dict_set(dict, "ColorSpace", pobj_name("DeviceRGB")); pobj_dict_set(dict, "BitsPerComponent", pobj_int(8)); }
  if (quality > 0 && bpc == 8 && comps == 3) {
    PObj *cs = pdf_resolve(d, pobj_dict_get(dict, "ColorSpace"));
    int ok = as_rgb || (cs && cs->type == PO_NAME) || (cs && cs->type == PO_ARRAY && cs->u.arr.n > 1 && pobj_is_name(pdf_resolve(d, cs->u.arr.v[0]), "ICCBased"));
    if (ok && !pobj_dict_get(dict, "Decode") && (z = jpeg_write_rgb(samples, w, h, quality, &zl))) {
      pobj_dict_set(dict, "Filter", pobj_name("DCTDecode"));
      num = om_stream(d, dict, z, zl);
      free(z);
      return num;
    }
  }
  if (!(z = flt_deflate(samples, n, 6, &zl))) { pobj_free(dict); return 0; }
  pobj_dict_set(dict, "Filter", pobj_name("FlateDecode"));
  num = om_stream(d, dict, z, zl);
  free(z);
  return num;
}
