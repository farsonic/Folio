/*
 * pfont.h - font programs embedded in PDFs: TrueType (and TrueType collections), CFF (bare, as FontFile3, or inside
 * OpenType), and Type 1 (PFA or PFB). Glyph outlines come out as Paths in glyph units, y up.
 * Portable C99; built natively for the tests and with vbcc for the Amiga (68040 FPU: doubles throughout).
 */
#ifndef FOLIO_PFONT_H
#define FOLIO_PFONT_H

#include "util.h"
#include "geom.h"

typedef struct PFont PFont;
enum { PF_TRUETYPE = 1, PF_CFF, PF_TYPE1 };

/* a font program from memory: sfnt TrueType (0x00010000 / 'true'), OpenType 'OTTO' (its CFF table), bare CFF
   (FontFile3 /Type1C or /CIDFontType0C), Type 1 as PFA ("%!") or PFB (0x80 0x01 segments), or a TrueType
   collection ('ttcf', its first font). Copies the data it keeps. NULL with a message in err if it can't be used */
PFont *pf_load(const u8 *data, size_t len, char *err, size_t errcap);
void   pf_free(PFont *f);
int    pf_kind(const PFont *f);                 /* PF_ */
int    pf_num_glyphs(const PFont *f);
/* glyph space -> text space (TrueType: 1/unitsPerEm scale; CFF/Type1: the FontMatrix, usually 0.001) */
void   pf_font_matrix(const PFont *f, double m[6]);
/* the outline of glyph gid in glyph units (y up), appended to out. Returns 1 (an empty glyph such as space is 1
   with no ops), 0 if the glyph is bad (out is then left as it was) */
int    pf_glyph_outline(PFont *f, int gid, Path *out);
double pf_glyph_advance(PFont *f, int gid);     /* glyph units; 0 if unknown */
/* TrueType (and OpenType) cmap: the glyph for code in the (platform, encoding) subtable; -1 if that subtable is
   absent (or of a format not read), 0 if unmapped. Formats 0, 4, 6, 10, 12 and 13 */
int    pf_cmap(const PFont *f, int platform, int encoding, u32 code);
int    pf_glyph_by_name(PFont *f, const char *name);   /* Type1/CFF charset, TrueType 'post' names; -1 none */
const char *pf_glyph_name(PFont *f, int gid);          /* NULL if unknown */
int    pf_is_cid(const PFont *f);                      /* a CID-keyed CFF */
int    pf_cid_to_gid(const PFont *f, int cid);         /* CID-keyed CFF charset (identity otherwise); -1 none */
/* the built-in encoding of a Type1 or CFF font: code -> glyph id (-1 none), 256 entries; NULL if none/standard */
const int *pf_builtin_encoding(PFont *f);

#endif
