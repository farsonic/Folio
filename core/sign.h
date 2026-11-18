/*
 * sign.h - signing a PDF with a certificate the user supplies (core/pki.c reads it): a signature
 * field (a new one, visible or invisible, or an empty one the document has), its signature
 * dictionary (adbe.pkcs7.detached, CMS SignedData with SHA-256), approval or certification
 * (DocMDP), written as an incremental update so that earlier signatures stay valid.
 */
#ifndef FOLIO_SIGN_H
#define FOLIO_SIGN_H
#include "pdf.h"
#include "pki.h"

typedef struct {
  int page; double rect[4];         /* where the signature shows (rect[2] <= rect[0]: invisible) */
  const char *field;                /* an empty signature field to sign (its full name), NULL: a new field */
  const char *reason, *location;    /* UTF-8, may be NULL */
  int certify;                      /* 0 an approval signature; 1-3 certifies with DocMDP P 1 (no changes),
                                       2 (filling in forms and signing), 3 (also comments) */
  int hash;                         /* HASH_SHA256 (0) normally */
  const char *when;                 /* YYYYMMDDHHMMSS (NULL: now, the local time) */
  const char *label;                /* the appearance's first words (NULL: "Digitally signed by"), Latin-1 */
} SignOpts;
/* the document signed: the whole file (malloc'd, *len), NULL on failure with err set. d itself
   gets the signature field (when it can be saved incrementally); open the file returned to go on */
u8 *pdf_sign(PDoc *d, const SignerId *id, const SignOpts *o, size_t *len, char *err, int ecap);
#endif
