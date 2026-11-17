/*
 * pfont.c - font programs embedded in PDFs (see pfont.h): TrueType glyf outlines with composites, CFF Type 2
 * charstrings (name-keyed or CID-keyed, bare or inside OpenType) and Type 1 (PFA/PFB, eexec, Type 1 charstrings).
 * Everything works on the font's own copy in memory and checks every offset and length against it, since a font
 * inside a PDF can be anything; subroutine nesting, stacks and the work done for one glyph are all bounded.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pfont.h"
#include "pdfenc.h"

#define MAXDEPTH 10              /* subroutine nesting (Type 2's own limit) */
#define MAXOPS 200000L           /* operators run for one glyph (subroutines included): against subroutine bombs */
#define MAXPTS 50000             /* TrueType points in one glyph, its components included */
#define MAXPARTS 20000           /* TrueType glyphs visited for one glyph (composites of composites) */
#define MAXSEGS 50000L           /* lines and curves in one glyph */
#define MAXGLYPHS 65535
#define MAXFD 256                /* font DICTs of a CID-keyed CFF */

typedef struct { u32 lsub; double defw, nomw; } CffPriv;    /* lsub: the local Subrs INDEX (0: none) */
typedef struct { double x, y; u8 on; } TTPt;
typedef struct { const char *n; int gid; } NameEnt;

struct PFont {
  int kind, nglyphs;
  u8 *data; u32 len;               /* the font program (Type 1: the clear text, then the decrypted private part) */
  double fm[6];
  /* sfnt: TrueType, or OpenType around a CFF */
  int sfnt; u32 base;              /* the table directory's offset (a collection's fonts don't start at 0) */
  u32 glyf, lglyf, loca, lloca, hmtx, lhmtx, cmap, lcmap, post, lpost;
  int upm, longloca, nhmetrics;
  TTPt *pts; int npts, cappts;     /* work space for a TrueType glyph */
  int parts;                       /* glyphs visited for it */
  int *ends; int nends, capends;
  /* CFF */
  const u8 *cff; u32 lcff;
  u32 cs_idx, gsub_idx, str_idx, nstr;
  CffPriv priv, *fds; int nfd; u32 fdsel;
  int cid;
  u16 *charset;                    /* glyph id -> SID (name-keyed) or CID */
  u32 *cidmap;                     /* CID << 16 | glyph id, sorted (CID-keyed) */
  /* Type 1 */
  u32 *t1cs, *t1sub; int nsub;     /* charstrings and Subrs: offset, length pairs into data (already decrypted) */
  /* encodings and names */
  int enc[256], has_enc;
  char *namebuf; const char **names; int names_done;
  NameEnt *byname; int nbyname;
};

/* CFF's standard strings: the names of SIDs 0-390 */
static const char *const cff_std_strings[391] = {
  ".notdef", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quoteright", "parenleft", "parenright",
  "asterisk", "plus", "comma", "hyphen", "period", "slash", "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
  "colon", "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
  "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",
  "quoteleft", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y",
  "z", "braceleft", "bar", "braceright", "asciitilde", "exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section", "currency",
  "quotesingle", "quotedblleft", "guillemotleft", "guilsinglleft", "guilsinglright", "fi", "fl", "endash", "dagger", "daggerdbl",
  "periodcentered", "paragraph", "bullet", "quotesinglbase", "quotedblbase", "quotedblright", "guillemotright", "ellipsis", "perthousand",
  "questiondown", "grave", "acute", "circumflex", "tilde", "macron", "breve", "dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut",
  "ogonek", "caron", "emdash", "AE", "ordfeminine", "Lslash", "Oslash", "OE", "ordmasculine", "ae", "dotlessi", "lslash", "oslash", "oe",
  "germandbls", "onesuperior", "logicalnot", "mu", "trademark", "Eth", "onehalf", "plusminus", "Thorn", "onequarter", "divide", "brokenbar",
  "degree", "thorn", "threequarters", "twosuperior", "registered", "minus", "eth", "multiply", "threesuperior", "copyright", "Aacute",
  "Acircumflex", "Adieresis", "Agrave", "Aring", "Atilde", "Ccedilla", "Eacute", "Ecircumflex", "Edieresis", "Egrave", "Iacute",
  "Icircumflex", "Idieresis", "Igrave", "Ntilde", "Oacute", "Ocircumflex", "Odieresis", "Ograve", "Otilde", "Scaron", "Uacute",
  "Ucircumflex", "Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron", "aacute", "acircumflex", "adieresis", "agrave", "aring", "atilde",
  "ccedilla", "eacute", "ecircumflex", "edieresis", "egrave", "iacute", "icircumflex", "idieresis", "igrave", "ntilde", "oacute",
  "ocircumflex", "odieresis", "ograve", "otilde", "scaron", "uacute", "ucircumflex", "udieresis", "ugrave", "yacute", "ydieresis", "zcaron",
  "exclamsmall", "Hungarumlautsmall", "dollaroldstyle", "dollarsuperior", "ampersandsmall", "Acutesmall", "parenleftsuperior",
  "parenrightsuperior", "twodotenleader", "onedotenleader", "zerooldstyle", "oneoldstyle", "twooldstyle", "threeoldstyle", "fouroldstyle",
  "fiveoldstyle", "sixoldstyle", "sevenoldstyle", "eightoldstyle", "nineoldstyle", "commasuperior", "threequartersemdash", "periodsuperior",
  "questionsmall", "asuperior", "bsuperior", "centsuperior", "dsuperior", "esuperior", "isuperior", "lsuperior", "msuperior", "nsuperior",
  "osuperior", "rsuperior", "ssuperior", "tsuperior", "ff", "ffi", "ffl", "parenleftinferior", "parenrightinferior", "Circumflexsmall",
  "hyphensuperior", "Gravesmall", "Asmall", "Bsmall", "Csmall", "Dsmall", "Esmall", "Fsmall", "Gsmall", "Hsmall", "Ismall", "Jsmall",
  "Ksmall", "Lsmall", "Msmall", "Nsmall", "Osmall", "Psmall", "Qsmall", "Rsmall", "Ssmall", "Tsmall", "Usmall", "Vsmall", "Wsmall",
  "Xsmall", "Ysmall", "Zsmall", "colonmonetary", "onefitted", "rupiah", "Tildesmall", "exclamdownsmall", "centoldstyle", "Lslashsmall",
  "Scaronsmall", "Zcaronsmall", "Dieresissmall", "Brevesmall", "Caronsmall", "Dotaccentsmall", "Macronsmall", "figuredash",
  "hypheninferior", "Ogoneksmall", "Ringsmall", "Cedillasmall", "questiondownsmall", "oneeighth", "threeeighths", "fiveeighths",
  "seveneighths", "onethird", "twothirds", "zerosuperior", "foursuperior", "fivesuperior", "sixsuperior", "sevensuperior", "eightsuperior",
  "ninesuperior", "zeroinferior", "oneinferior", "twoinferior", "threeinferior", "fourinferior", "fiveinferior", "sixinferior",
  "seveninferior", "eightinferior", "nineinferior", "centinferior", "dollarinferior", "periodinferior", "commainferior", "Agravesmall",
  "Aacutesmall", "Acircumflexsmall", "Atildesmall", "Adieresissmall", "Aringsmall", "AEsmall", "Ccedillasmall", "Egravesmall",
  "Eacutesmall", "Ecircumflexsmall", "Edieresissmall", "Igravesmall", "Iacutesmall", "Icircumflexsmall", "Idieresissmall", "Ethsmall",
  "Ntildesmall", "Ogravesmall", "Oacutesmall", "Ocircumflexsmall", "Otildesmall", "Odieresissmall", "OEsmall", "Oslashsmall", "Ugravesmall",
  "Uacutesmall", "Ucircumflexsmall", "Udieresissmall", "Yacutesmall", "Thornsmall", "Ydieresissmall", "001.000", "001.001", "001.002",
  "001.003", "Black", "Bold", "Book", "Light", "Medium", "Regular", "Roman", "Semibold",
};
/* the predefined Expert charset (glyph id -> SID) */
static const u16 cff_expert_charset[166] = {
  0, 1, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 13, 14, 15, 99, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 27, 28, 249,
  250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 109, 110, 267, 268, 269, 270, 271, 272, 273, 274,
  275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301,
  302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 158, 155, 163, 319, 320, 321, 322, 323, 324, 325,
  326, 150, 164, 169, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349,
  350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376,
  377, 378,
};
/* the predefined ExpertSubset charset */
static const u16 cff_expertsub_charset[87] = {
  0, 1, 231, 232, 235, 236, 237, 238, 13, 14, 15, 99, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 27, 28, 249, 250, 251, 253, 254,
  255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 109, 110, 267, 268, 269, 270, 272, 300, 301, 302, 305, 314, 315, 158, 155,
  163, 320, 321, 322, 323, 324, 325, 326, 150, 164, 169, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342,
  343, 344, 345, 346,
};
/* the predefined Expert encoding (code -> SID, 0 none) */
static const u16 cff_expert_encoding[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 229, 230, 0, 231, 232, 233, 234, 235,
  236, 237, 238, 13, 14, 15, 99, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 27, 28, 249, 250, 251, 252, 0, 253, 254, 255, 256, 257,
  0, 0, 0, 258, 0, 0, 259, 260, 261, 262, 0, 0, 263, 264, 265, 0, 266, 109, 110, 267, 268, 269, 0, 270, 271, 272, 273, 274, 275, 276, 277,
  278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 304, 305, 306, 0, 0, 307, 308, 309, 310, 311,
  0, 312, 0, 0, 313, 0, 0, 314, 315, 0, 0, 316, 317, 318, 0, 0, 0, 158, 155, 163, 319, 320, 321, 322, 323, 324, 325, 0, 0, 326, 150, 164,
  169, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352,
  353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 378,
};
/* the Macintosh glyph order: TrueType 'post' names 0-257 */
static const char *const mac_glyph_names[258] = {
  ".notdef", ".null", "nonmarkingreturn", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle",
  "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash", "zero", "one", "two", "three", "four", "five", "six",
  "seven", "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E", "F", "G", "H", "I",
  "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "bracketleft", "backslash", "bracketright",
  "asciicircum", "underscore", "grave", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
  "u", "v", "w", "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde", "Adieresis", "Aring", "Ccedilla", "Eacute", "Ntilde",
  "Odieresis", "Udieresis", "aacute", "agrave", "acircumflex", "adieresis", "atilde", "aring", "ccedilla", "eacute", "egrave",
  "ecircumflex", "edieresis", "iacute", "igrave", "icircumflex", "idieresis", "ntilde", "oacute", "ograve", "ocircumflex", "odieresis",
  "otilde", "uacute", "ugrave", "ucircumflex", "udieresis", "dagger", "degree", "cent", "sterling", "section", "bullet", "paragraph",
  "germandbls", "registered", "copyright", "trademark", "acute", "dieresis", "notequal", "AE", "Oslash", "infinity", "plusminus",
  "lessequal", "greaterequal", "yen", "mu", "partialdiff", "summation", "product", "pi", "integral", "ordfeminine", "ordmasculine", "Omega",
  "ae", "oslash", "questiondown", "exclamdown", "logicalnot", "radical", "florin", "approxequal", "Delta", "guillemotleft",
  "guillemotright", "ellipsis", "nonbreakingspace", "Agrave", "Atilde", "Otilde", "OE", "oe", "endash", "emdash", "quotedblleft",
  "quotedblright", "quoteleft", "quoteright", "divide", "lozenge", "ydieresis", "Ydieresis", "fraction", "currency", "guilsinglleft",
  "guilsinglright", "fi", "fl", "daggerdbl", "periodcentered", "quotesinglbase", "quotedblbase", "perthousand", "Acircumflex",
  "Ecircumflex", "Aacute", "Edieresis", "Egrave", "Iacute", "Icircumflex", "Idieresis", "Igrave", "Oacute", "Ocircumflex", "apple",
  "Ograve", "Uacute", "Ucircumflex", "Ugrave", "dotlessi", "circumflex", "tilde", "macron", "breve", "dotaccent", "ring", "cedilla",
  "hungarumlaut", "ogonek", "caron", "Lslash", "lslash", "Scaron", "scaron", "Zcaron", "zcaron", "brokenbar", "Eth", "eth", "Yacute",
  "yacute", "Thorn", "thorn", "minus", "multiply", "onesuperior", "twosuperior", "threesuperior", "onehalf", "onequarter", "threequarters",
  "franc", "Gbreve", "gbreve", "Idotaccent", "Scedilla", "scedilla", "Cacute", "cacute", "Ccaron", "ccaron", "dcroat",
};

