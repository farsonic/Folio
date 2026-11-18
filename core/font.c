/*
 * font.c - PDF fonts (see font.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "font.h"
#include "pdfenc.h"

/* ------------------------------------------------------------------ stand-ins */
static char fontdir[200] = "TrueType";
static const char *const subst_files[] = {
  "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf", "LiberationSans-Italic.ttf", "LiberationSans-BoldItalic.ttf",
  "LiberationSerif-Regular.ttf", "LiberationSerif-Bold.ttf", "LiberationSerif-Italic.ttf", "LiberationSerif-BoldItalic.ttf",
  "LiberationMono-Regular.ttf", "LiberationMono-Bold.ttf", "LiberationMono-Italic.ttf", "LiberationMono-BoldItalic.ttf",
  "Carlito-Regular.ttf", "Carlito-Bold.ttf", "Carlito-Italic.ttf", "Carlito-BoldItalic.ttf",
  "Caladea-Regular.ttf", "Caladea-Bold.ttf", "Caladea-Italic.ttf", "Caladea-BoldItalic.ttf"
};
#define NSUBST 20
static PFont *subst[NSUBST];
static u8 subst_tried[NSUBST];
void font_set_dir(const char *dir) { snprintf(fontdir, sizeof fontdir, "%s", dir); }
void font_free_substitutes(void)
{
  int i;
  for (i = 0; i < NSUBST; i++) { pf_free(subst[i]); subst[i] = NULL; subst_tried[i] = 0; }
}
/* stand-in i, loaded on first use (NULL if its file isn't there) */
static PFont *subst_font(int i)
{
  if (i < 0 || i >= NSUBST) return NULL;
  if (!subst_tried[i]) {
    char path[300];
    size_t len;
    u8 *data;
    char err[80];
    size_t n = strlen(fontdir);
    subst_tried[i] = 1;
    snprintf(path, sizeof path, "%s%s%s", fontdir, n && fontdir[n - 1] != ':' && fontdir[n - 1] != '/' ? "/" : "", subst_files[i]);
    if ((data = file_read(path, &len))) { subst[i] = pf_load(data, len, err, sizeof err); free(data); }
  }
  if (!subst[i] && i % 4) return subst_font(i - i % 4);      /* (no bold or italic file: the regular one) */
  if (!subst[i] && i >= 12) return subst_font(i >= 16 ? 4 + i % 4 : i % 4);   /* (Carlito/Caladea missing: Sans/Serif) */
  return subst[i];
}
static int has_word(const char *name, const char *w)
{
  char a[64], b[32];
  size_t i;
  for (i = 0; name[i] && i < sizeof a - 1; i++) a[i] = (char)tolower((unsigned char)name[i]);
  a[i] = 0;
  for (i = 0; w[i] && i < sizeof b - 1; i++) b[i] = (char)tolower((unsigned char)w[i]);
  b[i] = 0;
  return strstr(a, b) != NULL;
}
/* the stand-in for a font by its name and descriptor */
static PFont *pick_subst(PdfFont *f)
{
  int fam = 0, style;
  const char *n = f->name;
  if (has_word(n, "courier") || has_word(n, "mono") || has_word(n, "consol") || f->fixed) fam = 2;
  else if (has_word(n, "calibri")) fam = 3;
  else if (has_word(n, "cambria")) fam = 4;
  else if (has_word(n, "times") || has_word(n, "roman") || has_word(n, "serif") || has_word(n, "georgia") || has_word(n, "garamond") ||
           has_word(n, "book") || has_word(n, "palatino") || has_word(n, "minion") || (f->serif && !has_word(n, "sans") && !has_word(n, "arial") && !has_word(n, "helvet")))
    fam = 1;
  if (has_word(n, "sans") || has_word(n, "arial") || has_word(n, "helvet") || has_word(n, "verdana") || has_word(n, "tahoma")) fam = fam == 3 ? 3 : 0;
  style = (f->bold ? 1 : 0) + (f->italic ? 2 : 0);
  return subst_font(fam * 4 + style);
}

