/*
 * font.h - a PDF font dictionary made usable: how its string bytes split into codes, each code's
 * glyph (in the embedded font program, or in a stand-in when the font isn't embedded), its width,
 * and its text (for search and copy). Type 1, TrueType, Type 3 and composite (Type 0) fonts.
 *
 * Fonts that aren't embedded are drawn with metric-compatible stand-ins found in the font drawer:
 * Liberation Sans for Helvetica and Arial, Liberation Serif for Times, Liberation Mono for Courier,
 * Carlito for Calibri, Caladea for Cambria. Positions always come from the PDF's own widths.
 */
#ifndef FOLIO_FONT_H
#define FOLIO_FONT_H

#include "pdf.h"
#include "geom.h"
#include "pfont.h"
#include "cmap.h"

enum { PFT_SIMPLE, PFT_TYPE3, PFT_TYPE0 };
typedef struct PdfFont {
  int kind;                       /* PFT_ */
  PFont *prog;                    /* the glyphs (embedded, or a stand-in: shared, not to be freed) */
  int own_prog;                   /* prog is this font's own */
  int substitute;                 /* drawn with a stand-in */
  char name[64];                  /* /BaseFont, without a subset tag */
  int bold, italic, serif, fixed, symbolic;
  /* simple fonts: each code's glyph, text and width (text space units x 1000) */
  int gid[256]; u32 uni[256]; double width[256];
  /* composite fonts */
  CMap *encoding;                 /* bytes to CIDs */
  struct { int lo, hi; double w; } *cw; int ncw; double dw;   /* CID widths (W, DW) */
  u16 *cid2gid; int ncid2gid;     /* CIDToGIDMap (NULL: identity) */
  int cid_cff;                    /* a CID-keyed CFF program: CIDs map through its charset */
  /* both */
  CMap *tounicode;
  /* Type 3 */
  Mat matrix;                     /* glyph space to text space (others: 0.001 scale) */
  PObj *charprocs, *resources;    /* (borrowed from the document) */
  const char *names[256];         /* each code's glyph name (Type 3: the CharProcs key) */
  int vertical;
} PdfFont;

/* where the stand-in fonts are (e.g. "PROGDIR:TrueType"); call once at the start */
void font_set_dir(const char *dir);
void font_free_substitutes(void);
/* (PDF/A) the TrueType file a stand-in comes from, read again (malloc'd); NULL for an embedded font */
u8  *font_subst_data(const PdfFont *f, size_t *len);   /* at the end */
/* a font dictionary loaded; NULL if it can't be used at all */
PdfFont *font_load(PDoc *d, PObj *dict);
void font_free(PdfFont *f);
/* the next character code in a string at s (len bytes left): its code and length */
int  font_next(const PdfFont *f, const u8 *s, int len, u32 *code);
/* a code's glyph id in f->prog (-1 none), width in text space units x 1000, and text (up to max
   code points, the number written) */
int  font_gid(const PdfFont *f, u32 code, int nbytes);
double font_width(const PdfFont *f, u32 code, int nbytes);
int  font_text(const PdfFont *f, u32 code, int nbytes, u32 *out, int max);
#endif
