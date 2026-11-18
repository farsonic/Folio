/*
 * filters.h - the PDF stream filters: Flate (inflate, plus a deflate encoder for writing PDFs), LZW, ASCII85,
 * ASCIIHex, RunLength, CCITTFax and the TIFF/PNG predictors.
 * Portable C99 (the native tests and vbcc for the Amiga's 68040).
 *
 * Every decoder returns a malloc'd buffer (at least 1 byte is always allocated, so an empty result is still a
 * pointer) with its length in *outlen, or NULL when out of memory or when the input is damaged before anything at
 * all decodes. Damaged or truncated input otherwise returns whatever decoded before the damage, as PDF viewers
 * must be lenient. maxout caps the output (0 = no cap): decoding stops there, protecting against bombs.
 */
#ifndef FOLIO_FILTERS_H
#define FOLIO_FILTERS_H

#include "util.h"

/* FlateDecode: a zlib header if present (checked loosely), else raw deflate. The Adler-32 trailer is ignored */
u8 *flt_flate(const u8 *in, size_t n, size_t maxout, size_t *outlen);
/* LZWDecode: 9-12 bit codes, early_change as /EarlyChange (default 1) */
u8 *flt_lzw(const u8 *in, size_t n, int early_change, size_t maxout, size_t *outlen);
/* ASCII85Decode: '~>' ends; 'z' is four zeros; whitespace ignored; a leading '<~' is accepted */
u8 *flt_ascii85(const u8 *in, size_t n, size_t *outlen);
/* ASCIIHexDecode: '>' ends; whitespace ignored; an odd final digit is padded with 0 */
u8 *flt_asciihex(const u8 *in, size_t n, size_t *outlen);
/* RunLengthDecode */
u8 *flt_runlength(const u8 *in, size_t n, size_t maxout, size_t *outlen);

/* predictors (DecodeParms /Predictor): 2 = TIFF, 10-15 = PNG (each row its own filter byte); anything else (or
   parameters out of range) gives a plain copy. Returns a new buffer; the input is not freed.
   colors 1-32, bpc 1/2/4/8/16, columns >= 1. A partial last row is decoded as far as it goes */
u8 *flt_predict(const u8 *in, size_t n, int predictor, int colors, int bpc, int columns, size_t *outlen);

/* CCITTFaxDecode: K < 0 pure 2-D (G4), K = 0 1-D (G3 MH), K > 0 mixed (G3 2-D). Output: rows of ceil(columns/8)
   bytes, 1 bit a pixel, a 0 bit black unless black_is_1 (then 1 is black), so the result can be used as a 1-bpc
   DeviceGray image with Decode [0 1]. rows 0 = until the data ends. encoded_byte_align, end_of_line and
   end_of_block as the PDF parameters (PDF's defaults: 0, 0, 1). Uncompressed mode is not supported (it stops) */
u8 *flt_ccitt(const u8 *in, size_t n, int K, int columns, int rows, int black_is_1, int encoded_byte_align,
              int end_of_line, int end_of_block, size_t *outlen);

/* zlib-wrapped deflate for writing PDFs: LZ77 with hash chains and dynamic Huffman blocks (fixed or stored when
   smaller), Adler-32 trailer. level 1-9 trades speed for size (1 is fast, for the 68040). NULL if out of memory */
u8 *flt_deflate(const u8 *in, size_t n, int level, size_t *outlen);

/* a bitonal picture (w x h bytes, non-zero black) encoded as CCITT Group 4 (K -1, BlackIs1 false,
   EncodedByteAlign false, with EOFB); malloc'd, *outlen; NULL if out of memory */
u8 *flt_ccitt_g4_encode(const u8 *px, int w, int h, size_t *outlen);
#endif