/* ------------------------------------------------------------------ helpers */
static void strip_subset(const char *s, char *out, size_t cap)
{
  size_t i;
  /* "ABCDEF+Name": the tag dropped */
  if (strlen(s) > 7 && s[6] == '+') { for (i = 0; i < 6 && s[i] >= 'A' && s[i] <= 'Z'; i++) ; if (i == 6) s += 7; }
  snprintf(out, cap, "%s", s);
}
/* the font program in a descriptor (FontFile, FontFile2, FontFile3), loaded; NULL if none */
static PFont *embedded(PDoc *d, PObj *desc)
{
  static const char *const keys[] = { "FontFile", "FontFile2", "FontFile3" };
  int i;
  for (i = 0; i < 3; i++) {
    PObj *s = pdf_dget(d, desc, keys[i]);
    if (s && s->type == PO_STREAM) {
      size_t len;
      u8 *data = pdf_stream_data(d, s, &len);
      char err[80];
      PFont *p = data ? pf_load(data, len, err, sizeof err) : NULL;
      free(data);
      if (p) return p;
    }
  }
  return NULL;
}
static void read_descriptor(PDoc *d, PdfFont *f, PObj *desc)
{
  long flags;
  const char *fn;
  if (!desc) return;
  flags = pdf_dint(d, desc, "Flags", 0);
  f->fixed = (flags & 1) != 0;
  f->serif = (flags & 2) != 0;
  f->symbolic = (flags & 4) != 0;
  f->italic = (flags & 64) != 0 || pdf_dnum(d, desc, "ItalicAngle", 0) < -3;
  f->bold = (flags & 0x40000) != 0 || pdf_dnum(d, desc, "FontWeight", 400) >= 600 || pdf_dnum(d, desc, "StemV", 0) >= 120;
  fn = pdf_dname(d, desc, "FontName");
  (void)fn;
}
static void style_from_name(PdfFont *f)
{
  if (has_word(f->name, "bold") || has_word(f->name, "black") || has_word(f->name, "heavy") || has_word(f->name, "semibold") || has_word(f->name, ",bold")) f->bold = 1;
  if (has_word(f->name, "italic") || has_word(f->name, "oblique")) f->italic = 1;
}
static PObj *load_tounicode(PDoc *d, PObj *dict, CMap **out)
{
  PObj *tu = pdf_dget(d, dict, "ToUnicode");
  *out = NULL;
  if (tu && tu->type == PO_STREAM) {
    size_t len;
    u8 *data = pdf_stream_data(d, tu, &len);
    if (data) { *out = cmap_parse(data, len); free(data); }
  } else if (tu && tu->type == PO_NAME && strstr(tu->u.str.s, "Identity")) *out = cmap_identity(0);
  return tu;
}
/* the standard 14 fonts' names, for Symbol and ZapfDingbats' own encodings */
static int is_symbol(const char *n) { return !strcmp(n, "Symbol") || has_word(n, "symbol"); }
static int is_dingbats(const char *n) { return has_word(n, "dingbats"); }