/* ------------------------------------------------------------------ */
/* small helpers */

static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static i32 sbe16(const u8 *p) { i32 v = (i32)be16(p); return v >= 32768 ? v - 65536 : v; }
static double f2dot14(const u8 *p) { return sbe16(p) / 16384.0; }
/* a message into err (if there is room for one); always NULL, for "return fail(...)" */
static PFont *fail(char *err, size_t errcap, const char *msg)
{
  if (err && errcap) { strncpy(err, msg, errcap - 1); err[errcap - 1] = 0; }
  return NULL;
}
/* a coordinate kept sane: NaN and the absurdly large (division by tiny numbers in a hostile charstring) become 0 */
static double sane(double v) { return (v == v && v < 1e7 && v > -1e7) ? v : 0; }
/* off and n lie within a block of len bytes */
static int within(u32 off, u32 n, u32 len) { return off <= len && n <= len - off; }

/* ------------------------------------------------------------------ */
/* the pen: moves wait until something is drawn, so lone moves leave nothing; (dx, dy) shifts an accent */
typedef struct { Path *p; double x, y, dx, dy; int open, over; long segs; } Pen;   /* over: past MAXSEGS */
static void pen_close(Pen *g)
{
  if (g->open && g->p) path_close(g->p);
  g->open = 0;
}
static void pen_move(Pen *g, double x, double y) { pen_close(g); g->x = x; g->y = y; }
static void pen_start(Pen *g)
{
  if (++g->segs > MAXSEGS) { g->over = 1; return; }
  if (!g->open && g->p) path_move(g->p, sane(g->x + g->dx), sane(g->y + g->dy));
  g->open = 1;
}
static void pen_line(Pen *g, double x, double y)
{
  pen_start(g);
  if (g->p && !g->over) path_line(g->p, sane(x + g->dx), sane(y + g->dy));
  g->x = x; g->y = y;
}
static void pen_curve(Pen *g, double x1, double y1, double x2, double y2, double x3, double y3)
{
  pen_start(g);
  if (g->p && !g->over) path_curve(g->p, sane(x1 + g->dx), sane(y1 + g->dy), sane(x2 + g->dx), sane(y2 + g->dy), sane(x3 + g->dx), sane(y3 + g->dy));
  g->x = x3; g->y = y3;
}

/* ------------------------------------------------------------------ */
/* sfnt: the table directory, TrueType outlines, metrics, cmap and post */

/* the table tag in the directory at f->base: its offset and length (checked against the data); 0 if missing */
static int sfnt_table(const PFont *f, const char *tag, u32 *off, u32 *len)
{
  u32 n, i;
  if (!within(f->base, 12, f->len)) return 0;
  n = be16(f->data + f->base + 4);
  for (i = 0; i < n && within(f->base + 12 + 16 * i, 16, f->len); i++) {
    const u8 *e = f->data + f->base + 12 + 16 * i;
    if (!memcmp(e, tag, 4)) {
      u32 o = be32(e + 8), l = be32(e + 12);
      if (!within(o, 0, f->len)) return 0;
      if (l > f->len - o) l = f->len - o;               /* (a table cut short: what there is) */
      *off = o; *len = l;
      return 1;
    }
  }
  return 0;
}
/* room for n more points and c more contour ends in the work space */
static int tt_grow(PFont *f, int n, int c)
{
  if (f->npts + n > MAXPTS || f->nends + c > MAXPTS) return 0;
  if (f->npts + n > f->cappts) {
    int nc = f->cappts ? f->cappts : 256;
    TTPt *np;
    while (nc < f->npts + n) nc *= 2;
    if (!(np = (TTPt *)realloc(f->pts, (size_t)nc * sizeof(TTPt)))) return 0;
    f->pts = np; f->cappts = nc;
  }
  if (f->nends + c > f->capends) {
    int nc = f->capends ? f->capends : 64;
    int *ne;
    while (nc < f->nends + c) nc *= 2;
    if (!(ne = (int *)realloc(f->ends, (size_t)nc * sizeof(int)))) return 0;
    f->ends = ne; f->capends = nc;
  }
  return 1;
}
/* glyph gid's points added to the work space (in its own units), its components moved and transformed into place */
static int tt_points(PFont *f, int gid, int depth)
{
  u32 o0, o1, len;
  const u8 *p, *end, *q;
  i32 nc;
  if (gid < 0 || gid >= f->nglyphs || depth > 8 || ++f->parts > MAXPARTS) return 0;
  if (f->longloca) { o0 = be32(f->data + f->loca + 4 * gid); o1 = be32(f->data + f->loca + 4 * gid + 4); }
  else { o0 = 2 * be16(f->data + f->loca + 2 * gid); o1 = 2 * be16(f->data + f->loca + 2 * gid + 2); }
  if (o1 <= o0) return 1;                                     /* nothing to draw */
  if (o1 > f->lglyf) return 0;
  len = o1 - o0;
  if (len < 10) return 0;
  p = f->data + f->glyf + o0; end = p + len;
  nc = sbe16(p);
  q = p + 10;
  if (nc >= 0) {
    int i, n, base = f->npts, k, prev = -1;
    double v;
    if (end - q < 2 * nc + 2) return 0;
    n = nc ? (int)be16(q + 2 * (nc - 1)) + 1 : 0;
    if (!tt_grow(f, n, nc)) return 0;
    for (i = 0; i < nc; i++) {
      int e = (int)be16(q + 2 * i);
      if (e >= n || e < prev) return 0;
      f->ends[f->nends + i] = base + e;
      prev = e;
    }
    q += 2 * nc;
    q += 2 + be16(q);                                         /* (the instructions) */
    if (q > end) return 0;
    /* flags (with repeats), then x and y, each short (1 byte and a sign) or long, or the same as before */
    for (i = 0; i < n; ) {
      u8 fl;
      if (q >= end) return 0;
      fl = *q++;
      f->pts[base + i++].on = fl;
      if (fl & 8) {
        if (q >= end) return 0;
        for (k = *q++; k && i < n; k--) f->pts[base + i++].on = fl;
      }
    }
    for (i = 0, v = 0; i < n; i++) {
      u8 fl = f->pts[base + i].on;
      if (fl & 2) { if (q >= end) return 0; v += fl & 16 ? *q : -(double)*q; q++; }
      else if (!(fl & 16)) { if (end - q < 2) return 0; v += sbe16(q); q += 2; }
      f->pts[base + i].x = v;
    }
    for (i = 0, v = 0; i < n; i++) {
      u8 fl = f->pts[base + i].on;
      if (fl & 4) { if (q >= end) return 0; v += fl & 32 ? *q : -(double)*q; q++; }
      else if (!(fl & 32)) { if (end - q < 2) return 0; v += sbe16(q); q += 2; }
      f->pts[base + i].y = v;
      f->pts[base + i].on = fl & 1;
    }
    f->npts += n; f->nends += nc;
    return 1;
  } else {
    /* a composite: other glyphs, each transformed and moved (by an offset or by matching two points) */
    int start = f->npts, i;
    u32 flags;
    do {
      double a = 1, b = 0, c = 0, d = 1, dx, dy;
      i32 e1, e2;
      int sub, cb;
      if (end - q < 4) return 0;
      flags = be16(q); sub = (int)be16(q + 2); q += 4;
      if (flags & 1) {
        if (end - q < 4) return 0;
        if (flags & 2) { e1 = sbe16(q); e2 = sbe16(q + 2); } else { e1 = (i32)be16(q); e2 = (i32)be16(q + 2); }
        q += 4;
      } else {
        if (end - q < 2) return 0;
        if (flags & 2) { e1 = (signed char)q[0]; e2 = (signed char)q[1]; } else { e1 = q[0]; e2 = q[1]; }
        q += 2;
      }
      if (flags & 8) { if (end - q < 2) return 0; a = d = f2dot14(q); q += 2; }
      else if (flags & 0x40) { if (end - q < 4) return 0; a = f2dot14(q); d = f2dot14(q + 2); q += 4; }
      else if (flags & 0x80) { if (end - q < 8) return 0; a = f2dot14(q); b = f2dot14(q + 2); c = f2dot14(q + 4); d = f2dot14(q + 6); q += 8; }
      cb = f->npts;
      if (!tt_points(f, sub, depth + 1)) return 0;
      for (i = cb; i < f->npts; i++) {
        double x = f->pts[i].x, y = f->pts[i].y;
        f->pts[i].x = a * x + c * y; f->pts[i].y = b * x + d * y;
      }
      if (flags & 2) {
        dx = e1; dy = e2;
        if ((flags & 0x800) && !(flags & 0x1000)) { double t = a * dx + c * dy; dy = b * dx + d * dy; dx = t; }   /* (Apple's way) */
      } else if (e1 < cb - start && e2 < f->npts - cb) {
        dx = f->pts[start + e1].x - f->pts[cb + e2].x;         /* matching points: one of ours onto one of its */
        dy = f->pts[start + e1].y - f->pts[cb + e2].y;
      } else dx = dy = 0;
      for (i = cb; i < f->npts; i++) { f->pts[i].x += dx; f->pts[i].y += dy; }
    } while (flags & 0x20);
    return 1;
  }
}
/* the work space's contours into the path: quadratic pieces raised to cubic, off-curve pairs meeting halfway */
static void tt_to_path(PFont *f, Path *out)
{
  int c, s = 0;
  for (c = 0; c < f->nends; c++) {
    int e = f->ends[c], n = e - s + 1, i, first;
    double sx, sy, cx = 0, cy = 0, px, py;
    int haveoff = 0;
    if (n <= 0) { s = e + 1; continue; }
    if (f->pts[s].on) { sx = f->pts[s].x; sy = f->pts[s].y; first = 1; }
    else if (f->pts[e].on) { sx = f->pts[e].x; sy = f->pts[e].y; first = 0; n--; }
    else { sx = (f->pts[s].x + f->pts[e].x) / 2; sy = (f->pts[s].y + f->pts[e].y) / 2; first = 0; }
    path_move(out, sx, sy);
    px = sx; py = sy;
    for (i = first; i <= n; i++) {
      double x, y;
      int on;
      if (i == n) { x = sx; y = sy; on = 1; }                 /* (back to the start) */
      else { x = f->pts[s + i].x; y = f->pts[s + i].y; on = f->pts[s + i].on; }
      if (on) {
        if (haveoff) {
          path_curve(out, px + 2 * (cx - px) / 3, py + 2 * (cy - py) / 3, x + 2 * (cx - x) / 3, y + 2 * (cy - y) / 3, x, y);
          haveoff = 0;
        } else if (i < n) path_line(out, x, y);
        px = x; py = y;
      } else {
        if (haveoff) {
          double mx = (cx + x) / 2, my = (cy + y) / 2;
          path_curve(out, px + 2 * (cx - px) / 3, py + 2 * (cy - py) / 3, mx + 2 * (cx - mx) / 3, my + 2 * (cy - my) / 3, mx, my);
          px = mx; py = my;
        }
        cx = x; cy = y; haveoff = 1;
      }
    }
    path_close(out);
    s = e + 1;
  }
}
/* a TrueType glyph's outline, moved so that its left edge is where hmtx's left side bearing says (as FreeType and
   fontTools do; they almost always agree already) */
