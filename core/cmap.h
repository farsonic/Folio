/*
 * cmap.h - CMaps: how a composite font's string bytes split into codes and become CIDs, and how a
 * ToUnicode CMap turns codes into text (for searching and copying).
 */
#ifndef FOLIO_CMAP_H
#define FOLIO_CMAP_H

#include "pdf.h"

typedef struct CMap CMap;
/* a CMap from its stream data; NULL if it has nothing usable */
CMap *cmap_parse(const u8 *data, size_t n);
/* Identity-H / Identity-V: two bytes a code, the code the CID */
CMap *cmap_identity(int vertical);
void  cmap_free(CMap *m);
int   cmap_vertical(const CMap *m);
/* the next code from s (len bytes left): its value and length in bytes (at least 1) */
int   cmap_next(const CMap *m, const u8 *s, int len, u32 *code);
/* the CID for a code (0 if unmapped) */
int   cmap_cid(const CMap *m, u32 code, int nbytes);
/* the text for a code: up to max code points into out; the number written (0 if unmapped) */
int   cmap_unicode(const CMap *m, u32 code, int nbytes, u32 *out, int max);
#endif
