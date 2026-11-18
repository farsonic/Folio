/*
 * review.h - checking documents: Compare Files (the text of two documents compared line by line,
 * the changes listed and highlighted in the newer one); the accessibility checker, reading
 * options, alternate text, simple automatic tagging and the tags as a list; digital signatures
 * listed and checked (does the signed part of the file still hash to what the signature says);
 * Save as PDF/A (the parts Folio can supply: XMP identification, an sRGB output intent, no
 * scripts or encryption; what it can't fix, such as fonts that aren't embedded, is reported).
 */
#ifndef FOLIO_REVIEW_H
#define FOLIO_REVIEW_H

#include "pdf.h"
#include "textpage.h"

typedef TextPage *(*ReviewText)(void *ud, int page);

/* ------------------------------------------------------------------ compare */
typedef struct { int page_a, page_b; int kind; char *text; } Change;   /* kind 1 inserted (in b), 2 deleted (from a) */
typedef struct { Change *c; int n; int pages_a, pages_b; } Diff;
/* a and b compared; with mark, the inserted lines are highlighted in b (and deletions noted in
   sticky notes there). Returns the number of changes */
int  compare_docs(PDoc *a, ReviewText ga, void *uda, PDoc *b, ReviewText gb, void *udb, int mark, Diff *out);
void diff_free(Diff *df);
/* the changes as a report (UTF-8, malloc'd) */
char *diff_report(const Diff *df);

/* ------------------------------------------------------------------ accessibility */
enum { A11Y_PASS, A11Y_FAIL, A11Y_MANUAL };
typedef struct { const char *rule; int status; char detail[120]; } A11yItem;
/* the checks (malloc'd into *out); how many */
int  a11y_check(PDoc *d, ReviewText get, void *ud, A11yItem **out);
char *a11y_report(const A11yItem *it, int n);
/* language (e.g. "en-GB"), title shown in the window's title bar */
int  a11y_reading_options(PDoc *d, const char *lang, int display_title);
/* the figures in the structure tree: their alternate texts (malloc'd array of malloc'd strings, NULL
   where none); how many. a11y_set_alt sets figure i's */
int  a11y_figures(PDoc *d, char ***alts);
int  a11y_set_alt(PDoc *d, int i, const char *alt);
/* every page's content tagged as one paragraph each in a Document structure (pages that have
   tags already are left alone), form fields as Form elements; pages tagged */
int  a11y_autotag(PDoc *d, int fields);
/* pages with content outside marked content (untagged); malloc'd list, how many */
int  a11y_untagged(PDoc *d, int **pages);
/* the structure tree as indented lines (UTF-8, malloc'd) */
char *a11y_tags(PDoc *d);

/* ------------------------------------------------------------------ signatures */
typedef struct {
  char name[96], reason[96], date[40], field[96];
  int covers_all;           /* the signed byte ranges reach the end of the file (nothing appended) */
  int digest_ok;            /* the signed bytes hash to the signature's message digest: 1 yes, 0 no, -1 couldn't tell */
  int certify;              /* a certification (DocMDP) signature */
  /* (after 0.1.2, round 2) the CMS signature and certificates checked (core/pki.c) */
  int sig_ok;               /* the signature matches the signer's certificate: 1, 0, -1 couldn't tell */
  int chain;                /* CHAIN_: how far the certificates lead (to one trusted: CHAIN_TRUSTED) */
  int time_ok;              /* signed within the certificate's validity: 1, 0, -1 not known */
  char signer[96], issuer[96], valid_to[16], why[120];
  u8 *cert; size_t certlen; /* the signer's certificate (malloc'd copy; sig_free) */
} SigInfo;
int  sig_list(PDoc *d, SigInfo **out);
void sig_free(SigInfo *si, int n);
/* the certificates (DER) trusted when signatures are checked: borrowed until changed */
void sig_trust(const u8 *const *certs, const size_t *lens, int n);

/* ------------------------------------------------------------------ PDF/A */
/* the document prepared for PDF/A-2B; what can't be fixed listed in problems (cap bytes); 1 if
   nothing was left */
int  pdfa_convert(PDoc *d, char *problems, int cap);
#endif
