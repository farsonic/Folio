/*
 * jpegout.h - baseline JPEG written from RGB rows (3 bytes a pixel, top down).
 */
#ifndef FOLIO_JPEGOUT_H
#define FOLIO_JPEGOUT_H
#include "util.h"
/* the file's bytes (malloc'd, *len long), quality 1-100; NULL if out of memory */
u8 *jpeg_write_rgb(const u8 *rgb, int w, int h, int quality, size_t *len);
#endif
