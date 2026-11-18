/*
 * pdfwrite.h - saving: the document (with whatever was changed in memory: annotations added,
 * pages turned, moved or deleted) written as a new PDF. Plainly by default; with SaveOpts, its
 * streams compressed, its objects packed into object streams, and/or protected with a password
 * (AES-128 or AES-256, the permissions given).
 */
#ifndef FOLIO_PDFWRITE_H
#define FOLIO_PDFWRITE_H

#include "pdf.h"

/* the /P permission bits (ISO 32000 table 22) */
#define PERM_PRINT    (1L << 2)
#define PERM_MODIFY   (1L << 3)
#define PERM_COPY     (1L << 4)
#define PERM_ANNOTATE (1L << 5)
#define PERM_FILL     (1L << 8)
#define PERM_ACCESS   (1L << 9)
#define PERM_ASSEMBLE (1L << 10)
#define PERM_PRINTHQ  (1L << 11)
#define PERM_ALL      (PERM_PRINT | PERM_MODIFY | PERM_COPY | PERM_ANNOTATE | PERM_FILL | PERM_ACCESS | PERM_ASSEMBLE | PERM_PRINTHQ)

enum { SAVE_PLAIN = 0, SAVE_AES128 = 2, SAVE_AES256 = 3 };
typedef struct {
  int compress;             /* streams without a filter Flate-compressed (when that is smaller) */
  int level;                /* the deflate level for them (1-9; 0: 6) */
  int objstm;               /* objects other than streams packed into object streams, with a cross-reference stream */
  int crypt;                /* SAVE_: protected with a password */
  const char *user_pw;      /* opens the document ("" or NULL: anyone may open it, the permissions still apply) */
  const char *owner_pw;     /* lifts the permissions (NULL or "": a random one) */
  long perms;               /* what may be done without the owner password: PERM_ bits (0: PERM_ALL) */
  const int *alias;         /* (optimizing) old object number -> another old number holding the same object, written once (0 none) */
  int nalias;
} SaveOpts;

/* the pages listed in order (indices into d->pages; NULL: all of them as they are) written out;
   malloc'd, *len its size; NULL if out of memory */
u8 *pdf_save(PDoc *d, const int *order, int n, size_t *len);
/* the same with options (o NULL: as pdf_save) */
u8 *pdf_save_opts(PDoc *d, const int *order, int n, const SaveOpts *o, size_t *len);
/* bytes that are hard to guess (for keys, salts and file identifiers): SHA-256 of the clock, a counter and seed */
void pdf_random(u8 *out, int n, const void *seed, size_t seedlen);
#endif
