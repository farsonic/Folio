/*
 * pdf.h - reading PDF files: the objects, the cross-reference table (or its reconstruction when
 * it is damaged), streams and their filters, the Standard Security Handler (RC4 and AES, so
 * files "protected" only against changes open as usual), the page tree, the outline
 * (bookmarks), named destinations and the document information.
 * Portable C99 (the native tests and vbcc for the Amiga).
 *
 * The file is kept in memory whole. Indirect objects are parsed when first wanted and kept until
 * the document is closed, so the pointers pdf_get and pdf_resolve return stay good until then.
 * Objects made otherwise (content stream operands, pdf_parse_object) belong to the caller, who
 * frees them with pobj_free.
 */
#ifndef FOLIO_PDF_H
#define FOLIO_PDF_H

#include "util.h"

enum { PO_NULL, PO_BOOL, PO_INT, PO_REAL, PO_NAME, PO_STRING, PO_ARRAY, PO_DICT, PO_REF, PO_STREAM };
typedef struct PObj PObj;
struct PObj {
  u8 type;
  union {
    int b;
    long i;
    double r;
    struct { char *s; int len; } str;                 /* names and strings (a name without its slash) */
    struct { PObj **v; int n, cap; } arr;
    struct { char **k; PObj **v; int n, cap; } dict;  /* (streams: their dictionary) */
    struct { int num, gen; } ref;
  } u;
  /* a stream: where its data starts in the file and how long it is (or, for one made in memory, data) */
  long soff, slen;
  u8 *sdata;
  int onum, ogen;                                    /* the indirect object a stream belongs to (decryption) */
};

typedef struct {
  long off;                 /* type 1: where it is in the file; type 2: the object stream's number */
  int gen;                  /* type 1: generation; type 2: index within the object stream */
  u8 type;                  /* 0 free, 1 in the file, 2 in an object stream */
  u8 loading;               /* (being parsed: a reference back to it is a loop) */
  PObj *obj;                /* parsed, once wanted */
} XEntry;

typedef struct PDoc {
  u8 *data; size_t len;     /* the whole file (the document's own copy) */
  XEntry *xref; int nxref;
  PObj *trailer;
  int repaired;             /* the cross-reference table had to be rebuilt by scanning */
  /* encryption */
  int crypt;                /* 0 none, 1 RC4, 2 AES-128, 3 AES-256 */
  int crypt_strings, crypt_streams;   /* (V4/V5 crypt filters: what each uses, as crypt) */
  u8 key[32]; int keylen;
  int encrypt_num;          /* the /Encrypt dictionary's object (never decrypted itself) */
  int perms;                /* the /P permissions */
  int need_password;        /* the user password isn't empty: pdf_password must be given one */
  /* pages, in order: their dictionaries (borrowed from the cache) */
  PObj **pages; int npages;
  int changed;              /* changed in memory since it was opened or saved */
  void *ostm;               /* (decoded object streams, kept for the next object from the same one) */
  char err[160];
} PDoc;

/* ------------------------------------------------------------------ objects */
PObj *pobj_new(int type);
void  pobj_free(PObj *o);
PObj *pobj_int(long v);
PObj *pobj_real(double v);
PObj *pobj_name(const char *s);
PObj *pobj_string(const char *s, int len);
PObj *pobj_copy(const PObj *o);                     /* a deep copy (streams: their data copied too) */
void  pobj_array_add(PObj *a, PObj *item);          /* takes item */
void  pobj_dict_set(PObj *d, const char *key, PObj *v);   /* takes v; replaces an existing key */
void  pobj_dict_del(PObj *d, const char *key);
PObj *pobj_dict_get(const PObj *d, const char *key);      /* the value as stored (may be a reference) */
int   pobj_is_name(const PObj *o, const char *s);
/* number of either kind (0 if not a number) */
double pobj_num(const PObj *o);
int   pobj_isnum(const PObj *o);

/* ------------------------------------------------------------------ the document */
/* a file (data copied); NULL with a message in err when it isn't a PDF at all. A damaged file is
   repaired as far as it can be (doc->repaired) */