static int tt_glyph(PFont *f, int gid, Path *out)
{
  u32 o0, o1, lo;
  int i;
  f->npts = 0; f->nends = 0; f->parts = 0;
  if (!f->glyf || !tt_points(f, gid, 0)) return 0;
  if (f->longloca) { o0 = be32(f->data + f->loca + 4 * gid); o1 = be32(f->data + f->loca + 4 * gid + 4); }
  else { o0 = 2 * be16(f->data + f->loca + 2 * gid); o1 = 2 * be16(f->data + f->loca + 2 * gid + 2); }
  lo = gid < f->nhmetrics ? 4 * (u32)gid + 2 : 4 * (u32)f->nhmetrics + 2 * (u32)(gid - f->nhmetrics);
  if (f->npts && o1 > o0 + 4 && f->nhmetrics && lo + 2 <= f->lhmtx) {
    double dx = sbe16(f->data + f->hmtx + lo) - sbe16(f->data + f->glyf + o0 + 2);
    for (i = 0; i < f->npts; i++) f->pts[i].x += dx;
  }
  tt_to_path(f, out);
  return 1;
}
/* a cmap subtable: the glyph for code; -1 if its format isn't one read here */
static int cmap_lookup(const PFont *f, u32 off, u32 code)
{
  const u8 *t = f->data + off;
  u32 left = f->cmap + f->lcmap - off, fmt;
  if (left < 4) return -1;
  fmt = be16(t);
  if (fmt == 0) {
    if (left < 6 + 256) return -1;
    return code < 256 ? (int)t[6 + code] : 0;
  }
  if (fmt == 4) {
    u32 segx2, lo, hi, n;
    if (left < 14) return -1;
    segx2 = be16(t + 6) & ~1u; n = segx2 / 2;
    if (left < 16 + 4 * segx2) return -1;
    if (code > 0xFFFF) return 0;
    /* the first segment whose end is at or past code */
    lo = 0; hi = n;
    while (lo < hi) {
      u32 mid = (lo + hi) / 2;
      if (be16(t + 14 + 2 * mid) < code) lo = mid + 1; else hi = mid;
    }
    if (lo >= n) return 0;
    {
      u32 start = be16(t + 16 + segx2 + 2 * lo), delta = be16(t + 16 + 2 * segx2 + 2 * lo);
      u32 ro = 16 + 3 * segx2 + 2 * lo, rofs = be16(t + ro), g;
      if (code < start) return 0;
      if (!rofs) return (int)((code + delta) & 0xFFFF);
      g = ro + rofs + 2 * (code - start);
      if (g + 2 > left) return 0;
      g = be16(t + g);
      return g ? (int)((g + delta) & 0xFFFF) : 0;
    }
  }
  if (fmt == 6) {
    u32 first, cnt;
    if (left < 10) return -1;
    first = be16(t + 6); cnt = be16(t + 8);
    if (code < first || code - first >= cnt || 10 + 2 * (code - first) + 2 > left) return 0;
    return (int)be16(t + 10 + 2 * (code - first));
  }
  if (fmt == 10) {
    u32 first, cnt;
    if (left < 20) return -1;
    first = be32(t + 12); cnt = be32(t + 16);
    if (code < first || code - first >= cnt || code - first >= (left - 20) / 2) return 0;
    return (int)be16(t + 20 + 2 * (code - first));
  }
  if (fmt == 12 || fmt == 13) {
    u32 n, lo, hi;
    if (left < 16) return -1;
    n = be32(t + 12);
    if (n > (left - 16) / 12) n = (left - 16) / 12;
    lo = 0; hi = n;
    while (lo < hi) {
      u32 mid = (lo + hi) / 2;
      const u8 *g = t + 16 + 12 * mid;
      if (code < be32(g)) hi = mid;
      else if (code > be32(g + 4)) lo = mid + 1;
      else {
        u32 gid = fmt == 12 ? be32(g + 8) + (code - be32(g)) : be32(g + 8);
        return gid > 0xFFFF ? 0 : (int)gid;
      }
    }
    return 0;
  }
  return -1;
}
/* the glyph for code in the (platform, encoding) subtable: the first such subtable of a format read here */
int pf_cmap(const PFont *f, int platform, int encoding, u32 code)
{
  u32 n, i;
  if (!f || !f->cmap || f->lcmap < 4) return -1;
  n = be16(f->data + f->cmap + 2);
  for (i = 0; i < n && 4 + 8 * i + 8 <= f->lcmap; i++) {
    const u8 *r = f->data + f->cmap + 4 + 8 * i;
    u32 off = be32(r + 4);
    int g;
    if ((int)be16(r) != platform || (int)be16(r + 2) != encoding || off >= f->lcmap) continue;
    if ((g = cmap_lookup(f, f->cmap + off, code)) >= 0) return g < f->nglyphs ? g : 0;
  }
  return -1;
}

/* ------------------------------------------------------------------ */
/* CFF: INDEXes (a count, an offset size, count + 1 offsets from 1, the data) and DICTs */

/* the INDEX at off: its count, and where it ends; 0 if it runs past the table */
static int idx_info(const PFont *f, u32 off, u32 *count, u32 *end)
{
  u32 n, os, last = 0, i, base;
  if (!within(off, 2, f->lcff)) return 0;
  n = be16(f->cff + off);
  if (!n) { *count = 0; *end = off + 2; return 1; }
  if (!within(off, 3, f->lcff)) return 0;
  os = f->cff[off + 2];
  if (os < 1 || os > 4 || !within(off + 3, (n + 1) * os, f->lcff)) return 0;
  for (i = 0; i < os; i++) last = (last << 8) | f->cff[off + 3 + n * os + i];
  base = off + 3 + (n + 1) * os - 1;
  if (last < 1 || !within(base, last, f->lcff)) return 0;
  *count = n; *end = base + last;
  return 1;
}
/* entry i of the INDEX at off: where it starts and how long it is */
static int idx_get(const PFont *f, u32 off, u32 i, u32 *at, u32 *len)
{
  u32 n, os, a = 0, b = 0, k, base;
  if (!within(off, 3, f->lcff)) return 0;
  n = be16(f->cff + off);
  os = f->cff[off + 2];
  if (i >= n || os < 1 || os > 4 || !within(off + 3, (n + 1) * os, f->lcff)) return 0;
  for (k = 0; k < os; k++) {
    a = (a << 8) | f->cff[off + 3 + i * os + k];
    b = (b << 8) | f->cff[off + 3 + (i + 1) * os + k];
  }
  base = off + 3 + (n + 1) * os - 1;
  if (a < 1 || b < a || !within(base, b, f->lcff)) return 0;
  *at = base + a; *len = b - a;
  return 1;
}
/* a real number in a DICT (packed decimal nibbles); p moves past it */
static double dict_real(const u8 **pp, const u8 *end)
{
  static const char nib[] = "0123456789.EE?-";
  char s[48];
  int n = 0, done = 0;
  const u8 *p = *pp;
  while (p < end && !done) {
    int k, h[2];
    h[0] = *p >> 4; h[1] = *p & 15; p++;
    for (k = 0; k < 2 && !done; k++) {
      if (h[k] == 15) done = 1;
      else if (n < 44) {
        s[n++] = nib[h[k]];
        if (h[k] == 12) s[n++] = '-';
      }
    }
  }
  s[n] = 0;
  *pp = p;
  return strtod(s, NULL);
}
/* the operands of operator op (12 x as 1200 + x) in the DICT at at (len bytes): up to nv into v; how many there
   were (0 if op isn't there) */
static int dict_get(const PFont *f, u32 at, u32 len, int op, double *v, int nv)
{
  const u8 *p, *end;
  double st[48];
  int n = 0, i;
  if (!within(at, len, f->lcff)) return 0;
  p = f->cff + at; end = p + len;
  while (p < end) {
    int b = *p++;
    double x;
    if (b >= 32 && b <= 246) x = b - 139;
    else if (b >= 247 && b <= 250) { if (p >= end) return 0; x = (b - 247) * 256 + *p++ + 108; }
    else if (b >= 251 && b <= 254) { if (p >= end) return 0; x = -(b - 251) * 256 - *p++ - 108; }
    else if (b == 28) { if (end - p < 2) return 0; x = sbe16(p); p += 2; }
    else if (b == 29) { if (end - p < 4) return 0; x = (double)(i32)be32(p); p += 4; }
    else if (b == 30) x = dict_real(&p, end);
    else {
      int o = b;
      if (b == 12) { if (p >= end) return 0; o = 1200 + *p++; }
      if (o == op) { for (i = 0; i < nv && i < n; i++) v[i] = st[i]; return n ? n : -1; }
      n = 0;
      continue;
    }
    if (n < 48) st[n++] = x;
  }
  return 0;
}
/* an offset operand: a non-negative whole number that fits the table; 0 otherwise */
static u32 dict_off(const PFont *f, u32 at, u32 len, int op)
{
  double v[1];
  if (dict_get(f, at, len, op, v, 1) < 1 || !(v[0] > 0) || v[0] >= (double)f->lcff) return 0;
  return (u32)v[0];
}
/* a Private DICT (size, offset from the Top DICT or a font DICT): its local Subrs and widths */
static void cff_private(const PFont *f, u32 at, u32 len, CffPriv *pv)
{
  double v[2];
  u32 cnt, e;
  pv->lsub = 0; pv->defw = 0; pv->nomw = 0;
  if (dict_get(f, at, len, 18, v, 2) < 2 || !(v[0] >= 0) || !(v[1] > 0) || v[1] >= f->lcff || v[0] > f->lcff) return;
  at = (u32)v[1]; len = (u32)v[0];
  if (!within(at, len, f->lcff)) return;
  if (dict_get(f, at, len, 20, v, 1) > 0) pv->defw = v[0];
  if (dict_get(f, at, len, 21, v, 1) > 0) pv->nomw = v[0];
  if (dict_get(f, at, len, 19, v, 1) > 0 && v[0] > 0 && v[0] < f->lcff) {
    u32 o = at + (u32)v[0];
    if (o < f->lcff && idx_info(f, o, &cnt, &e)) pv->lsub = o;
  }
}
/* a FontMatrix operand into m; 0 if absent or degenerate */
static int dict_matrix(const PFont *f, u32 at, u32 len, double m[6])
{
  double v[6];
  int i;
  if (dict_get(f, at, len, 1207, v, 6) != 6) return 0;
  for (i = 0; i < 6; i++) if (!(v[i] == v[i]) || fabs(v[i]) > 1e6) return 0;
  if (fabs(v[0] * v[3] - v[1] * v[2]) < 1e-12) return 0;
  memcpy(m, v, sizeof v);
  return 1;
}
/* the SID's string (standard or in the String INDEX): its bytes and length; 0 if there's no such string */
static int cff_string(const PFont *f, u32 sid, const u8 **s, u32 *len)
{
  u32 at;
  if (sid < 391) { *s = (const u8 *)cff_std_strings[sid]; *len = (u32)strlen(cff_std_strings[sid]); return 1; }
  if (!idx_get(f, f->str_idx, sid - 391, &at, len)) return 0;
  *s = f->cff + at;
  return 1;
}
/* the glyph whose charset SID is sid; -1 if none */
static int cff_sid_gid(const PFont *f, u32 sid)
{
  int g;
  for (g = 0; g < f->nglyphs; g++) if (f->charset[g] == sid) return g;
  return -1;
}
/* sorts cidmap entries (CID << 16 | glyph id) */
static int cmp_u32(const void *a, const void *b)
{
  u32 x = *(const u32 *)a, y = *(const u32 *)b;
  return x < y ? -1 : x > y;
}
/* the charset (glyph id -> SID or CID): predefined (0-2) or formats 0, 1 and 2. Glyphs it misses keep 0 */
static int cff_charset(PFont *f, u32 off)
{
  int g, n = f->nglyphs;
  if (!(f->charset = (u16 *)calloc((size_t)n, sizeof(u16)))) return 0;
  if (off <= 2 && !f->cid) {
    const u16 *t = off == 1 ? cff_expert_charset : cff_expertsub_charset;
    int tn = off == 1 ? (int)(sizeof cff_expert_charset / sizeof(u16)) : (int)(sizeof cff_expertsub_charset / sizeof(u16));
    for (g = 0; g < n; g++) f->charset[g] = off == 0 ? (u16)(g < 229 ? g : 0) : (u16)(g < tn ? t[g] : 0);
    return 1;
  }
  if (off > 2 && off < f->lcff) {
    const u8 *p = f->cff + off, *end = f->cff + f->lcff;
    int fmt = *p++;
    g = 1;
    if (fmt == 0) {
      for (; g < n && end - p >= 2; g++, p += 2) f->charset[g] = (u16)be16(p);
    } else if (fmt == 1 || fmt == 2) {
      while (g < n) {
        u32 first, left, k;
        if (end - p < (fmt == 1 ? 3 : 4)) break;
        first = be16(p); left = fmt == 1 ? p[2] : be16(p + 2);
        p += fmt == 1 ? 3 : 4;
        for (k = 0; k <= left && g < n; k++, g++) f->charset[g] = (u16)((first + k) & 0xFFFF);
      }
    }
  }
  return 1;
}
/* the built-in encoding (name-keyed fonts): predefined Standard (left to the caller), Expert, or formats 0 and 1
   with supplements */
