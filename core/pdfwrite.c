/*
 * pdfwrite.c - a document written out as a new PDF (see pdfwrite.h): every object reachable from
 * the catalog renumbered and written plainly (decrypted, object streams unpacked), the page tree
 * rebuilt flat from the pages in the order given, with inherited attributes copied into each page.
 * Pages left out (an extract, or deleted ones) become null wherever something else refers to them.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "pdfwrite.h"

typedef struct {
  PDoc *d;
  int *map;                 /* old object number -> new (0 not yet, -1 excluded) */
  int nmap;
  int *queue; int nq, capq; /* old numbers waiting to be written */
  long *offs; int noffs, capoffs;
  int next;                 /* the next new number */
  Buf out;
  int pages_num;            /* the new page tree's number */
  PObj **pages; int npages; /* the pages kept, in order */
  int *page_new;            /* their new numbers */
} W;

static int wmap(W *w, int old)
{
  if (old <= 0 || old >= w->nmap) return 0;
  if (w->map[old] == -1) return 0;
  if (!w->map[old]) {
    if (!pdf_get(w->d, old)) return 0;
    w->map[old] = w->next++;
    if (w->nq >= w->capq) {
      int nc = w->capq ? w->capq * 2 : 256;
      int *q = (int *)realloc(w->queue, (size_t)nc * sizeof(int));
      if (!q) return 0;
      w->queue = q; w->capq = nc;
    }
    w->queue[w->nq++] = old;
  }
  return w->map[old];
}
static int regular(int c) { return c > 32 && c < 127 && !strchr("()<>[]{}/%#", c); }
static void put_name(Buf *b, const char *s, int len)
{
  int i;
  buf_addc(b, '/');
  for (i = 0; i < len; i++) {
    u8 c = (u8)s[i];
    if (regular(c)) buf_addc(b, c); else buf_printf(b, "#%02X", c);
  }
}
static void put_string(Buf *b, const char *s, int len)
{
  int i, bin = 0;
  for (i = 0; i < len; i++) if ((u8)s[i] < 32 && s[i] != 10 && s[i] != 13 && s[i] != 9) bin++;
  if (bin > len / 8) {
    buf_addc(b, '<');
    for (i = 0; i < len; i++) buf_printf(b, "%02X", (u8)s[i]);
    buf_addc(b, '>');
    return;
  }
  buf_addc(b, '(');
  for (i = 0; i < len; i++) {
    u8 c = (u8)s[i];
    if (c == '(' || c == ')' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
    else if (c == 13) buf_adds(b, "\\r");
    else if (c < 32 && c != 10 && c != 9) buf_printf(b, "\\%03o", c);
    else buf_addc(b, c);
  }
  buf_addc(b, ')');
}
static void put_num(Buf *b, double v)
{
  char t[40];
  int n;
  if (fabs(v) < 1e-9) v = 0;
  if (v == floor(v) && fabs(v) < 2e9) { buf_printf(b, "%ld", (long)v); return; }
  n = snprintf(t, sizeof t, "%.6f", v);
  while (n > 0 && t[n - 1] == '0') t[--n] = 0;
  if (n > 0 && t[n - 1] == '.') t[--n] = 0;
  buf_adds(b, t);
}
static void put_obj(W *w, const PObj *o, int depth)
{
  Buf *b = &w->out;
  int i;
  if (!o || depth > 64) { buf_adds(b, "null"); return; }
  switch (o->type) {
  case PO_NULL: buf_adds(b, "null"); break;
  case PO_BOOL: buf_adds(b, o->u.b ? "true" : "false"); break;
  case PO_INT: buf_printf(b, "%ld", o->u.i); break;
  case PO_REAL: put_num(b, o->u.r); break;
  case PO_NAME: put_name(b, o->u.str.s, o->u.str.len); break;
  case PO_STRING: put_string(b, o->u.str.s, o->u.str.len); break;
  case PO_ARRAY:
    buf_addc(b, '[');
    for (i = 0; i < o->u.arr.n; i++) { if (i) buf_addc(b, ' '); put_obj(w, o->u.arr.v[i], depth + 1); }
    buf_addc(b, ']');
    break;
  case PO_DICT: case PO_STREAM:
    buf_adds(b, "<<");
    for (i = 0; i < o->u.dict.n; i++) {
      put_name(b, o->u.dict.k[i], (int)strlen(o->u.dict.k[i]));
      buf_addc(b, ' ');
      put_obj(w, o->u.dict.v[i], depth + 1);
      buf_addc(b, '\n');
    }
    buf_adds(b, ">>");
    break;
  case PO_REF: {
    int n = wmap(w, o->u.ref.num);
    if (n) buf_printf(b, "%d 0 R", n); else buf_adds(b, "null");
    break; }
  }
}
/* one object written as "n 0 obj ... endobj" at the current offset */
static void begin_obj(W *w, int num)
{
  if (num >= w->capoffs) {
    int nc = w->capoffs ? w->capoffs * 2 : 512;
    long *o;
    while (nc <= num) nc *= 2;
    if (!(o = (long *)realloc(w->offs, (size_t)nc * sizeof(long)))) { w->out.oom = 1; return; }
    memset(o + w->capoffs, 0, (size_t)(nc - w->capoffs) * sizeof(long));
    w->offs = o; w->capoffs = nc;
  }
  w->offs[num] = (long)w->out.len;
  if (num + 1 > w->noffs) w->noffs = num + 1;
  buf_printf(&w->out, "%d 0 obj\n", num);
}
static void write_value(W *w, int num, PObj *o)
{
  begin_obj(w, num);
  if (o && o->type == PO_STREAM) {
    size_t len = 0;
    u8 *data = pdf_stream_bytes(w->d, o, &len);
    PObj *dict = pobj_new(PO_DICT);
    int i;
    for (i = 0; dict && i < o->u.dict.n; i++) if (strcmp(o->u.dict.k[i], "Length")) pobj_dict_set(dict, o->u.dict.k[i], pobj_copy(o->u.dict.v[i]));
    pobj_dict_set(dict, "Length", pobj_int((long)len));
    put_obj(w, dict, 0);
    pobj_free(dict);
    buf_adds(&w->out, "\nstream\n");
    if (data) buf_add(&w->out, data, len);
    buf_adds(&w->out, "\nendstream");
    free(data);
  } else put_obj(w, o, 0);
  buf_adds(&w->out, "\nendobj\n");
}
u8 *pdf_save(PDoc *d, const int *order, int n, size_t *outlen)
{
  W w;
  int i, root_new, info_new = 0, k;
  PObj *root = pdf_dget(d, d->trailer, "Root"), *cat, *info_ref = pobj_dict_get(d->trailer, "Info");
  u8 *res = NULL;
  memset(&w, 0, sizeof w);
  w.d = d;
  if (!root) return NULL;
  if (!order) n = d->npages;
  if (n < 1) return NULL;
  w.nmap = d->nxref + 1;
  if (!(w.map = (int *)calloc((size_t)w.nmap, sizeof(int))) || !(w.pages = (PObj **)calloc((size_t)n, sizeof(PObj *))) ||
      !(w.page_new = (int *)calloc((size_t)n, sizeof(int)))) goto done;
  buf_init(&w.out);
  buf_adds(&w.out, "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
  w.next = 1;
  /* the pages kept; the others (and the old tree's nodes) left out */
  for (i = 0; i < n; i++) w.pages[i] = order ? (order[i] >= 0 && order[i] < d->npages ? d->pages[order[i]] : NULL) : d->pages[i];
  for (k = 1; k < d->nxref; k++) {
    PObj *o = d->xref[k].obj;
    if (!o) continue;
    if (o->type == PO_DICT && (pobj_is_name(pobj_dict_get(o, "Type"), "Pages") || pobj_is_name(pobj_dict_get(o, "Type"), "Page"))) w.map[k] = -1;
    if (k == d->encrypt_num) w.map[k] = -1;
  }
  /* the kept pages get their numbers first; then the tree's node */
  for (i = 0; i < n; i++) {
    int j;
    if (!w.pages[i]) continue;
    for (j = 1; j < d->nxref; j++) if (d->xref[j].obj == w.pages[i]) break;
    if (j < d->nxref) {
      w.map[j] = w.next++;
      w.page_new[i] = w.map[j];
    } else w.page_new[i] = w.next++;
  }
  w.pages_num = w.next++;
  root_new = w.next++;
  if (info_ref && info_ref->type == PO_REF) info_new = wmap(&w, info_ref->u.ref.num);
  /* the pages: each a copy with its inherited attributes and the new tree as its parent */
  for (i = 0; i < n; i++) {
    PObj *pg = w.pages[i], *c;
    static const char *const inh[] = { "Resources", "MediaBox", "CropBox", "Rotate" };
    int a;
    if (!pg) continue;
    if (!(c = pobj_copy(pg))) goto done;
    for (a = 0; a < 4; a++) if (!pobj_dict_get(c, inh[a])) { PObj *v = pdf_page_attr(d, pg, inh[a]); if (v) pobj_dict_set(c, inh[a], pobj_copy(v)); }
    pobj_dict_del(c, "Parent");
    begin_obj(&w, w.page_new[i]);
    put_obj(&w, c, 0);
    w.out.len -= 2;                                    /* (back over ">>" to add the parent) */
    buf_printf(&w.out, "/Parent %d 0 R\n>>\nendobj\n", w.pages_num);
    pobj_free(c);
  }
  /* the page tree */
  begin_obj(&w, w.pages_num);
  buf_adds(&w.out, "<</Type /Pages /Kids [");
  for (i = 0, k = 0; i < n; i++) if (w.pages[i]) { buf_printf(&w.out, "%s%d 0 R", k ? " " : "", w.page_new[i]); k++; }
  buf_printf(&w.out, "] /Count %d>>\nendobj\n", k);
  /* the catalog: the old one with the new tree */
  if (!(cat = pobj_copy(root))) goto done;
  pobj_dict_del(cat, "Pages");
  begin_obj(&w, root_new);
  put_obj(&w, cat, 0);
  w.out.len -= 2;
  buf_printf(&w.out, "/Pages %d 0 R\n>>\nendobj\n", w.pages_num);
  pobj_free(cat);
  /* everything they refer to */
  for (i = 0; i < w.nq; i++) {
    int old = w.queue[i];
    write_value(&w, w.map[old], pdf_get(d, old));
  }
  /* the cross-reference table and trailer */
  {
    long xref = (long)w.out.len;
    int total = w.next;
    buf_printf(&w.out, "xref\n0 %d\n0000000000 65535 f \n", total);
    for (i = 1; i < total; i++) {
      long off = i < w.noffs ? w.offs[i] : 0;
      if (off) buf_printf(&w.out, "%010ld 00000 n \n", off); else buf_adds(&w.out, "0000000000 00000 f \n");
    }
    buf_printf(&w.out, "trailer\n<</Size %d /Root %d 0 R", total, root_new);
    if (info_new) buf_printf(&w.out, " /Info %d 0 R", info_new);
    {
      PObj *id = pdf_dget(d, d->trailer, "ID");
      if (id && id->type == PO_ARRAY) { buf_adds(&w.out, " /ID "); put_obj(&w, id, 0); }
    }
    buf_printf(&w.out, ">>\nstartxref\n%ld\n%%%%EOF\n", xref);
  }
  if (!w.out.oom) { *outlen = w.out.len; res = (u8 *)buf_take(&w.out); }
done:
  buf_free(&w.out);
  free(w.map); free(w.queue); free(w.offs); free(w.pages); free(w.page_new);
  return res;
}