/* ------------------------------------------------------------------ simple fonts */
static const char *const *named_encoding(const char *n)
{
  if (!n) return NULL;
  if (!strcmp(n, "WinAnsiEncoding")) return enc_winansi;
  if (!strcmp(n, "MacRomanEncoding")) return enc_macroman;
  if (!strcmp(n, "MacExpertEncoding")) return enc_macexpert;
  if (!strcmp(n, "StandardEncoding")) return enc_standard;
  if (!strcmp(n, "PDFDocEncoding")) return enc_pdfdoc;
  return NULL;
}
/* a glyph in a TrueType program for a code and its glyph name (the PDF spec's recipe) */
static int tt_glyph(PdfFont *f, u32 code, const char *gname, int sym)
{
  int g;
  u32 u = gname ? agl_unicode(gname) : 0;
  if (!sym || f->substitute) {
    if (u && (g = pf_cmap(f->prog, 3, 1, u)) > 0) return g;
    if (gname && (g = pf_glyph_by_name(f->prog, gname)) > 0) return g;
    if (f->substitute) return 0;                    /* (a stand-in hasn't the glyph: nothing drawn) */
  }
  if ((g = pf_cmap(f->prog, 3, 0, code)) > 0) return g;
  if ((g = pf_cmap(f->prog, 3, 0, 0xF000 + code)) > 0) return g;
  if ((g = pf_cmap(f->prog, 3, 0, 0xF100 + code)) > 0) return g;
  if ((g = pf_cmap(f->prog, 3, 0, 0xF200 + code)) > 0) return g;
  if ((g = pf_cmap(f->prog, 1, 0, code)) > 0) return g;
  if (u && (g = pf_cmap(f->prog, 3, 1, u)) > 0) return g;
  if (!f->substitute && pf_cmap(f->prog, 3, 1, 32) < 0 && pf_cmap(f->prog, 1, 0, 32) < 0 && pf_cmap(f->prog, 3, 0, 32) < 0)
    return (int)code < pf_num_glyphs(f->prog) ? (int)code : 0;     /* (no cmap at all: codes are glyph ids) */
  return 0;
}
static void load_simple(PDoc *d, PdfFont *f, PObj *dict, const char *subtype)
{
  PObj *desc = pdf_dget(d, dict, "FontDescriptor"), *enc = pdf_dget(d, dict, "Encoding"), *diffs = NULL, *w;
  const char *const *base = NULL;
  const char *names[256];
  int i, first, truetype = subtype && !strcmp(subtype, "TrueType");
  const int *builtin = NULL;
  memset(names, 0, sizeof names);
  read_descriptor(d, f, desc);
  style_from_name(f);
  if (desc && (f->prog = embedded(d, desc))) f->own_prog = 1;
  if (!f->prog) { f->prog = pick_subst(f); f->substitute = 1; }
  if (f->prog && pf_kind(f->prog) == PF_TRUETYPE && !f->substitute) truetype = 1;
  /* the encoding: a name, or a dictionary with a base and differences */
  if (enc && enc->type == PO_NAME) base = named_encoding(enc->u.str.s);
  else if (enc && enc->type == PO_DICT) { base = named_encoding(pdf_dname(d, enc, "BaseEncoding")); diffs = pdf_dget(d, enc, "Differences"); }
  if (!base) {
    if (is_symbol(f->name) && f->substitute) base = enc_symbol;
    else if (is_dingbats(f->name) && f->substitute) base = enc_zapf;
    else if (f->prog && !f->substitute && pf_kind(f->prog) != PF_TRUETYPE && (builtin = pf_builtin_encoding(f->prog))) base = NULL;
    else if (truetype && f->symbolic && !f->substitute) base = NULL;
    else base = truetype ? enc_winansi : enc_standard;
  }
  for (i = 0; i < 256; i++) names[i] = base ? base[i] : NULL;
  if (builtin && !base) for (i = 0; i < 256; i++) if (builtin[i] > 0) names[i] = pf_glyph_name(f->prog, builtin[i]);
  if (diffs && diffs->type == PO_ARRAY) {
    int code = 0;
    for (i = 0; i < diffs->u.arr.n; i++) {
      PObj *x = pdf_resolve(d, diffs->u.arr.v[i]);
      if (pobj_isnum(x)) code = dbl_int(pobj_num(x));
      else if (x && x->type == PO_NAME) { if (code >= 0 && code < 256) names[code] = x->u.str.s; code++; }
    }
  }
  /* each code's glyph and text */
  for (i = 0; i < 256; i++) {
    int g = 0;
    const char *n = names[i];
    f->names[i] = n;
    if (f->prog) {
      if (pf_kind(f->prog) == PF_TRUETYPE) g = tt_glyph(f, (u32)i, n, (truetype && f->symbolic && !diffs && !(enc && enc->type == PO_NAME)));
      else {
        if (n) g = pf_glyph_by_name(f->prog, n);
        if (g <= 0 && builtin && builtin[i] > 0) g = builtin[i];
        if (g <= 0 && !n && !builtin) g = 0;
      }
    }
    f->gid[i] = g > 0 ? g : 0;
    f->uni[i] = n ? agl_unicode(n) : 0;
    if (!f->uni[i] && !n && i >= 32 && i < 127) f->uni[i] = (u32)i;
  }
  /* widths */
  first = (int)pdf_dint(d, dict, "FirstChar", 0);
  w = pdf_dget(d, dict, "Widths");
  {
    double missing = desc ? pdf_dnum(d, desc, "MissingWidth", 0) : 0;
    double m[6];
    pf_font_matrix(f->prog ? f->prog : NULL, m);
    for (i = 0; i < 256; i++) {
      double wv = -1;
      if (w && w->type == PO_ARRAY && i >= first && i - first < w->u.arr.n) {
        PObj *x = pdf_resolve(d, w->u.arr.v[i - first]);
        if (pobj_isnum(x)) wv = pobj_num(x);
      }
      if (wv < 0 && f->prog && f->gid[i] > 0) wv = pf_glyph_advance(f->prog, f->gid[i]) * m[0] * 1000;   /* (no widths: the program's own) */
      if (wv < 0) wv = missing;
      f->width[i] = wv;
    }
  }
}