static void cff_encoding(PFont *f, u32 off)
{
  int i;
  for (i = 0; i < 256; i++) f->enc[i] = -1;
  if (off == 0) return;                                       /* StandardEncoding */
  f->has_enc = 1;
  if (off == 1) {
    for (i = 0; i < 256; i++) if (cff_expert_encoding[i]) f->enc[i] = cff_sid_gid(f, cff_expert_encoding[i]);
    return;
  }
  if (off < f->lcff) {
    const u8 *p = f->cff + off, *end = f->cff + f->lcff;
    int fmt = *p++, g = 1, n;
    if (p >= end) return;
    n = *p++;
    if ((fmt & 0x7F) == 0) {
      for (i = 0; i < n && p < end; i++, g++) { if (g < f->nglyphs) f->enc[*p] = g; p++; }
    } else if ((fmt & 0x7F) == 1) {
      for (i = 0; i < n && end - p >= 2; i++, p += 2) {
        int k;
        for (k = 0; k <= p[1] && p[0] + k < 256; k++, g++) if (g < f->nglyphs) f->enc[p[0] + k] = g;
      }
    } else return;
    if (fmt & 0x80) {
      if (p >= end) return;
      n = *p++;
      for (i = 0; i < n && end - p >= 3; i++, p += 3) f->enc[p[0]] = cff_sid_gid(f, be16(p + 1));
    }
  }
}
/* opens the CFF at f->cff: the INDEXes, the Top DICT, the Private DICT(s), the charset and the encoding */
static int cff_open(PFont *f, char *err, size_t errcap)
{
  u32 n, e1, e2, e3, top_at, top_len, cnt, csoff;
  double v[2];
  if (f->lcff < 4) return fail(err, errcap, "CFF: too short"), 0;
  if (f->cff[0] != 1) return fail(err, errcap, f->cff[0] == 2 ? "CFF2 is not supported" : "CFF: unknown version"), 0;
  /* the Name INDEX, the Top DICT INDEX (its first DICT), the String INDEX, the global Subrs */
  if (!idx_info(f, f->cff[2], &n, &e1) || !idx_info(f, e1, &n, &e2) || !idx_get(f, e1, 0, &top_at, &top_len) ||
      !idx_info(f, e2, &f->nstr, &e3) || !idx_info(f, e3, &n, &cnt))
    return fail(err, errcap, "CFF: damaged header INDEXes"), 0;
  f->str_idx = e2; f->gsub_idx = e3;
  if (dict_get(f, top_at, top_len, 1206, v, 1) > 0 && v[0] != 2) return fail(err, errcap, "CFF: not Type 2 charstrings"), 0;
  if (!(csoff = dict_off(f, top_at, top_len, 17)) || !idx_info(f, csoff, &cnt, &n) || !cnt)
    return fail(err, errcap, "CFF: no CharStrings"), 0;
  f->cs_idx = csoff; f->nglyphs = (int)cnt;
  f->fm[0] = f->fm[3] = 0.001; f->fm[1] = f->fm[2] = f->fm[4] = f->fm[5] = 0;
  dict_matrix(f, top_at, top_len, f->fm);
  f->cid = dict_get(f, top_at, top_len, 1230, v, 2) != 0;
  if (f->cid) {
    /* CID-keyed: a font DICT (and Private DICT) for each group of glyphs, which FDSelect picks */
    u32 fda = dict_off(f, top_at, top_len, 1236), nfd, k, at, len;
    double m[6];
    f->fdsel = dict_off(f, top_at, top_len, 1237);
    if (!fda || !idx_info(f, fda, &nfd, &n) || !nfd) return fail(err, errcap, "CFF: CID font without FDArray"), 0;
    if (nfd > MAXFD) nfd = MAXFD;
    if (!(f->fds = (CffPriv *)calloc(nfd, sizeof(CffPriv)))) return fail(err, errcap, "out of memory"), 0;
    for (k = 0; k < nfd; k++) if (idx_get(f, fda, k, &at, &len)) cff_private(f, at, len, &f->fds[k]);
    f->nfd = (int)nfd;
    if (idx_get(f, fda, 0, &at, &len) && dict_matrix(f, at, len, m)) {
      double t[6];
      if (dict_get(f, top_at, top_len, 1207, t, 6) == 6) {
        Mat a = mat_make(m[0], m[1], m[2], m[3], m[4], m[5]), b = mat_make(f->fm[0], f->fm[1], f->fm[2], f->fm[3], f->fm[4], f->fm[5]);
        Mat r = mat_mul(a, b);
        f->fm[0] = r.a; f->fm[1] = r.b; f->fm[2] = r.c; f->fm[3] = r.d; f->fm[4] = r.e; f->fm[5] = r.f;
      } else memcpy(f->fm, m, sizeof m);
    }
  } else cff_private(f, top_at, top_len, &f->priv);
  if (!cff_charset(f, dict_off(f, top_at, top_len, 15))) return fail(err, errcap, "out of memory"), 0;
  if (f->cid) {
    int g;
    if (!(f->cidmap = (u32 *)malloc((size_t)f->nglyphs * sizeof(u32)))) return fail(err, errcap, "out of memory"), 0;
    for (g = 0; g < f->nglyphs; g++) f->cidmap[g] = ((u32)f->charset[g] << 16) | (u32)g;
    qsort(f->cidmap, (size_t)f->nglyphs, sizeof(u32), cmp_u32);
    { int i; for (i = 0; i < 256; i++) f->enc[i] = -1; }
  } else cff_encoding(f, dict_off(f, top_at, top_len, 16));
  return 1;
}
/* the font DICT's Private settings for glyph g of a CID-keyed font (FDSelect format 0 or 3) */
static const CffPriv *cff_fd(const PFont *f, int g)
{
  u32 fd = 0;
  if (!f->cid) return &f->priv;
  if (f->fdsel && f->fdsel < f->lcff) {
    const u8 *s = f->cff + f->fdsel;
    u32 left = f->lcff - f->fdsel;
    if (s[0] == 0 && left > 1u + (u32)g) fd = s[1 + g];
    else if (s[0] == 3 && left >= 5) {
      u32 nr = be16(s + 1), i;
      for (i = 0; i < nr && 3 + 3 * i + 5 <= left; i++)
        if ((u32)g >= be16(s + 3 + 3 * i) && (u32)g < be16(s + 3 + 3 * (i + 1))) { fd = s[3 + 3 * i + 2]; break; }
    }
  }
  return (int)fd < f->nfd ? &f->fds[fd] : &f->fds[0];
}

