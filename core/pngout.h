/*
 * pngout.h - writing a rendered page (RGB rows, 3 bytes a pixel) as a PNG file.
 */
#ifndef FOLIO_PNGOUT_H
#define FOLIO_PNGOUT_H
#include "util.h"
/* the file's bytes (malloc'd, *len long); NULL if out of memory. dpi 0: no resolution recorded */
u8 *png_write_rgb(const u8 *rgb, int w, int h, int dpi, size_t *len);
#endif
