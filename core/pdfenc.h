/*
 * pdfenc.h - PDF's simple-font encodings (code -> glyph name) and the Adobe Glyph List (glyph name <-> Unicode).
 * Portable C99; built natively for the tests and with vbcc for the Amiga.
 */
#ifndef FOLIO_PDFENC_H
#define FOLIO_PDFENC_H

#include "util.h"

/* 256 glyph names each, NULL where a code has none */
extern const char *const enc_standard[256];    /* StandardEncoding (Type 1 fonts' usual built-in one) */
extern const char *const enc_winansi[256];     /* WinAnsiEncoding */
extern const char *const enc_macroman[256];    /* MacRomanEncoding */
extern const char *const enc_macexpert[256];   /* MacExpertEncoding */
extern const char *const enc_pdfdoc[256];      /* PDFDocEncoding (text strings) */
extern const char *const enc_symbol[256];      /* the Symbol font's built-in encoding */
extern const char *const enc_zapf[256];        /* ZapfDingbats' built-in encoding (a1..a191) */

/* the Unicode code point of a glyph name: the AGL, then uniXXXX, uXXXX[XX], ligatures with '_' and suffixes after '.'
   (their base name); 0 if unknown (also for gNN and cidNN, which say nothing about the character) */
u32 agl_unicode(const char *name);
/* the usual glyph name of a code point (for TrueType fonts without names); NULL if the list has none */
const char *agl_name(u32 unicode);

#endif