/* ------------------------------------------------------------------ */
/* Type 2 charstrings */
typedef struct {
  PFont *f;
  Pen *pen;
  double st[48]; int n;
  double trans[32];
  int nstems, masked, width_done, depth, seac;
  long ops;
  double width; int have_width;
  u32 lsub;
} T2;
static int t2_glyph(PFont *f, int gid, Pen *pen, int seac, double *width);
/* the Type 2 subroutine bias for an INDEX of n entries */
static u32 t2_bias(u32 n) { return n < 1240 ? 107 : n < 33900 ? 1131 : 32768; }
/* the charstring at at (len bytes in the CFF); 2 at endchar, 1 at its end or a return, 0 if damaged */
static int t2_run(T2 *c, u32 at, u32 len)
{
  const u8 *p = c->f->cff + at, *end = p + len;
  double *s = c->st;
  Pen *g = c->pen;
  if (++c->depth > MAXDEPTH) return 0;
  while (p < end) {
    int b = *p++, i;
    if (++c->ops > MAXOPS) return 0;
    if (b >= 32 || b == 28) {
      double v;
      if (b == 28) { if (end - p < 2) return 0; v = sbe16(p); p += 2; }
      else if (b <= 246) v = b - 139;
      else if (b <= 250) { if (p >= end) return 0; v = (b - 247) * 256 + *p++ + 108; }
      else if (b <= 254) { if (p >= end) return 0; v = -(b - 251) * 256 - *p++ - 108; }
      else { if (end - p < 4) return 0; v = (i32)be32(p) / 65536.0; p += 4; }
      if (c->n >= 48) return 0;
      s[c->n++] = v;
      continue;
    }
    /* the first operator that clears the stack may have the glyph's width before its own arguments */
#define WIDTH(odd) do { if (!c->width_done) { c->width_done = 1; if ((c->n & 1) == (odd) && c->n > 0) { \
      c->width = s[0]; c->have_width = 1; memmove(s, s + 1, (size_t)--c->n * sizeof(double)); } } } while (0)
    switch (b) {
    case 1: case 3: case 18: case 23:                         /* stems: counted for the masks */
      WIDTH(1); if (!c->masked) c->nstems += c->n / 2; c->n = 0; break;
    case 19: case 20:                                         /* hintmask, cntrmask (the first maybe after implied vstems) */
      WIDTH(1);
      if (!c->masked) c->nstems += c->n / 2;                  /* (stems come before the first mask, fixing its size) */
      c->masked = 1; c->n = 0;
      if (end - p < (c->nstems + 7) / 8) return 0;
      p += (c->nstems + 7) / 8;
      break;
    case 21: WIDTH(1); if (c->n < 2) return 0; pen_move(g, g->x + s[c->n - 2], g->y + s[c->n - 1]); c->n = 0; break;
    case 22: WIDTH(0); if (c->n < 1) return 0; pen_move(g, g->x + s[c->n - 1], g->y); c->n = 0; break;
    case 4: WIDTH(0); if (c->n < 1) return 0; pen_move(g, g->x, g->y + s[c->n - 1]); c->n = 0; break;
    case 5:                                                   /* rlineto */
      for (i = 0; i + 1 < c->n; i += 2) pen_line(g, g->x + s[i], g->y + s[i + 1]);
      c->n = 0; break;
    case 6: case 7: {                                         /* hlineto, vlineto: alternating */
      int h = b == 6;
      for (i = 0; i < c->n; i++, h = !h) { if (h) pen_line(g, g->x + s[i], g->y); else pen_line(g, g->x, g->y + s[i]); }
      c->n = 0; break; }
    case 8: case 24:                                          /* rrcurveto, rcurveline */
      for (i = 0; i + 5 < c->n; i += 6) {
        double x1 = g->x + s[i], y1 = g->y + s[i + 1], x2 = x1 + s[i + 2], y2 = y1 + s[i + 3];
        pen_curve(g, x1, y1, x2, y2, x2 + s[i + 4], y2 + s[i + 5]);
      }
      if (b == 24 && i + 1 < c->n) pen_line(g, g->x + s[i], g->y + s[i + 1]);
      c->n = 0; break;
    case 25:                                                  /* rlinecurve */
      for (i = 0; i + 7 < c->n; i += 2) pen_line(g, g->x + s[i], g->y + s[i + 1]);
      if (i + 5 < c->n) {
        double x1 = g->x + s[i], y1 = g->y + s[i + 1], x2 = x1 + s[i + 2], y2 = y1 + s[i + 3];
        pen_curve(g, x1, y1, x2, y2, x2 + s[i + 4], y2 + s[i + 5]);
      }
      c->n = 0; break;
    case 26: case 27: {                                       /* vvcurveto, hhcurveto */
      double d1 = 0;
      i = 0;
      if (c->n & 1) d1 = s[i++];
      for (; i + 3 < c->n; i += 4, d1 = 0) {
        double x1, y1, x2, y2;
        if (b == 26) { x1 = g->x + d1; y1 = g->y + s[i]; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; pen_curve(g, x1, y1, x2, y2, x2, y2 + s[i + 3]); }
        else { x1 = g->x + s[i]; y1 = g->y + d1; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; pen_curve(g, x1, y1, x2, y2, x2 + s[i + 3], y2); }
      }
      c->n = 0; break; }
    case 30: case 31: {                                       /* vhcurveto, hvcurveto: alternating */
      int h = b == 31;
      for (i = 0; i + 3 < c->n; i += 4, h = !h) {
        double last = (i + 5 == c->n) ? s[i + 4] : 0, x1, y1, x2, y2;
        if (h) { x1 = g->x + s[i]; y1 = g->y; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; pen_curve(g, x1, y1, x2, y2, x2 + last, y2 + s[i + 3]); }
        else { x1 = g->x; y1 = g->y + s[i]; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; pen_curve(g, x1, y1, x2, y2, x2 + s[i + 3], y2 + last); }
      }
      c->n = 0; break; }
    case 10: case 29: {                                       /* callsubr, callgsubr */
      u32 idx = b == 10 ? c->lsub : c->f->gsub_idx, cnt, e, sat, slen;
      double num;
      int rr;
      if (!c->n || (b == 10 && !idx) || !idx_info(c->f, idx, &cnt, &e)) return 0;
      num = s[--c->n] + t2_bias(cnt);
      if (!(num >= 0 && num < cnt) || !idx_get(c->f, idx, (u32)num, &sat, &slen)) return 0;
      if (!(rr = t2_run(c, sat, slen))) return 0;
      c->depth--;
      if (rr == 2) return 2;
      break; }
    case 11: return 1;                                        /* return */
    case 14:                                                  /* endchar, maybe an accented character (seac) */
      WIDTH(1);
      pen_close(g);
      if (c->n >= 4 && !c->f->cid) {
        int bc = (int)s[c->n - 2], ac = (int)s[c->n - 1], bg, ag;
        double adx = s[c->n - 4], ady = s[c->n - 3], odx = g->dx, ody = g->dy;
        if (c->seac || bc < 0 || bc > 255 || ac < 0 || ac > 255 || !enc_standard[bc] || !enc_standard[ac]) return 0;
        bg = pf_glyph_by_name(c->f, enc_standard[bc]); ag = pf_glyph_by_name(c->f, enc_standard[ac]);
        if (bg < 0 || ag < 0 || !t2_glyph(c->f, bg, g, 1, NULL)) return 0;
        g->dx += adx; g->dy += ady;
        if (!t2_glyph(c->f, ag, g, 1, NULL)) return 0;
        g->dx = odx; g->dy = ody;
      }
      c->n = 0;
      return 2;
    case 12: {
      int e2;
      double a, d;
      if (p >= end) return 0;
      e2 = *p++;
      switch (e2) {
      case 35:                                                /* flex: two curves */
        if (c->n >= 12) {
          double x1 = g->x + s[0], y1 = g->y + s[1], x2 = x1 + s[2], y2 = y1 + s[3], x3 = x2 + s[4], y3 = y2 + s[5];
          double x4 = x3 + s[6], y4 = y3 + s[7], x5 = x4 + s[8], y5 = y4 + s[9], x6 = x5 + s[10], y6 = y5 + s[11];
          pen_curve(g, x1, y1, x2, y2, x3, y3); pen_curve(g, x4, y4, x5, y5, x6, y6);
        }
        c->n = 0; break;
      case 34:                                                /* hflex */
        if (c->n >= 7) {
          double y0 = g->y, x1 = g->x + s[0], x2 = x1 + s[1], y2 = y0 + s[2], x3 = x2 + s[3];
          double x4 = x3 + s[4], x5 = x4 + s[5], x6 = x5 + s[6];
          pen_curve(g, x1, y0, x2, y2, x3, y2); pen_curve(g, x4, y2, x5, y0, x6, y0);
        }
        c->n = 0; break;
      case 36:                                                /* hflex1 */
        if (c->n >= 9) {
          double y0 = g->y, x1 = g->x + s[0], y1 = y0 + s[1], x2 = x1 + s[2], y2 = y1 + s[3], x3 = x2 + s[4];
          double x4 = x3 + s[5], x5 = x4 + s[6], y5 = y2 + s[7], x6 = x5 + s[8];
          pen_curve(g, x1, y1, x2, y2, x3, y2); pen_curve(g, x4, y2, x5, y5, x6, y0);
        }
        c->n = 0; break;
      case 37:                                                /* flex1: the last point by the larger move */
        if (c->n >= 11) {
          double x0 = g->x, y0 = g->y, x1 = x0 + s[0], y1 = y0 + s[1], x2 = x1 + s[2], y2 = y1 + s[3], x3 = x2 + s[4], y3 = y2 + s[5];
          double x4 = x3 + s[6], y4 = y3 + s[7], x5 = x4 + s[8], y5 = y4 + s[9], x6, y6;
          if (fabs(x5 - x0) > fabs(y5 - y0)) { x6 = x5 + s[10]; y6 = y0; } else { x6 = x0; y6 = y5 + s[10]; }
          pen_curve(g, x1, y1, x2, y2, x3, y3); pen_curve(g, x4, y4, x5, y5, x6, y6);
        }
        c->n = 0; break;
      /* arithmetic, storage and conditionals (deprecated, but cheap) */
      case 3: case 4: case 10: case 11: case 12: case 15: case 24: case 28:
        if (c->n < 2) return 0;
        a = s[c->n - 2]; d = s[c->n - 1]; c->n -= 2;
        switch (e2) {
        case 3: a = (a != 0 && d != 0); break;
        case 4: a = (a != 0 || d != 0); break;
        case 10: a += d; break;
        case 11: a -= d; break;
        case 12: a = d != 0 ? a / d : 0; break;
        case 15: a = a == d; break;
        case 24: a *= d; break;
        default: s[c->n++] = d; break;                        /* exch: d then a */
        }
        s[c->n++] = a;
        break;
      case 5: case 9: case 14: case 26: case 27: case 18:     /* not, abs, neg, sqrt, dup, drop */
        if (c->n < 1) return 0;
        a = s[c->n - 1];
        if (e2 == 5) s[c->n - 1] = a == 0;
        else if (e2 == 9) s[c->n - 1] = fabs(a);
        else if (e2 == 14) s[c->n - 1] = -a;
        else if (e2 == 26) s[c->n - 1] = a > 0 ? sqrt(a) : 0;
        else if (e2 == 18) c->n--;
        else { if (c->n >= 48) return 0; s[c->n++] = a; }
        break;
      case 20:                                                /* put: val i */
        if (c->n < 2) return 0;
        i = (int)s[c->n - 1];
        if (i >= 0 && i < 32) c->trans[i] = s[c->n - 2];
        c->n -= 2; break;
      case 21:                                                /* get: i -> val */
        if (c->n < 1) return 0;
        i = (int)s[c->n - 1];
        s[c->n - 1] = i >= 0 && i < 32 ? c->trans[i] : 0;
        break;
      case 22:                                                /* ifelse: s1 s2 v1 v2 -> v1 <= v2 ? s1 : s2 */
        if (c->n < 4) return 0;
        a = s[c->n - 2] <= s[c->n - 1] ? s[c->n - 4] : s[c->n - 3];
        c->n -= 4; s[c->n++] = a; break;
      case 23:                                                /* random: something in (0, 1] */
        if (c->n >= 48) return 0;
        s[c->n++] = 0.5; break;
      case 29:                                                /* index */
        if (c->n < 1) return 0;
        i = (int)s[c->n - 1];
        if (i < 0) i = 0;
        s[c->n - 1] = i < c->n - 1 ? s[c->n - 2 - i] : 0;
        break;
      case 30: {                                              /* roll: n items by j */
        int nn, j, k;
        double tmp[48];
        if (c->n < 2) return 0;
        nn = (int)s[c->n - 2]; j = (int)s[c->n - 1]; c->n -= 2;
        if (nn <= 0 || nn > c->n) break;
        j %= nn; if (j < 0) j += nn;
        for (k = 0; k < nn; k++) tmp[(k + j) % nn] = s[c->n - nn + k];
        memcpy(s + c->n - nn, tmp, (size_t)nn * sizeof(double));
        break; }
      default: c->n = 0; break;                               /* (dotsection and the rest) */
      }
      break; }
    default: c->n = 0; break;                                 /* (reserved operators) */
    }
#undef WIDTH
  }
  return 1;
}
/* glyph gid of a CFF font drawn with pen (NULL path: the width alone); *width its advance. seac: inside an
   accented character, where another seac is not allowed */
static int t2_glyph(PFont *f, int gid, Pen *pen, int seac, double *width)
{
  T2 c;
  u32 at, len;
  const CffPriv *pv;
  double sx = pen->x, sy = pen->y;
  if (gid < 0 || gid >= f->nglyphs || !idx_get(f, f->cs_idx, (u32)gid, &at, &len)) return 0;
  memset(&c, 0, sizeof c);
  pv = cff_fd(f, gid);
  c.f = f; c.pen = pen; c.seac = seac; c.lsub = pv->lsub;
  pen->x = 0; pen->y = 0;
  if (!t2_run(&c, at, len)) return 0;
  pen_close(pen);
  if (seac) { pen->x = sx; pen->y = sy; }
  if (width) *width = c.have_width ? pv->nomw + c.width : pv->defw;
  return 1;
}