/* ------------------------------------------------------------------ Type 3 */
static void load_type3(PDoc *d, PdfFont *f, PObj *dict)
{
  PObj *enc = pdf_dget(d, dict, "Encoding"), *fm = pdf_dget(d, dict, "FontMatrix"), *w = pdf_dget(d, dict, "Widths");
  int i, first = (int)pdf_dint(d, dict, "FirstChar", 0);
  f->kind = PFT_TYPE3;
  f->matrix = mat_make(0.001, 0, 0, 0.001, 0, 0);
  if (fm && fm->type == PO_ARRAY && fm->u.arr.n >= 6) {
    double v[6];
    for (i = 0; i < 6; i++) v[i] = pobj_num(pdf_resolve(d, fm->u.arr.v[i]));
    f->matrix = mat_make(v[0], v[1], v[2], v[3], v[4], v[5]);
  }
  f->charprocs = pdf_dget(d, dict, "CharProcs");
  f->resources = pdf_dget(d, dict, "Resources");
  if (enc && enc->type == PO_DICT) {
    PObj *diffs = pdf_dget(d, enc, "Differences");
    int code = 0;
    if (diffs && diffs->type == PO_ARRAY)
      for (i = 0; i < diffs->u.arr.n; i++) {
        PObj *x = pdf_resolve(d, diffs->u.arr.v[i]);
        if (pobj_isnum(x)) code = dbl_int(pobj_num(x));
        else if (x && x->type == PO_NAME) { if (code >= 0 && code < 256) f->names[code] = x->u.str.s; code++; }
      }
  }
  for (i = 0; i < 256; i++) {
    double wv = 0;
    if (w && w->type == PO_ARRAY && i >= first && i - first < w->u.arr.n) wv = pobj_num(pdf_resolve(d, w->u.arr.v[i - first]));
    /* (Type 3 widths are in glyph space: to text space x 1000 through the matrix) */
    f->width[i] = wv * f->matrix.a * 1000;
    f->uni[i] = f->names[i] ? agl_unicode(f->names[i]) : 0;
  }
}

/* ------------------------------------------------------------------ composite fonts */
static void load_type0(PDoc *d, PdfFont *f, PObj *dict)
{
  PObj *enc = pdf_dget(d, dict, "Encoding"), *desc_arr = pdf_dget(d, dict, "DescendantFonts"), *cid = NULL, *desc, *w;
  const char *sub;
  f->kind = PFT_TYPE0;
  if (enc && enc->type == PO_STREAM) {
    size_t len;
    u8 *data = pdf_stream_data(d, enc, &len);
    if (data) { f->encoding = cmap_parse(data, len); free(data); }
  } else if (enc && enc->type == PO_NAME) f->encoding = cmap_identity(strstr(enc->u.str.s, "-V") != NULL);
  if (!f->encoding) f->encoding = cmap_identity(0);
  f->vertical = cmap_vertical(f->encoding);
  if (desc_arr && desc_arr->type == PO_ARRAY && desc_arr->u.arr.n > 0) cid = pdf_resolve(d, desc_arr->u.arr.v[0]);
  else if (desc_arr && desc_arr->type == PO_DICT) cid = desc_arr;
  if (!cid) return;
  sub = pdf_dname(d, cid, "Subtype");
  desc = pdf_dget(d, cid, "FontDescriptor");
  read_descriptor(d, f, desc);
  style_from_name(f);
  if (desc && (f->prog = embedded(d, desc))) f->own_prog = 1;
  if (!f->prog) { f->prog = pick_subst(f); f->substitute = 1; }
  if (f->prog && pf_is_cid(f->prog)) f->cid_cff = 1;
  /* CIDToGIDMap */
  if (sub && !strcmp(sub, "CIDFontType2")) {
    PObj *m = pdf_dget(d, cid, "CIDToGIDMap");
    if (m && m->type == PO_STREAM) {
      size_t len, i;
      u8 *data = pdf_stream_data(d, m, &len);
      if (data && (f->cid2gid = (u16 *)malloc((len / 2 + 1) * sizeof(u16)))) {
        for (i = 0; i + 1 < len; i += 2) f->cid2gid[i / 2] = (u16)((data[i] << 8) | data[i + 1]);
        f->ncid2gid = dbl_int(len / 2);
      }
      free(data);
    }
  }
  /* widths: W [c [w1 w2 ...] c1 c2 w ...], DW */
  f->dw = pdf_dnum(d, cid, "DW", 1000);
  w = pdf_dget(d, cid, "W");
  if (w && w->type == PO_ARRAY) {
    int i = 0, cap = 0;
    while (i < w->u.arr.n) {
      PObj *a = pdf_resolve(d, w->u.arr.v[i]), *b = i + 1 < w->u.arr.n ? pdf_resolve(d, w->u.arr.v[i + 1]) : NULL;
      if (!pobj_isnum(a) || !b) break;
      if (b->type == PO_ARRAY) {
        int k, c = dbl_int(pobj_num(a));
        for (k = 0; k < b->u.arr.n; k++) {
          if (f->ncw >= cap) { int nc = cap ? cap * 2 : 64; void *p = realloc(f->cw, (size_t)nc * sizeof(*f->cw)); if (!p) return; f->cw = p; cap = nc; }
          f->cw[f->ncw].lo = f->cw[f->ncw].hi = c + k; f->cw[f->ncw].w = pobj_num(pdf_resolve(d, b->u.arr.v[k])); f->ncw++;
        }
        i += 2;
      } else {
        PObj *c = i + 2 < w->u.arr.n ? pdf_resolve(d, w->u.arr.v[i + 2]) : NULL;
        if (!pobj_isnum(b) || !pobj_isnum(c)) break;
        if (f->ncw >= cap) { int nc = cap ? cap * 2 : 64; void *p = realloc(f->cw, (size_t)nc * sizeof(*f->cw)); if (!p) return; f->cw = p; cap = nc; }
        f->cw[f->ncw].lo = dbl_int(pobj_num(a)); f->cw[f->ncw].hi = dbl_int(pobj_num(b)); f->cw[f->ncw].w = pobj_num(c); f->ncw++;
        i += 3;
      }
    }
  }
}

