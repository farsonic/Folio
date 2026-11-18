/*
 * scan.h - scanned pages: text recognition (OCR) and the clean-up of scans (straightening,
 * speckle removal, a white background, camera pictures evened out, black and white pictures
 * stored as CCITT Group 4).
 *
 * The OCR is Folio's own and small: the page is drawn at 200-300 dpi, thresholded, its ink split
 * into connected pieces, the pieces gathered into lines, glyphs (dots and accents joined to their
 * letters, touching letters split again where that matches better) and words, and each glyph is
 * matched against templates: the same characters drawn by Folio's renderer in its own fonts
 * (Helvetica, Times and Courier and their substitutes in the fonts drawer: Liberation, Carlito,
 * Caladea) at several sizes, compared by their shape on a 10 x 10 grid, their proportions and
 * where they sit against the line's baseline and x-height. It reads clean printed text in those
 * kinds of type; handwriting, unusual typefaces, pictures and tables don't come out.
 * What it reads is laid over the page as invisible text (search, select, copy).
 */
#ifndef FOLIO_SCAN_H
#define FOLIO_SCAN_H
#include "pdf.h"
#include "render.h"

typedef struct { double box[4]; char text[64]; float conf; } OcrWord;   /* box: page units; text: Latin-1 */
typedef struct { OcrWord *w; int n, cap; int lines; } OcrResult;
/* page recognised (drawn at dpi, 0: 200; abort may stop it): 1 if it ran (res->n words, maybe 0) */
int  ocr_page(PDoc *d, RCache *rc, int page, int dpi, int (*abort)(void *), void *ud, OcrResult *res);
void ocr_free(OcrResult *r);
/* the words laid over the page as invisible text (a stream marked "OCR": done again, it replaces it) */
int  ocr_add_text_layer(PDoc *d, int page, const OcrResult *r);
/* the words as text, line by line (Latin-1, malloc'd) */
char *ocr_text(const OcrResult *r);
/* the templates: made at the first ocr_page from the renderer's fonts (font_set_dir); made again after this */
void ocr_forget_templates(void);

typedef struct {
  int deskew;               /* straightened (up to 10 degrees) */
  int despeckle;            /* specks of a few pixels taken out */
  int whiten;               /* the paper made white, the ink black (levels from the histogram) */
  int camera;               /* uneven light evened out (a photograph of a page) */
  int bitonal;              /* 1: stored black and white as CCITT Group 4; 2: only if it is nearly so already */
  int quality;              /* JPEG quality for grey or colour results (0: 75) */
} ScanOpts;
/* the page's scan (its biggest picture, covering most of the page) cleaned up as the options say:
   1 if it changed. *angle (may be NULL): the skew found, degrees */
int  scan_clean_page(PDoc *d, int page, const ScanOpts *o, double *angle);
/* the skew of a grey picture (w x h, 0 black .. 255 white), degrees (positive: turned anticlockwise) */
double scan_skew(const u8 *grey, int w, int h);
#endif