/* ------------------------------------------------------------------ */
/* Type 1: the eexec-encrypted private part and Type 1 charstrings */

/* decrypts n bytes in place with key r (eexec 55665, charstrings 4330) */
static void t1_decrypt(u8 *p, u32 n, u16 r)
{
  u32 i;
  for (i = 0; i < n; i++) {
    u8 c = p[i];
    p[i] = (u8)(c ^ (r >> 8));
    r = (u16)((c + r) * 52845u + 22719u);
  }
}
static int ps_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0; }
static int ps_delim(int c) { return ps_space(c) || (c && strchr("/[]{}()<>%", c) != NULL); }
/* the next token at *pp (before end): its start and length (a name keeps its '/'); 0 at the end */
static int ps_token(const u8 **pp, const u8 *end, const u8 **tok, u32 *tlen)
{
  const u8 *p = *pp;
  for (;;) {
    while (p < end && ps_space(*p)) p++;
    if (p < end && *p == '%') { while (p < end && *p != '\n' && *p != '\r') p++; continue; }
    break;
  }
  if (p >= end) { *pp = p; return 0; }
  *tok = p;
  if (*p == '/') { p++; while (p < end && !ps_delim(*p)) p++; }
  else if (ps_delim(*p)) p++;
  else while (p < end && !ps_delim(*p)) p++;
  *tlen = (u32)(p - *tok);
  *pp = p;
  return 1;
}
static int tok_is(const u8 *t, u32 n, const char *s) { return n == strlen(s) && !memcmp(t, s, n); }
/* a token as a number; 0 if it isn't one */
static int tok_num(const u8 *t, u32 n, double *v)
{
  char b[40];
  char *e;
  if (!n || n >= sizeof b) return 0;
  memcpy(b, t, n); b[n] = 0;
  *v = strtod(b, &e);
  return e == b + n && *v == *v;
}
/* where the key /name is in [p, end) as a whole token; NULL if nowhere */
static const u8 *ps_find(const u8 *p, const u8 *end, const char *key)
{
  size_t k = strlen(key);
  while (p && (size_t)(end - p) >= k) {
    const u8 *q = (const u8 *)memchr(p, key[0], (size_t)(end - p) - k + 1);
    if (!q) return NULL;
    if (!memcmp(q, key, k) && (q + k == end || ps_delim(q[k]))) return q + k;
    p = q + 1;
  }
  return NULL;
}
/* an RD block: "n RD " and n bytes (after the count token already read as n); its offset within data; 0 if bad */
static int t1_block(const u8 *data, const u8 **pp, const u8 *end, double n, u32 *off, u32 *len)
{
  const u8 *tok;
  u32 tl;
  if (!(n >= 0) || !ps_token(pp, end, &tok, &tl)) return 0;  /* (RD, -| or whatever the font calls it) */
  if (*pp >= end) return 0;
  (*pp)++;                                                    /* exactly one space before the bytes */
  if (n > (double)(end - *pp)) return 0;
  *off = (u32)(*pp - data); *len = (u32)n;
  *pp += (u32)n;
  return 1;
}
/* a charstring decrypted in place, its lenIV leading bytes then dropped from the (offset, length) pair */
static void t1_cs(u8 *data, u32 *pair, int leniv)
{
  if (leniv < 0) return;
  t1_decrypt(data + pair[0], pair[1], 4330);
  if (pair[1] >= (u32)leniv) { pair[0] += (u32)leniv; pair[1] -= (u32)leniv; }
  else pair[1] = 0;
}
/* the font's parts from the clear text (FontMatrix, Encoding) and the decrypted private part (lenIV, Subrs,
   CharStrings), all in f->data: clear text in [0, clen), private part after */