/* ------------------------------------------------------------------ the public calls */
PdfFont *font_load(PDoc *d, PObj *dict)
{
  PdfFont *f;
  const char *sub, *bf;
  dict = pdf_dict_of(d, dict);
  if (!dict) return NULL;
  if (!(f = (PdfFont *)calloc(1, sizeof *f))) return NULL;
  f->matrix = mat_make(0.001, 0, 0, 0.001, 0, 0);
  sub = pdf_dname(d, dict, "Subtype");
  bf = pdf_dname(d, dict, "BaseFont");
  strip_subset(bf ? bf : "", f->name, sizeof f->name);
  if (sub && !strcmp(sub, "Type3")) load_type3(d, f, dict);
  else if (sub && !strcmp(sub, "Type0")) load_type0(d, f, dict);
  else load_simple(d, f, dict, sub);
  load_tounicode(d, dict, &f->tounicode);
  return f;
}
void font_free(PdfFont *f)
{
  if (!f) return;
  if (f->own_prog) pf_free(f->prog);
  cmap_free(f->encoding);
  cmap_free(f->tounicode);
  free(f->cw); free(f->cid2gid);
  free(f);
}
int font_next(const PdfFont *f, const u8 *s, int len, u32 *code)
{
  if (f->kind == PFT_TYPE0 && f->encoding) return cmap_next(f->encoding, s, len, code);
  *code = len > 0 ? s[0] : 0;
  return 1;
}
/* a composite font's CID for a code */
static int cid_of(const PdfFont *f, u32 code, int nbytes) { return f->encoding ? cmap_cid(f->encoding, code, nbytes) : (int)code; }
int font_gid(const PdfFont *f, u32 code, int nbytes)
{
  if (f->kind == PFT_TYPE3) return code < 256 ? (int)code : -1;
  if (f->kind == PFT_TYPE0) {
    int cid = cid_of(f, code, nbytes);
    if (f->substitute) {
      /* a stand-in for a composite font: through the text */
      u32 u[4];
      int n = font_text(f, code, nbytes, u, 4);
      return n && f->prog ? pf_cmap(f->prog, 3, 1, u[0]) : 0;
    }
    if (f->cid2gid) return cid >= 0 && cid < f->ncid2gid ? f->cid2gid[cid] : 0;
    if (f->cid_cff) return pf_cid_to_gid(f->prog, cid);
    return cid;
  }
  return code < 256 ? f->gid[code] : 0;
}
double font_width(const PdfFont *f, u32 code, int nbytes)
{
  if (f->kind == PFT_TYPE0) {
    int cid = cid_of(f, code, nbytes), i;
    for (i = 0; i < f->ncw; i++) if (cid >= f->cw[i].lo && cid <= f->cw[i].hi) return f->cw[i].w;
    return f->dw;
  }
  return code < 256 ? f->width[code] : 0;
}
int font_text(const PdfFont *f, u32 code, int nbytes, u32 *out, int max)
{
  int n;
  if (f->tounicode && (n = cmap_unicode(f->tounicode, code, nbytes, out, max)) > 0) return n;
  if (f->kind == PFT_TYPE0) {
    /* without a ToUnicode CMap: a CFF's glyph name, if it has one */
    int g = font_gid(f, code, nbytes);
    const char *nm = f->prog && g > 0 && !f->cid_cff ? pf_glyph_name(f->prog, g) : NULL;
    u32 u = nm ? agl_unicode(nm) : 0;
    if (u && max > 0) { out[0] = u; return 1; }
    return 0;
  }
  if (code < 256 && f->uni[code] && max > 0) { out[0] = f->uni[code]; return 1; }
  return 0;
}
