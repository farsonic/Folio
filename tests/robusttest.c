/*
 * robusttest.c - damaged and hostile PDFs that once crashed Folio's core, hung it or ran into
 * undefined behaviour (found when the core was fuzzed inside pdf.datatype, and by tools/fuzz.c).
 * Each PDF is made here, in memory, and opened and drawn in a child process with a time limit,
 * so one failure (a sanitizer's report, a crash, a hang) doesn't stop the others. Built with
 * -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all (tools/test.sh), and
 * also with -m32 where the compiler can, for a 32-bit long as on the Amiga.
 *
 *   robusttest [name...]       all the tests, or those named
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "pdf.h"
#include "render.h"
#include "image.h"
#include "font.h"
#include "pdfwrite.h"

#define TIME_LIMIT 20                      /* seconds a test may take before it counts as a hang */

/* ------------------------------------------------------------------ making PDFs */
typedef struct { const char *s; size_t n; } Obj;          /* n 0: strlen(s), and numbers written out */
/* PDF has no exponents: "1e30" here is written out as 1 and 30 zeros, "-2e-5" as -0.00002 (so the
   tests can say big and small numbers shortly); the result is malloc'd */
static char *xnum(const char *s)
{
  Buf b;
  const char *p = s;
  buf_init(&b);
  while (*p) {
    const char *q = p;
    int neg = 0, k;
    if (*q == '-') { neg = 1; q++; }
    if ((p == s || !((p[-1] >= '0' && p[-1] <= '9') || p[-1] == '.' || p[-1] == '/' || (p[-1] >= 'A' && p[-1] <= 'z'))) &&
        *q >= '1' && *q <= '9' && q[1] == 'e' && (q[2] == '-' || (q[2] >= '0' && q[2] <= '9'))) {
      int digit = *q - '0', e = atoi(q + 2);
      q += 2; if (*q == '-') q++;
      while (*q >= '0' && *q <= '9') q++;
      if (neg) buf_addc(&b, '-');
      if (e >= 0) { buf_addc(&b, '0' + digit); for (k = 0; k < e; k++) buf_addc(&b, '0'); }
      else { buf_adds(&b, "0."); for (k = 1; k < -e; k++) buf_addc(&b, '0'); buf_addc(&b, '0' + digit); }
      p = q;
    } else buf_addc(&b, *p++);
  }
  return buf_take(&b);
}
/* objects 1..n with a correct cross-reference table; Root is 1 0 R */
static u8 *mkpdf(const Obj *objs, int n, size_t *len)
{
  Buf b;
  long *off = (long *)calloc((size_t)n + 1, sizeof(long));
  long x;
  int i;
  buf_init(&b);
  buf_adds(&b, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  for (i = 0; i < n; i++) {
    off[i] = (long)b.len;
    buf_printf(&b, "%d 0 obj\n", i + 1);
    if (objs[i].n) buf_add(&b, objs[i].s, objs[i].n);
    else { char *x = xnum(objs[i].s); buf_adds(&b, x); free(x); }
    buf_adds(&b, "\nendobj\n");
  }
  x = (long)b.len;
  buf_printf(&b, "xref\n0 %d\n0000000000 65535 f \n", n + 1);
  for (i = 0; i < n; i++) buf_printf(&b, "%010ld 00000 n \n", off[i]);
  buf_printf(&b, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", n + 1, x);
  free(off);
  *len = b.len;
  return (u8 *)buf_take(&b);
}
/* a stream object's text: the dictionary's entries (without << >>, numbers written out), then the
   data (n TEXT: text, with its numbers written out too) */
#define TEXT ((size_t)-1)
static char *stream_obj(const char *dict, const char *data, size_t n, size_t *outn)
{
  Buf b;
  char *xd = xnum(dict), *xt = n == TEXT ? xnum(data) : NULL;
  if (xt) { data = xt; n = strlen(xt); }
  buf_init(&b);
  buf_printf(&b, "<< %s /Length %lu >>\nstream\n", xd, (unsigned long)n);
  buf_add(&b, data, n);
  buf_adds(&b, "\nendstream");
  free(xd); free(xt);
  *outn = b.len;
  return buf_take(&b);
}
/* one 200 x 200 page: content, its resources (NULL: no /Resources at all) and objects 5... */
static u8 *page_pdf(const char *content, const char *res, const Obj *extra, int nextra, size_t *len)
{
  Obj o[16];
  char page[600];
  size_t cn;
  char *cs = stream_obj("", content, TEXT, &cn);
  u8 *pdf;
  int i;
  snprintf(page, sizeof page, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] %s%s%s/Contents 4 0 R >>",
           res ? "/Resources " : "", res ? res : "", res ? " " : "");
  o[0].s = "<< /Type /Catalog /Pages 2 0 R >>"; o[0].n = 0;
  o[1].s = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"; o[1].n = 0;
  o[2].s = page; o[2].n = 0;
  o[3].s = cs; o[3].n = cn;
  for (i = 0; i < nextra && i < 12; i++) o[4 + i] = extra[i];
  pdf = mkpdf(o, 4 + (nextra < 12 ? nextra : 12), len);
  free(cs);
  return pdf;
}

/* ------------------------------------------------------------------ opening and drawing */
/* every page (up to 3) drawn at scale; how many pages were drawn (-1: it didn't open) */
static int draw_all(const u8 *data, size_t len, double scale)
{
  char err[160];
  PDoc *d = pdf_open(data, len, err, sizeof err);
  RCache *c;
  int i, drawn = 0;
  if (!d) return -1;
  c = rcache_new(d);
  for (i = 0; i < d->npages && i < 3; i++) {
    RenderOpts o;
    Pixmap pm;
    TextPage tp;
    memset(&o, 0, sizeof o);
    o.scale = scale; o.paper = 0xFFFFFF; o.annots = 1;
    textpage_init(&tp);
    o.text = &tp;
    pm.rgb = NULL;
    if (pdf_render(d, c, i, &o, &pm)) drawn++;
    free(pm.rgb);
    textpage_finish(&tp);
    textpage_free(&tp);
  }
  rcache_free(c);
  pdf_close(d);
  return drawn;
}
/* the stack below here filled with non-zero bytes, so what a later call leaves uninitialised there
   is garbage rather than (by luck) zero */
static void dirty_stack(void)
{
  volatile unsigned char junk[65536];
  size_t i;
  for (i = 0; i < sizeof junk; i++) junk[i] = (unsigned char)(0xA5 ^ i);
}
static int fails;
/* a check inside a test: the child exits non-zero at the first that fails */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "  check failed, line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static int draw_page(const char *content, const char *res, const Obj *extra, int nextra)
{
  size_t len;
  u8 *pdf = page_pdf(content, res, extra, nextra, &len);
  int n = draw_all(pdf, len, 1.0);
  free(pdf);
  return n;
}

/* ------------------------------------------------------------------ the tests */
/* the cross-reference table rebuilt from a file ending in "trai": "trailer" was compared past the
   end of the data (a heap overread) */
static void t_xref_trailer_at_end(void)
{
  static const char f[] = "%PDF-1.4\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\ntrai";
  u8 *d = (u8 *)malloc(sizeof f - 1);
  memcpy(d, f, sizeof f - 1);
  draw_all(d, sizeof f - 1, 1.0);
  free(d);
}
/* an object whose offset in the table points at something other than "n g obj": two of its three
   tokens were freed without having been read (uninitialised) */
static void t_parse_wrong_offset(void)
{
  size_t len, i;
  Obj o[4] = { { "<< /Type /Catalog /Pages 2 0 R >>", 0 }, { "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", 0 },
               { "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>", 0 }, { "<< /Length 0 >>\nstream\n\nendstream", 0 } };
  u8 *pdf = mkpdf(o, 4, &len);
  char *x = strstr((char *)pdf, "xref\n");
  /* each entry is 20 bytes after "xref\n0 5\n"; object 3's offset moved onto its "<<" */
  char *e3 = x + 5 + 4 + 20 * 3;
  long at = strtol(e3, NULL, 10);
  for (i = (size_t)at; i < len && pdf[i] != '<'; i++) continue;
  {
    char num[12];
    snprintf(num, sizeof num, "%010ld", (long)i);
    memcpy(e3, num, 10);
  }
  /* (and object 4's onto "/Length", a name) */
  {
    char *e4 = e3 + 20, num[12];
    long at4 = strtol(e4, NULL, 10);
    for (i = (size_t)at4; i < len && pdf[i] != '/'; i++) ;
    snprintf(num, sizeof num, "%010ld", (long)i);
    memcpy(e4, num, 10);
  }
  dirty_stack();
  draw_all(pdf, len, 1.0);
  free(pdf);
}
/* scn with a pattern's name and no resources to look it up in, and sc with a name: the operand
   (freed after the operator) was kept as the pattern and used when filling */
static void t_pattern_name_no_resources(void)
{
  CHECK(draw_page("/Pattern cs /P0 scn 10 10 100 100 re f /Pattern CS /P0 SCN 5 w 0 0 m 200 200 l S", NULL, NULL, 0) == 1);
}
static void t_pattern_name_with_sc(void)
{
  size_t n;
  char *pat = stream_obj("/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 10 10] /XStep 10 /YStep 10 /Resources << >>", "1 0 0 rg 0 0 5 5 re f", TEXT, &n);
  Obj ex[1];
  ex[0].s = pat; ex[0].n = n;
  CHECK(draw_page("/Pattern cs /P0 sc 10 10 100 100 re f /Pattern CS /P0 SC 5 w 0 0 m 200 200 l S", "<< /Pattern << /P0 5 0 R >> >>", ex, 1) == 1);
  free(pat);
}
/* a dash pattern with a negative entry never came to an end */
static void t_dash_negative(void)
{
  CHECK(draw_page("2 w [-1 3] 0 d 10 10 m 190 190 l S [-1e300 1e300 1] 0 d 10 190 m 190 10 l S", "<< >>", NULL, 0) == 1);
}
/* dashes far finer than the line: billions of them */
static void t_dash_too_fine(void)
{
  CHECK(draw_page("2 w [0.0000001] 0 d 10 10 m 190 190 l S [1e-30 1e-30] 0 d 0 0 m 200 0 l 200 200 l h S", "<< >>", NULL, 0) == 1);
}
/* numbers too big for a 32-bit long: the lexer's integer overflowed (undefined) on the Amiga; they
   are reals everywhere, and the largest int is still an int */
static void t_lexer_big_numbers(void)
{
  static const char s[] = "2147483647 2147483648 -2147483647 12345678901 99999999999999999999 4294967296.5 007";
  Lex l;
  Token t;
  lex_init(&l, (const u8 *)s, sizeof s - 1, NULL);
  CHECK(lex_next(&l, &t) == TK_INT && t.i == 2147483647L); tok_free(&t);
  CHECK(lex_next(&l, &t) == TK_REAL && t.r == 2147483648.0); tok_free(&t);
  CHECK(lex_next(&l, &t) == TK_INT && t.i == -2147483647L); tok_free(&t);
  CHECK(lex_next(&l, &t) == TK_REAL && t.r == 12345678901.0); tok_free(&t);
  CHECK(lex_next(&l, &t) == TK_REAL && t.r > 9.9e19); tok_free(&t);
  CHECK(lex_next(&l, &t) == TK_REAL && t.r == 4294967296.5); tok_free(&t);
  CHECK(lex_next(&l, &t) == TK_INT && t.i == 7); tok_free(&t);
}
/* a mesh shading with 32-bit coordinates whose data runs out: a shift by 32 (undefined: 0 on the
   68k, the value itself on x86) */
static void t_mesh_shift32(void)
{
  static const char data[] = { 0, 0x7F };
  size_t n4, n5;
  char *sh = stream_obj("/ShadingType 4 /ColorSpace /DeviceRGB /BitsPerCoordinate 32 /BitsPerComponent 8 /BitsPerFlag 8 /Decode [0 200 0 200 0 1 0 1 0 1]", data, sizeof data, &n4);
  char *sh5 = stream_obj("/ShadingType 5 /ColorSpace /DeviceGray /BitsPerCoordinate 32 /BitsPerComponent 16 /VerticesPerRow 2 /Decode [0 200 0 200 0 1]", data, sizeof data, &n5);
  Obj ex[2];
  ex[0].s = sh; ex[0].n = n4; ex[1].s = sh5; ex[1].n = n5;
  CHECK(draw_page("/S4 sh /S5 sh", "<< /Shading << /S4 5 0 R /S5 6 0 R >> >>", ex, 2) == 1);
  free(sh); free(sh5);
}
/* a JPEG whose Huffman table has more codes of a length than there are: its lookup table was
   written far past its end */
static void t_jpeg_overfull_huffman(void)
{
  u8 j[600];
  size_t n = 0;
  int k;
  u8 *rgb;
#define B(x) (j[n++] = (u8)(x))
  B(0xFF); B(0xD8);
  /* a quantisation table, all 1 */
  B(0xFF); B(0xDB); B(0); B(67); B(0); for (k = 0; k < 64; k++) B(1);
  /* baseline, 8 x 8, one component */
  B(0xFF); B(0xC0); B(0); B(11); B(8); B(0); B(8); B(0); B(8); B(1); B(1); B(0x11); B(0);
  /* DC table 0: 200 codes of length 1 (there can only be 2) */
  B(0xFF); B(0xC4); B(0); B(19 + 200); B(0x00); B(200); for (k = 1; k < 16; k++) B(0); for (k = 0; k < 200; k++) B(k & 15);
  /* AC table 0: 2 codes of length 1, then 3 of length 2 (only 0 left there) */
  B(0xFF); B(0xC4); B(0); B(19 + 5); B(0x10); B(2); B(3); for (k = 2; k < 16; k++) B(0); B(0); B(1); B(2); B(3); B(4);
  /* the scan */
  B(0xFF); B(0xDA); B(0); B(8); B(1); B(1); B(0x00); B(0); B(63); B(0);
  for (k = 0; k < 16; k++) B(0x55);
  B(0xFF); B(0xD9);
#undef B
  rgb = img_decode(j, (u32)n, 8, 8);
  free(rgb);
}
/* (tools/fuzz.c) a JPEG with coefficients and quantisation as big as the format allows: the
   dequantising and the inverse DCT overflowed 32 bits */
static void t_jpeg_huge_coefficients(void)
{
  int pass;
  for (pass = 0; pass < 2; pass++) {
    u8 j[1200], acc = 0;
    size_t n = 0;
    int k, nb = 0;
    u8 *rgb;
#define B(x) (j[n++] = (u8)(x))
#define BIT(v) do { acc = (u8)(acc << 1 | (v)); if (++nb == 8) { B(acc); if (acc == 0xFF) B(0); nb = 0; acc = 0; } } while (0)
    B(0xFF); B(0xD8);
    if (!pass) { B(0xFF); B(0xDB); B(0); B(67); B(0x00); for (k = 0; k < 64; k++) B(255); }           /* 8-bit table, all 255 */
    else { B(0xFF); B(0xDB); B(0); B(131); B(0x10); for (k = 0; k < 64; k++) { B(0xFF); B(0xFF); } } /* 16-bit, all 65535 */
    B(0xFF); B(0xC0); B(0); B(11); B(8); B(0); B(8); B(0); B(8); B(1); B(1); B(0x11); B(0);
    /* DC: one code, 0, for 15 bits; AC: 0 for (run 0, 15 bits), 1 for the end of the block */
    B(0xFF); B(0xC4); B(0); B(20); B(0x00); B(1); for (k = 1; k < 16; k++) B(0); B(15);
    B(0xFF); B(0xC4); B(0); B(21); B(0x10); B(2); for (k = 1; k < 16; k++) B(0); B(0x0F); B(0x00);
    B(0xFF); B(0xDA); B(0); B(8); B(1); B(1); B(0x00); B(0); B(63); B(0);
    for (k = 0; k < 64; k++) {
      /* the largest values, the signs mixed so that the transform's sums grow */
      int i, plus = k == 0 || ((k * 7) & 4) != 0;
      BIT(0);
      for (i = 0; i < 15; i++) BIT(plus);
    }
    while (nb) BIT(1);
    B(0xFF); B(0xD9);
#undef BIT
#undef B
    rgb = img_decode(j, (u32)n, 8, 8);
    CHECK(rgb != NULL);
    free(rgb);
  }
}
/* coordinates, sizes and numbers far out of range converted to integers (undefined; the 68040
   and x86 give different answers) in the rasteriser, the glyph cache, clipping, shadings,
   patterns, images and the parser */
static void t_huge_numbers(void)
{
  static const char *const contents[] = {
    "1e20 0 0 1e20 0 0 cm 0 0 1 1 re f",
    "0 0 m 1e15 0 l 0 1e15 l h W n 0 0 200 200 re f",
    "-1e15 -1e15 m 1e15 -1e15 l 0 1e15 l h f",
    "0 0 m 1e30 1e30 -1e30 1e30 5 5 c S",
    "1e300 0 0 1e300 0 0 cm 0 0 m 1 0 l 0 1 l h f",
    "1e200 0 0 1e200 0 0 cm 1e200 0 0 1e200 0 0 cm 0 0 m 1 0 l 0 1 l h f 0 0 1 1 re f",
    "1e300 0 0 1e300 0 0 cm 1e200 1e200 -1e200 -1e200 0 0 cm 0 0 m 10 0 l 0 10 l h W n 0 0 1 1 re f",
    "1e300 0 0 1e300 0 0 cm 1e200 1e200 -1e200 -1e200 0 0 cm 0 0 1 1 re W n 0 0 1 1 re S",
    "BT /F1 12 Tf 1e200 1e200 1e200 1e200 0 0 Tm 1e300 0 Td (Ab) Tj ET",
    "1e300 0 0 1e300 0 0 cm 1e200 1e200 -1e200 -1e200 0 0 cm /Sh1 sh /Sh2 sh /Im2 Do",
    "1e30 J 1e30 j 0 0 m 100 100 l S -1e30 J -1e30 j 0 0 m 100 10 l S",
    "BT 1e30 Tr /F1 12 Tf 10 10 Td (A) Tj ET",
    "BT /F1 12 Tf 1 0 0 1 1e15 1e15 Tm (Ab) Tj 0.001 0 0 0.001 -1e12 5 Tm (Ab) Tj ET",
    "/GS1 gs 0 0 m 100 100 l S",
    "/I1 cs 1e30 sc 0 0 50 50 re f /I1 cs -1e30 sc 0 0 50 50 re f",
    "/Im1 Do 1e10 0 0 1e10 0 0 cm /Im2 Do",
    "/P1 cs /Pat scn 0 0 100 100 re f",
    "/Sh1 sh",
    "1e9 0 0 1e9 -5e8 -5e8 cm /Sh2 sh",
    "1e20 0 0 1e20 0 0 cm BI /W 2 /H 2 /CS /G /BPC 8 ID \x01\x02\x03\x04 EI",
    "q 1e-30 0 0 1e-30 0 0 cm BI /W 2 /H 2 /CS /G /BPC 8 ID \x01\x02\x03\x04 EI Q",
  };
  size_t i, n1, n2, n3, n4, n5;
  char *im1 = stream_obj("/Type /XObject /Subtype /Image /Width 1e20 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8", "abcd", 4, &n1);
  char *im2 = stream_obj("/Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 1e30 /Decode [1e30 -1e30]", "abcd", 4, &n2);
  char *pat = stream_obj("/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1e30 1e30] /XStep 1e30 /YStep 1e30 /Resources << >>", "0 0 5 5 re f", TEXT, &n3);
  char *fn = stream_obj("/FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1]", "{ 1e30 mul dup 3 idiv exch 1e30 mod dup 1e20 bitshift dup 1e30 and 1e30 or not -1e30 -1 idiv -1e30 -1 mod -5 3 bitshift 7 -1e30 bitshift pop pop pop pop }", TEXT, &n4);
  char *fn0 = stream_obj("/FunctionType 0 /Domain [0 1] /Range [0 1] /Size [1e30] /BitsPerSample 8", "ab", 2, &n5);
  Obj ex[9];
  ex[0].s = im1; ex[0].n = n1;                                                                                   /* 5 */
  ex[1].s = im2; ex[1].n = n2;                                                                                   /* 6 */
  ex[2].s = pat; ex[2].n = n3;                                                                                   /* 7 */
  ex[3].s = fn; ex[3].n = n4;                                                                                    /* 8 */
  ex[4].s = "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 1e-300 0] /Function 8 0 R /Extend [true true] >>"; ex[4].n = 0; /* 9 */
  ex[5].s = "<< /Type /Font /Subtype /TrueType /BaseFont /Helvetica /FirstChar 1e30 /Widths [1e30] /Encoding << /Differences [1e30 /A -1e30 /B] >> >>"; ex[5].n = 0; /* 10 */
  ex[6].s = "<< /LW 1e30 /LC 1e30 /LJ -1e30 /CA 1e30 /ca -1e30 >>"; ex[6].n = 0;                         /* 11 */
  ex[7].s = "<< /ShadingType 3 /ColorSpace /DeviceGray /Coords [0 0 0 1e30 1e30 1e30] /Function 13 0 R /Extend [true true] >>"; ex[7].n = 0; /* 12 */
  ex[8].s = fn0; ex[8].n = n5;                                                                                   /* 13 */
  for (i = 0; i < sizeof contents / sizeof contents[0]; i++)
    CHECK(draw_page(contents[i], "<< /Font << /F1 10 0 R >> /XObject << /Im1 5 0 R /Im2 6 0 R >> /Pattern << /Pat 7 0 R >> "
                    "/Shading << /Sh1 9 0 R /Sh2 12 0 R >> /ExtGState << /GS1 11 0 R >> "
                    "/ColorSpace << /I1 [/Indexed /DeviceRGB 1e30 <000000FFFFFF>] /P1 [/Pattern] >> >>", ex, 9) == 1);
  free(im1); free(im2); free(pat); free(fn); free(fn0);
}
/* page attributes, lengths and table entries out of range */
static void t_huge_numbers_parser(void)
{
  size_t len, n;
  char *cs = stream_obj("", "0 0 10 10 re f", 14, &n);
  Obj o[5] = { { "<< /Type /Catalog /Pages 2 0 R /OpenAction [1e30 /XYZ 0 1e30 0] /Outlines << /First 5 0 R >> >>", 0 },
               { "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", 0 },
               { "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 1e30 200] /Rotate 1e30 /Contents 4 0 R >>", 0 },
               { "<< /Length 1e30 >>\nstream\n0 0 10 10 re f\nendstream", 0 },
               { "<< /Title (x) /Dest [1e30 /Fit] >>", 0 } };
  u8 *pdf = mkpdf(o, 5, &len);
  POutline *ol;
  char err[100];
  PDoc *d;
  free(cs);
  draw_all(pdf, len, 0.0001);
  d = pdf_open(pdf, len, err, sizeof err);
  if (d) {
    if ((ol = pdf_outline(d))) pdf_outline_free(ol);
    pdf_close(d);
  }
  free(pdf);
}
/* (tools/fuzz.c) a document saved again with a real too long for the writer's 40 characters
   (snprintf's length used as an index past them), and reals that can't be written at all */
/* (tools/fuzz.c, -m32) a page far too big to draw: its pixel count overflowed a 32-bit long */
static void t_huge_page(void)
{
  size_t len;
  Obj o[4] = { { "<< /Type /Catalog /Pages 2 0 R >>", 0 }, { "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", 0 },
               { "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 3e9 300] /Contents 4 0 R >>", 0 },
               { "<< /Length 0 >>\nstream\n\nendstream", 0 } };
  u8 *pdf = mkpdf(o, 4, &len);
  CHECK(draw_all(pdf, len, 1.0) == 0);
  CHECK(draw_all(pdf, len, 1e-9) == 1);
  free(pdf);
}
/* cross-reference streams and tables with fields too big for a 32-bit long (8-byte offsets, object
   numbers near 2^31, negative offsets), which overflowed it or converted out of range */
static void t_xref_out_of_range(void)
{
  Buf b;
  long x;
  int k, v;
  for (v = 0; v < 2; v++) {
    buf_init(&b);
    buf_adds(&b, "%PDF-1.5\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n");
    x = (long)b.len;
    if (!v) {
      buf_adds(&b, "3 0 obj\n<< /Type /XRef /Root 1 0 R /W [1 8 1] /Index [0 1 2147483000 1] /Size 3 /XRefStm -5 /Prev -3 /Length 20 >>\nstream\n");
      for (k = 0; k < 2; k++) { int i; buf_addc(&b, 1); for (i = 0; i < 8; i++) buf_addc(&b, 0xFF); buf_addc(&b, 0); }
      buf_adds(&b, "\nendstream\nendobj\n");
    } else {
      buf_adds(&b, "xref\n2147483640 3\n0000000009 00000 n \n0000000009 00000 n \n0000000009 00000 n \n"
                   "trailer\n<< /Size 3 /Root 1 0 R /XRefStm -7 /Prev 99999999999999999999 >>\n");
    }
    buf_printf(&b, "startxref\n%ld\n%%%%EOF\n", x);
    draw_all((const u8 *)b.data, b.len, 1.0);
    buf_free(&b);
  }
}
static int near(double a, double b) { return a / b > 0.999999 && a / b < 1.000001; }
static void t_save_huge_real(void)
{
  size_t len, n;
  Obj o[4] = { { "<< /Type /Catalog /Pages 2 0 R >>", 0 }, { "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", 0 },
               { "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Big 1e40 /Small -3e44 /Inf 4e400 >>", 0 },
               { "<< /Length 0 >>\nstream\n\nendstream", 0 } };
  u8 *pdf = mkpdf(o, 4, &len), *out;
  char err[100];
  PDoc *d = pdf_open(pdf, len, err, sizeof err), *d2;
  CHECK(d && d->npages == 1);
  CHECK(near(pdf_dnum(d, d->pages[0], "Big", 0), 1e40));
  CHECK((out = pdf_save(d, NULL, 0, &n)) != NULL);
  CHECK((d2 = pdf_open(out, n, err, sizeof err)) && d2->npages == 1);
  CHECK(near(pdf_dnum(d2, d2->pages[0], "Big", 0), 1e40));
  CHECK(near(pdf_dnum(d2, d2->pages[0], "Small", 0), -3e44));
  CHECK(pdf_dnum(d2, d2->pages[0], "Inf", 1) == 0);
  pdf_close(d2); pdf_close(d);
  free(out); free(pdf);
}

/* ------------------------------------------------------------------ running them */
typedef struct { const char *name; void (*fn)(void); } Test;
static const Test tests[] = {
  { "xref_trailer_at_end", t_xref_trailer_at_end },
  { "parse_wrong_offset", t_parse_wrong_offset },
  { "pattern_name_no_resources", t_pattern_name_no_resources },
  { "pattern_name_with_sc", t_pattern_name_with_sc },
  { "dash_negative", t_dash_negative },
  { "dash_too_fine", t_dash_too_fine },
  { "lexer_big_numbers", t_lexer_big_numbers },
  { "mesh_shift32", t_mesh_shift32 },
  { "jpeg_overfull_huffman", t_jpeg_overfull_huffman },
  { "jpeg_huge_coefficients", t_jpeg_huge_coefficients },
  { "huge_numbers", t_huge_numbers },
  { "huge_numbers_parser", t_huge_numbers_parser },
  { "save_huge_real", t_save_huge_real },
  { "huge_page", t_huge_page },
  { "xref_out_of_range", t_xref_out_of_range },
};
static const char *current;
static void on_alarm(int s)
{
  static const char m[] = "  HANG: over the time limit\n";
  (void)s;
  if (write(2, m, sizeof m - 1) < 0) { }
  _exit(3);
}
int main(int argc, char **argv)
{
  size_t i;
  int ran = 0, a;
  for (i = 0; i < sizeof tests / sizeof tests[0]; i++) {
    pid_t pid;
    int st = 0;
    if (argc > 1) {
      for (a = 1; a < argc && strcmp(argv[a], tests[i].name); a++) ;
      if (a == argc) continue;
    }
    current = tests[i].name;
    ran++;
    fflush(stdout);
    if ((pid = fork()) == 0) {
      signal(SIGALRM, on_alarm);
      alarm(TIME_LIMIT);
      tests[i].fn();
      font_free_substitutes();
      _exit(0);
    }
    if (pid < 0 || waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st)) {
      printf("FAIL %s (%s %d)\n", current, pid > 0 && WIFSIGNALED(st) ? "signal" : "exit", pid > 0 && WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
      fails++;
    } else printf("ok   %s\n", current);
  }
  printf("robusttest (%d-bit long): %d tests, %d failed\n", (int)sizeof(long) * 8, ran, fails);
  return fails != 0;
}