static int t1_parse(PFont *f, u32 clen, char *err, size_t errcap)
{
  u8 *d = f->data;
  const u8 *pe = d + f->len, *ps = d + clen, *p, *q, *tok;
  u32 tl, cap = 0;
  int leniv = 4, i, n = 0;
  double v;
  Buf names;
  int *nameoff = NULL;
  /* FontMatrix */
  f->fm[0] = f->fm[3] = 0.001; f->fm[1] = f->fm[2] = f->fm[4] = f->fm[5] = 0;
  if ((p = ps_find(d, d + clen, "/FontMatrix")) || (p = ps_find(ps, pe, "/FontMatrix"))) {
    double m[6];
    int k = 0;
    const u8 *e = p < ps ? d + clen : pe;
    if (ps_token(&p, e, &tok, &tl) && (tok_is(tok, tl, "[") || tok_is(tok, tl, "{"))) {
      while (k < 6 && ps_token(&p, e, &tok, &tl) && tok_num(tok, tl, &m[k])) k++;
      if (k == 6 && fabs(m[0] * m[3] - m[1] * m[2]) > 1e-12 && fabs(m[0]) < 1e6 && fabs(m[3]) < 1e6) memcpy(f->fm, m, sizeof m);
    }
  }
  /* lenIV (before the Subrs and CharStrings), then the Subrs */
  q = ps_find(ps, pe, "/Subrs");
  p = ps_find(ps, q ? q : pe, "/lenIV");
  if (p && ps_token(&p, pe, &tok, &tl) && tok_num(tok, tl, &v)) leniv = v >= 0 && v <= 64 ? (int)v : -1;
  if (q && ps_token(&q, pe, &tok, &tl) && tok_num(tok, tl, &v) && v > 0) {
    int cnt = v < 65536 ? (int)v : 65536;
    if (!(f->t1sub = (u32 *)calloc((size_t)cnt * 2, sizeof(u32)))) return fail(err, errcap, "out of memory"), 0;
    f->nsub = cnt;
    for (;;) {
      const u8 *save = q;
      double idx, len;
      u32 off, ln;
      if (!ps_token(&q, pe, &tok, &tl)) break;
      if (tok_is(tok, tl, "array") || tok_is(tok, tl, "NP") || tok_is(tok, tl, "|") || tok_is(tok, tl, "noaccess") ||
          tok_is(tok, tl, "put")) continue;
      if (!tok_is(tok, tl, "dup")) { q = save; break; }
      if (!ps_token(&q, pe, &tok, &tl) || !tok_num(tok, tl, &idx) || !ps_token(&q, pe, &tok, &tl) || !tok_num(tok, tl, &len) ||
          !t1_block(d, &q, pe, len, &off, &ln)) break;
      if (idx >= 0 && idx < cnt) {
        u32 *pr = f->t1sub + 2 * (int)idx;
        pr[0] = off; pr[1] = ln;
        t1_cs(d, pr, leniv);
      }
    }
  } else q = NULL;
  /* the CharStrings: "/name n RD <n bytes> ND", until "end" */
  p = q ? ps_find(q, pe, "/CharStrings") : NULL;
  if (!p) p = ps_find(ps, pe, "/CharStrings");
  if (!p) return fail(err, errcap, "Type 1: no CharStrings"), 0;
  buf_init(&names);
  while (ps_token(&p, pe, &tok, &tl)) {
    double len;
    u32 off, ln;
    const u8 *nm;
    u32 nl;
    if (tok_is(tok, tl, "end")) break;
    if (tok[0] != '/') continue;                             /* (dict, dup, begin, ND, |-, readonly ...) */
    nm = tok + 1; nl = tl - 1;
    if (!ps_token(&p, pe, &tok, &tl) || !tok_num(tok, tl, &len)) continue;
    if (!t1_block(d, &p, pe, len, &off, &ln)) break;
    if (n >= MAXGLYPHS) break;
    if ((u32)n >= cap) {
      u32 nc = cap ? cap * 2 : 256;
      u32 *nt = (u32 *)realloc(f->t1cs, nc * 2 * sizeof(u32));
      int *no = (int *)realloc(nameoff, nc * sizeof(int));
      if (nt) f->t1cs = nt;
      if (no) nameoff = no;
      if (!nt || !no) { buf_free(&names); free(nameoff); return fail(err, errcap, "out of memory"), 0; }
      cap = nc;
    }
    f->t1cs[2 * n] = off; f->t1cs[2 * n + 1] = ln;
    t1_cs(d, f->t1cs + 2 * n, leniv);
    nameoff[n] = (int)names.len;
    buf_add(&names, nm, nl); buf_addc(&names, 0);
    n++;
  }
  if (!n || names.oom || !(f->names = (const char **)malloc((size_t)n * sizeof(char *)))) {
    buf_free(&names); free(nameoff);
    return fail(err, errcap, n ? "out of memory" : "Type 1: no glyphs"), 0;
  }
  f->namebuf = buf_take(&names);
  for (i = 0; i < n; i++) f->names[i] = f->namebuf + nameoff[i];
  free(nameoff);
  f->nglyphs = n; f->names_done = 1;
  /* the Encoding: StandardEncoding, or "dup code /name put" entries */
  for (i = 0; i < 256; i++) f->enc[i] = -1;
  if ((p = ps_find(d, d + clen, "/Encoding")) || (p = ps_find(ps, pe, "/Encoding"))) {
    const u8 *e = p < ps ? d + clen : pe;
    if (ps_token(&p, e, &tok, &tl) && !tok_is(tok, tl, "StandardEncoding")) {
      while (ps_token(&p, e, &tok, &tl) && !tok_is(tok, tl, "def")) {
        double code;
        if (!tok_is(tok, tl, "dup")) continue;
        if (ps_token(&p, e, &tok, &tl) && tok_num(tok, tl, &code) && code >= 0 && code < 256 &&
            ps_token(&p, e, &tok, &tl) && tok[0] == '/' && tl < 64) {
          char nm[64];
          memcpy(nm, tok + 1, tl - 1); nm[tl - 1] = 0;
          f->enc[(int)code] = pf_glyph_by_name(f, nm);
          f->has_enc = 1;
        }
      }
    }
  }
  return 1;
}
/* splits a Type 1 font into its clear text and its decrypted private part, into f->data; 0 if it isn't one */
static int t1_open(PFont *f, const u8 *data, size_t len, char *err, size_t errcap)
{
  u8 *clear = NULL, *bin = NULL;
  u32 clen = 0, blen = 0;
  int ok;
  if (len >= 6 && data[0] == 0x80 && data[1] == 1) {
    /* PFB: segments of clear text (1) and binary (2), each with a little-endian length */
    size_t at = 0;
    if (!(clear = (u8 *)malloc(len)) || !(bin = (u8 *)malloc(len))) { free(clear); return fail(err, errcap, "out of memory"), 0; }
    while (at + 2 <= len && data[at] == 0x80 && (data[at + 1] == 1 || data[at + 1] == 2)) {
      u32 n;
      int t = data[at + 1];
      if (at + 6 > len) break;
      n = (u32)data[at + 2] | ((u32)data[at + 3] << 8) | ((u32)data[at + 4] << 16) | ((u32)data[at + 5] << 24);
      at += 6;
      if (n > len - at) n = (u32)(len - at);
      if (t == 1 && !blen) { memcpy(clear + clen, data + at, n); clen += n; }
      else if (t == 2) { memcpy(bin + blen, data + at, n); blen += n; }
      at += n;
    }
  } else {
    /* PFA or a PDF's FontFile: clear text up to eexec, then binary or hexadecimal */
    const u8 *e = ps_find(data, data + len, "eexec"), *p;
    if (!e) return fail(err, errcap, "Type 1: no eexec part"), 0;
    clen = (u32)(e - data);
    /* (the first byte of the encrypted part is never blank, tab, CR or LF: the spec sees to that) */
    p = e;
    while (p < data + len && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    {
      const u8 *h = p, *end = data + len;
      int k = 0, hex = 1;
      for (k = 0; k < 4; k++) if (h + k >= end || !((h[k] >= '0' && h[k] <= '9') || ((h[k] | 32) >= 'a' && (h[k] | 32) <= 'f'))) hex = 0;
      if (!(clear = (u8 *)malloc(clen ? clen : 1)) || !(bin = (u8 *)malloc(len - (size_t)(p - data) + 1))) {
        free(clear); return fail(err, errcap, "out of memory"), 0;
      }
      memcpy(clear, data, clen);
      if (hex) {
        int hi = -1;
        for (; h < end; h++) {
          int c = *h, d;
          if (c >= '0' && c <= '9') d = c - '0';
          else if ((c | 32) >= 'a' && (c | 32) <= 'f') d = (c | 32) - 'a' + 10;
          else if (ps_space(c)) continue;
          else break;
          if (hi < 0) hi = d; else { bin[blen++] = (u8)(hi * 16 + d); hi = -1; }
        }
      } else { blen = (u32)(end - p); memcpy(bin, p, blen); }
    }
  }
  if (blen < 4) { free(clear); free(bin); return fail(err, errcap, "Type 1: no encrypted part"), 0; }
  t1_decrypt(bin, blen, 55665);
  f->len = clen + blen - 4;
  if (!(f->data = (u8 *)malloc(f->len + 1))) { free(clear); free(bin); return fail(err, errcap, "out of memory"), 0; }
  memcpy(f->data, clear, clen);
  memcpy(f->data + clen, bin + 4, blen - 4);
  f->data[f->len] = 0;
  free(clear); free(bin);
  ok = t1_parse(f, clen, err, errcap);
  return ok;
}
typedef struct {
  PFont *f;
  Pen *pen;
  double st[64]; int n;
  int known, unknown;              /* results of the last callothersubr still to be popped */
  int flex, nflex; double fx[7], fy[7];
  double sbx, sby, wx;
  int depth, seac;
  long ops;
} T1;
static int t1_glyph(PFont *f, int gid, Pen *pen, int seac, double *width);
/* the Type 1 charstring at off (len bytes of data, decrypted); 2 at endchar, 1 at its end or a return, 0 if bad */
static int t1_run(T1 *c, u32 off, u32 len)
{
  const u8 *p = c->f->data + off, *end = p + len;
  double *s = c->st;
  Pen *g = c->pen;
  if (++c->depth > MAXDEPTH + 6) return 0;
  while (p < end) {
    int b = *p++;
    if (++c->ops > MAXOPS) return 0;
    if (b >= 32) {
      double v;
      if (b <= 246) v = b - 139;
      else if (b <= 250) { if (p >= end) return 0; v = (b - 247) * 256 + *p++ + 108; }
      else if (b <= 254) { if (p >= end) return 0; v = -(b - 251) * 256 - *p++ - 108; }
      else { if (end - p < 4) return 0; v = (double)(i32)be32(p); p += 4; }
      if (c->n >= 64) return 0;
      s[c->n++] = v;
      continue;
    }
    if (b != 12) c->known = c->unknown = 0;
    switch (b) {
    case 13:                                                  /* hsbw: sbx wx */
      if (c->n >= 2) { c->sbx = s[c->n - 2]; c->wx = s[c->n - 1]; g->x = c->sbx; g->y = 0; }
      c->n = 0; break;
    case 21:                                                  /* rmoveto (in a flex: a point of it) */
    case 22: case 4: {                                        /* hmoveto, vmoveto */
      double dx = 0, dy = 0;
      if (b == 21) { if (c->n < 2) return 0; dx = s[c->n - 2]; dy = s[c->n - 1]; }
      else { if (c->n < 1) return 0; if (b == 22) dx = s[c->n - 1]; else dy = s[c->n - 1]; }
      if (c->flex) {
        g->x += dx; g->y += dy;
        if (c->nflex < 7) { c->fx[c->nflex] = g->x; c->fy[c->nflex] = g->y; c->nflex++; }
      } else pen_move(g, g->x + dx, g->y + dy);
      c->n = 0; break; }
    case 5: if (c->n >= 2) pen_line(g, g->x + s[c->n - 2], g->y + s[c->n - 1]); c->n = 0; break;     /* rlineto */
    case 6: if (c->n >= 1) pen_line(g, g->x + s[c->n - 1], g->y); c->n = 0; break;                  /* hlineto */
    case 7: if (c->n >= 1) pen_line(g, g->x, g->y + s[c->n - 1]); c->n = 0; break;                  /* vlineto */
    case 8:                                                   /* rrcurveto */
      if (c->n >= 6) {
        double *a = s + c->n - 6, x1 = g->x + a[0], y1 = g->y + a[1], x2 = x1 + a[2], y2 = y1 + a[3];
        pen_curve(g, x1, y1, x2, y2, x2 + a[4], y2 + a[5]);
      }
      c->n = 0; break;
    case 30:                                                  /* vhcurveto: dy1 dx2 dy2 dx3 */
      if (c->n >= 4) {
        double *a = s + c->n - 4, x1 = g->x, y1 = g->y + a[0], x2 = x1 + a[1], y2 = y1 + a[2];
        pen_curve(g, x1, y1, x2, y2, x2 + a[3], y2);
      }
      c->n = 0; break;
    case 31:                                                  /* hvcurveto: dx1 dx2 dy2 dy3 */
      if (c->n >= 4) {
        double *a = s + c->n - 4, x1 = g->x + a[0], y1 = g->y, x2 = x1 + a[1], y2 = y1 + a[2];
        pen_curve(g, x1, y1, x2, y2, x2, y2 + a[3]);
      }
      c->n = 0; break;
    case 9: pen_close(g); c->n = 0; break;                    /* closepath */
    case 10: {                                                /* callsubr */
      int k, rr;
      if (!c->n) return 0;
      k = (int)s[--c->n];
      if (k < 0 || k >= c->f->nsub) return 0;
      if (!(rr = t1_run(c, c->f->t1sub[2 * k], c->f->t1sub[2 * k + 1]))) return 0;
      c->depth--;
      if (rr == 2) return 2;
      break; }
    case 11: return 1;                                        /* return */
    case 14: pen_close(g); c->n = 0; return 2;                /* endchar */
    case 1: case 3: c->n = 0; break;                          /* hstem, vstem */
    case 12: {
      int e2;
      if (p >= end) return 0;
      e2 = *p++;
      if (e2 != 16 && e2 != 17) c->known = c->unknown = 0;
      switch (e2) {
      case 7:                                                 /* sbw: sbx sby wx wy */
        if (c->n >= 4) { c->sbx = s[c->n - 4]; c->sby = s[c->n - 3]; c->wx = s[c->n - 2]; g->x = c->sbx; g->y = c->sby; }
        c->n = 0; break;
      case 12:                                                /* div */
        if (c->n < 2) return 0;
        s[c->n - 2] = s[c->n - 1] != 0 ? s[c->n - 2] / s[c->n - 1] : 0;
        c->n--; break;
      case 6: {                                               /* seac: asb adx ady bchar achar */
        int bc, ac, bg, ag;
        double asb, adx, ady, odx = g->dx, ody = g->dy;
        if (c->n < 5 || c->seac) return 0;
        asb = s[c->n - 5]; adx = s[c->n - 4]; ady = s[c->n - 3]; bc = (int)s[c->n - 2]; ac = (int)s[c->n - 1];
        if (bc < 0 || bc > 255 || ac < 0 || ac > 255 || !enc_standard[bc] || !enc_standard[ac]) return 0;
        bg = pf_glyph_by_name(c->f, enc_standard[bc]); ag = pf_glyph_by_name(c->f, enc_standard[ac]);
        pen_close(g);
        if (bg < 0 || ag < 0 || !t1_glyph(c->f, bg, g, 1, NULL)) return 0;
        g->dx += adx - asb + c->sbx; g->dy += ady;            /* (the accent's own sidebearing then adds asb) */
        if (!t1_glyph(c->f, ag, g, 1, NULL)) return 0;
        g->dx = odx; g->dy = ody;
        c->n = 0;
        return 2; }
      case 16: {                                              /* callothersubr: args n othersubr */
        int k, na, base;
        if (c->n < 2) return 0;
        k = (int)s[c->n - 1]; na = (int)s[c->n - 2]; c->n -= 2;
        if (na < 0 || na > c->n) return 0;
        base = c->n - na;
        c->known = c->unknown = 0;
        if (k == 0) {                                         /* the end of a flex: its two curves */
          if (c->flex && c->nflex >= 7) {
            pen_curve(g, c->fx[1], c->fy[1], c->fx[2], c->fy[2], c->fx[3], c->fy[3]);
            pen_curve(g, c->fx[4], c->fy[4], c->fx[5], c->fy[5], c->fx[6], c->fy[6]);
          }
          c->flex = 0;
          c->n = base;
          s[c->n++] = g->x; s[c->n++] = g->y;                 /* (what the pops and setcurrentpoint after want) */
          c->known = 2;
        } else if (k == 1) { pen_start(g); c->flex = 1; c->nflex = 0; c->n = base; }   /* (a contour may start with it) */
        else if (k == 2) c->n = base;
        else if (k == 3) { c->n = base; s[c->n++] = 3; c->known = 1; }    /* hint replacement: subr 3 */
        else if (k >= 14 && k <= 18) {                        /* multiple master blends: the default design */
          static const int nres[5] = { 1, 2, 3, 4, 6 };
          int r = nres[k - 14];
          if (r <= na) { c->n = base + r; c->known = r; } else c->n = base;
        } else { c->n = base; c->unknown = na; }              /* the arguments come back with the pops */
        break; }
      case 17:                                                /* pop */
        if (c->known > 0) c->known--;
        else if (c->unknown > 0 && c->n < 64) { c->n++; c->unknown--; }
        break;
      case 33:                                                /* setcurrentpoint */
        if (c->n >= 2) { g->x = s[c->n - 2]; g->y = s[c->n - 1]; }
        c->n = 0; break;
      default: c->n = 0; break;                               /* dotsection, vstem3, hstem3 */
      }
      break; }
    default: c->n = 0; break;
    }
  }
  return 1;
}
/* glyph gid of a Type 1 font drawn with pen (NULL path: the width alone); *width its advance */
static int t1_glyph(PFont *f, int gid, Pen *pen, int seac, double *width)
{
  T1 c;
  if (gid < 0 || gid >= f->nglyphs) return 0;
  memset(&c, 0, sizeof c);
  c.f = f; c.pen = pen; c.seac = seac;
  pen->x = 0; pen->y = 0;
  if (!t1_run(&c, f->t1cs[2 * gid], f->t1cs[2 * gid + 1])) return 0;
  pen_close(pen);
  if (width) *width = c.wx;
  return 1;
}

/* ------------------------------------------------------------------ */
/* loading */

/* the sfnt at f->base: TrueType's tables, or the CFF inside OpenType */
static int sfnt_open(PFont *f, char *err, size_t errcap)
{
  u32 off, len, hh, lhh;
  int maxg = -1;
  if (!within(f->base, 12, f->len)) return fail(err, errcap, "sfnt: too short"), 0;
  f->sfnt = 1;
  if (sfnt_table(f, "cmap", &f->cmap, &f->lcmap) && f->lcmap < 4) f->cmap = f->lcmap = 0;
  if (sfnt_table(f, "post", &f->post, &f->lpost) && f->lpost < 32) f->post = f->lpost = 0;
  if (sfnt_table(f, "maxp", &off, &len) && len >= 6) maxg = (int)be16(f->data + off + 4);
  f->upm = 1000;
  if (sfnt_table(f, "head", &off, &len) && len >= 54) {
    f->upm = (int)be16(f->data + off + 18);
    f->longloca = sbe16(f->data + off + 50) != 0;
    if (f->upm < 16 || f->upm > 16384) f->upm = 1000;
  }
  if (sfnt_table(f, "hhea", &hh, &lhh) && lhh >= 36 && sfnt_table(f, "hmtx", &f->hmtx, &f->lhmtx)) {
    f->nhmetrics = (int)be16(f->data + hh + 34);
    if ((u32)f->nhmetrics > f->lhmtx / 4) f->nhmetrics = (int)(f->lhmtx / 4);
  }
  if (!memcmp(f->data + f->base, "OTTO", 4)) {
    if (!sfnt_table(f, "CFF ", &off, &len))
      return fail(err, errcap, sfnt_table(f, "CFF2", &off, &len) ? "CFF2 is not supported" : "OpenType: no CFF table"), 0;
    f->kind = PF_CFF; f->cff = f->data + off; f->lcff = len;
    return cff_open(f, err, errcap);
  }
  f->kind = PF_TRUETYPE;
  if (!sfnt_table(f, "loca", &f->loca, &f->lloca) || !sfnt_table(f, "glyf", &f->glyf, &f->lglyf))
    return fail(err, errcap, "TrueType: no glyf/loca tables"), 0;
  f->nglyphs = (int)(f->lloca / (f->longloca ? 4 : 2)) - 1;
  if (maxg >= 0 && maxg < f->nglyphs) f->nglyphs = maxg;
  if (f->nglyphs <= 0) return fail(err, errcap, "TrueType: no glyphs"), 0;
  f->fm[0] = f->fm[3] = 1.0 / f->upm; f->fm[1] = f->fm[2] = f->fm[4] = f->fm[5] = 0;
  { int i; for (i = 0; i < 256; i++) f->enc[i] = -1; }
  return 1;
}
/* a font program from memory, of whichever kind it turns out to be */
PFont *pf_load(const u8 *data, size_t len, char *err, size_t errcap)
{
  PFont *f;
  int ok = 0;
  if (err && errcap) err[0] = 0;
  if (!data || len < 4) return fail(err, errcap, "font: too short");
  if (len > 0x7FFFFFFFUL) return fail(err, errcap, "font: too big");
  if (!(f = (PFont *)calloc(1, sizeof *f))) return fail(err, errcap, "out of memory");
  if (be32(data) == 0x00010000UL || !memcmp(data, "true", 4) || !memcmp(data, "OTTO", 4) || !memcmp(data, "ttcf", 4) ||
      (data[0] == 1 && data[1] == 0 && data[2] >= 4 && data[3] >= 1 && data[3] <= 4)) {
    if (!(f->data = (u8 *)malloc(len))) { free(f); return fail(err, errcap, "out of memory"); }
    memcpy(f->data, data, len); f->len = (u32)len;
    if (!memcmp(data, "ttcf", 4)) {
      if (len < 16 || be32(data + 8) < 1) fail(err, errcap, "TrueType collection: no fonts");
      else {
        f->base = be32(data + 12);
        if (!within(f->base, 12, f->len)) fail(err, errcap, "TrueType collection: bad offset");
        else ok = sfnt_open(f, err, errcap);
      }
    } else if (data[0] == 1) {
      f->kind = PF_CFF; f->cff = f->data; f->lcff = f->len;
      ok = cff_open(f, err, errcap);
    } else ok = sfnt_open(f, err, errcap);
  } else if ((data[0] == 0x80 && data[1] == 1) || ps_find(data, data + len, "eexec")) {
    f->kind = PF_TYPE1;
    f->fm[0] = f->fm[3] = 0.001;
    ok = t1_open(f, data, len, err, errcap);
  } else fail(err, errcap, "font: unknown format");
  if (!ok) { pf_free(f); return NULL; }
  return f;
}
/* everything the font holds */
void pf_free(PFont *f)
{
  if (!f) return;
  free(f->data); free(f->pts); free(f->ends); free(f->fds); free(f->charset); free(f->cidmap);
  free(f->t1cs); free(f->t1sub); free(f->namebuf); free((void *)f->names); free(f->byname);
  free(f);
}

/* ------------------------------------------------------------------ */
/* the questions */

int pf_kind(const PFont *f) { return f ? f->kind : 0; }
int pf_num_glyphs(const PFont *f) { return f ? f->nglyphs : 0; }
void pf_font_matrix(const PFont *f, double m[6])
{
  if (f) memcpy(m, f->fm, 6 * sizeof(double));
  else { m[0] = m[3] = 0.001; m[1] = m[2] = m[4] = m[5] = 0; }
}
/* the outline; on failure the path is put back as it was */
int pf_glyph_outline(PFont *f, int gid, Path *out)
{
  int nops = out->nops, npts = out->npts, ok = 0;
  Pen pen;
  if (!f || gid < 0 || gid >= f->nglyphs || out->oom) return 0;
  memset(&pen, 0, sizeof pen);
  pen.p = out;
  if (f->kind == PF_TRUETYPE) ok = tt_glyph(f, gid, out);
  else if (f->kind == PF_CFF) ok = t2_glyph(f, gid, &pen, 0, NULL);
  else if (f->kind == PF_TYPE1) ok = t1_glyph(f, gid, &pen, 0, NULL);
  if (out->oom || pen.over) ok = 0;
  if (!ok) { out->nops = nops; out->npts = npts; }
  return ok;
}
/* the advance: hmtx, or the charstring's width */
double pf_glyph_advance(PFont *f, int gid)
{
  Pen pen;
  double w = 0;
  if (!f || gid < 0 || gid >= f->nglyphs) return 0;
  memset(&pen, 0, sizeof pen);
  if (f->kind == PF_TRUETYPE) {
    int i = gid < f->nhmetrics ? gid : f->nhmetrics - 1;
    return i >= 0 ? (double)be16(f->data + f->hmtx + 4 * i) : 0;
  }
  if (f->kind == PF_CFF && !t2_glyph(f, gid, &pen, 0, &w)) return 0;
  if (f->kind == PF_TYPE1 && !t1_glyph(f, gid, &pen, 0, &w)) return 0;
  return w;
}
/* the names of all glyphs, from the CFF charset or the TrueType post table (Type 1's are made at load) */
static void build_names(PFont *f)
{
  int g, n = f->nglyphs;
  f->names_done = 1;
  if (!(f->names = (const char **)calloc((size_t)n, sizeof(char *)))) return;
  if (f->kind == PF_CFF && !f->cid) {
    /* the String INDEX's strings copied with NULs */
    Buf b;
    int *offs = (int *)malloc((size_t)n * sizeof(int));
    if (!offs) return;
    buf_init(&b);
    for (g = 0; g < n; g++) {
      const u8 *s;
      u32 l;
      offs[g] = -1;
      if (f->charset[g] < 391) continue;
      if (cff_string(f, f->charset[g], &s, &l) && l < 256) { offs[g] = (int)b.len; buf_add(&b, s, l); buf_addc(&b, 0); }
    }
    if (!b.oom) {
      f->namebuf = buf_take(&b);
      for (g = 0; g < n; g++)
        f->names[g] = f->charset[g] < 391 ? cff_std_strings[f->charset[g]] : offs[g] >= 0 && f->namebuf ? f->namebuf + offs[g] : NULL;
    } else buf_free(&b);
    free(offs);
  } else if (f->sfnt && f->post) {
    const u8 *p = f->data + f->post;
    u32 ver = be32(p);
    if (ver == 0x00010000UL) { for (g = 0; g < n && g < 258; g++) f->names[g] = mac_glyph_names[g]; }
    else if (ver == 0x00020000UL && f->lpost >= 34) {
      u32 ng = be16(p + 32), k, cnt = 0, at;
      const u8 *q, *end = p + f->lpost;
      const char **tab;
      char *o;
      if (ng > (u32)n) ng = (u32)n;
      at = 34 + 2 * be16(p + 32);
      if (at > f->lpost) return;
      /* the Pascal strings after the indexes: counted, then copied out with NULs */
      for (q = p + at; q < end && (u32)(end - q) >= 1u + *q; q += 1 + *q) cnt++;
      if (!(tab = (const char **)malloc((cnt + 1) * sizeof(char *)))) return;
      if (!(o = f->namebuf = (char *)malloc(f->lpost - at + cnt + 1))) { free(tab); return; }
      for (q = p + at, k = 0; k < cnt; q += 1 + *q, k++) {
        tab[k] = o;
        memcpy(o, q + 1, *q); o[*q] = 0; o += *q + 1;
      }
      for (k = 0; k < ng; k++) {
        u32 ix = be16(p + 34 + 2 * k);
        if (ix < 258) f->names[k] = mac_glyph_names[ix];
        else if (ix - 258 < cnt) f->names[k] = tab[ix - 258];
      }
      free(tab);
    }
  }
}
/* sorts names (then glyph ids) for the search */
static int cmp_name(const void *a, const void *b)
{
  const NameEnt *x = (const NameEnt *)a, *y = (const NameEnt *)b;
  int c = strcmp(x->n, y->n);
  return c ? c : x->gid - y->gid;
}
const char *pf_glyph_name(PFont *f, int gid)
{
  if (!f || gid < 0 || gid >= f->nglyphs) return NULL;
  if (!f->names_done) build_names(f);
  return f->names ? f->names[gid] : NULL;
}
/* the glyph of that name (the first if several have it): a binary search of the names, sorted once */
int pf_glyph_by_name(PFont *f, const char *name)
{
  int lo, hi, g;
  if (!f || !name) return -1;
  if (!f->names_done) build_names(f);
  if (!f->names) return -1;
  if (!f->byname) {
    int n = 0;
    if (!(f->byname = (NameEnt *)malloc((size_t)f->nglyphs * sizeof(NameEnt)))) return -1;
    for (g = 0; g < f->nglyphs; g++) if (f->names[g]) { f->byname[n].n = f->names[g]; f->byname[n].gid = g; n++; }
    qsort(f->byname, (size_t)n, sizeof(NameEnt), cmp_name);
    f->nbyname = n;
  }
  lo = 0; hi = f->nbyname;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (strcmp(f->byname[mid].n, name) < 0) lo = mid + 1; else hi = mid;
  }
  return lo < f->nbyname && !strcmp(f->byname[lo].n, name) ? f->byname[lo].gid : -1;
}
int pf_is_cid(const PFont *f) { return f && f->cid; }
/* a CID's glyph: a binary search of the sorted charset */
int pf_cid_to_gid(const PFont *f, int cid)
{
  int lo, hi;
  if (!f || cid < 0) return -1;
  if (!f->cid) return cid < f->nglyphs ? cid : -1;
  if (cid > 0xFFFF) return -1;
  lo = 0; hi = f->nglyphs;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if ((f->cidmap[mid] >> 16) < (u32)cid) lo = mid + 1; else hi = mid;
  }
  return lo < f->nglyphs && (f->cidmap[lo] >> 16) == (u32)cid ? (int)(f->cidmap[lo] & 0xFFFF) : -1;
}
const int *pf_builtin_encoding(PFont *f)
{
  return f && f->has_enc && (f->kind == PF_CFF || f->kind == PF_TYPE1) ? f->enc : NULL;
}
