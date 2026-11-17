/*
 * pdfwrite.h - saving: the document (with whatever was changed in memory: annotations added,
 * pages turned, moved or deleted) written as a new, unencrypted PDF.
 */
#ifndef FOLIO_PDFWRITE_H
#define FOLIO_PDFWRITE_H

#include "pdf.h"

/* the pages listed in order (indices into d->pages; NULL: all of them as they are) written out;
   malloc'd, *len its size; NULL if out of memory */
u8 *pdf_save(PDoc *d, const int *order, int n, size_t *len);
#endif