PDoc *pdf_open(const u8 *data, size_t len, char *err, size_t errcap);
int   pdf_ensure_pages(PDoc *d);                    /* (after pdf_password) the pages found; their number */
void  pdf_close(PDoc *d);
/* the user (or owner) password, if doc->need_password: 1 if it opens the file */
int   pdf_password(PDoc *d, const char *pw);
/* object num (generation ignored), parsed and cached; NULL if missing */
PObj *pdf_get(PDoc *d, int num);
/* o itself, or what it refers to (following chains of references); NULL for a missing object */
PObj *pdf_resolve(PDoc *d, PObj *o);
/* d[key] resolved; and the conveniences on top */
PObj *pdf_dget(PDoc *d, const PObj *dict, const char *key);
double pdf_dnum(PDoc *d, const PObj *dict, const char *key, double def);
long  pdf_dint(PDoc *d, const PObj *dict, const char *key, long def);
const char *pdf_dname(PDoc *d, const PObj *dict, const char *key);   /* NULL if not a name */
PObj *pdf_dict_of(PDoc *d, PObj *o);                /* o resolved, if a dictionary or a stream (its dictionary); else NULL */
/* a stream's data, every filter undone (images: DCT, JPX, JBIG2 and CCITT are left for the caller
   when raw is set at the first such filter: *last_filter gets its name, *parms its parameters).
   malloc'd, *len its length; NULL on failure */
u8   *pdf_stream_data(PDoc *d, PObj *stream, size_t *len);
u8   *pdf_stream_bytes(PDoc *d, PObj *stream, size_t *len);     /* as stored, only decrypted */
/* a new indirect object (taken); its number */
int   pdf_new_object(PDoc *d, PObj *o);
u8   *pdf_stream_data_raw(PDoc *d, PObj *stream, size_t *len, const char **image_filter, PObj **parms);
/* a string's bytes decrypted (strings in the file are decrypted as they are parsed; this is for
   tests) */

/* ------------------------------------------------------------------ parsing from memory */
/* a lexer over a buffer: content streams, CMaps, Type 4 functions */
typedef struct { const u8 *p, *end; PDoc *doc; } Lex;
enum { TK_EOF, TK_INT, TK_REAL, TK_NAME, TK_STRING, TK_HEXSTRING, TK_KEYWORD, TK_ARRAY_OPEN, TK_ARRAY_CLOSE,
       TK_DICT_OPEN, TK_DICT_CLOSE, TK_BRACE_OPEN, TK_BRACE_CLOSE, TK_ERROR };
typedef struct { int type; long i; double r; char *s; int len; } Token;   /* s: malloc'd for names, strings, keywords */
void  lex_init(Lex *l, const u8 *p, size_t n, PDoc *doc);
int   lex_next(Lex *l, Token *t);                   /* the token's type; free t->s afterwards (tok_free) */
void  tok_free(Token *t);
/* the next object (arrays and dictionaries whole); NULL at the end or on an error. *kw gets a
   keyword instead (an operator), malloc'd, when the next token is one */
PObj *lex_object(Lex *l, char **kw);
void  lex_skip_ws(Lex *l);

/* ------------------------------------------------------------------ the page tree */
/* a page's attribute, inherited from its ancestors if it hasn't its own (Resources, MediaBox,
   CropBox, Rotate) */
PObj *pdf_page_attr(PDoc *d, PObj *page, const char *key);
/* the page's box (CropBox within MediaBox, normalised so x0 < x1) and its rotation (0, 90, 180, 270) */
void  pdf_page_box(PDoc *d, int page, double box[4], int *rotate);
/* the number of a page dictionary (by its object number), -1 if it isn't one */
int   pdf_page_index(PDoc *d, PObj *page);

/* ------------------------------------------------------------------ outline and destinations */
typedef struct POutline {
  char *title;              /* UTF-8 */
  int page;                 /* the page it goes to, -1 none */
  double top;               /* where on the page (PDF units from the bottom), -1 not given */
  char *uri;                /* or a web address */
  int open;                 /* shown open (positive /Count) */
  struct POutline *child, *next;
} POutline;
POutline *pdf_outline(PDoc *d);                     /* NULL if there is none */
void  pdf_outline_free(POutline *o);
/* a destination (array, name or string, via /Dests or the name tree): its page and top; 0 if none */
int   pdf_dest(PDoc *d, PObj *dest, int *page, double *top);
/* a text string (PDFDocEncoding or UTF-16BE with a BOM, or UTF-8 with its BOM) as UTF-8, malloc'd */
char *pdf_text_string(const PObj *s);
/* the document information entry key (Title, Author, Subject, Keywords, Creator, Producer,
   CreationDate, ModDate) as UTF-8, malloc'd; NULL if absent */
char *pdf_info(PDoc *d, const char *key);
#endif
