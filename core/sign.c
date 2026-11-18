/*
 * sign.c - signing a document (see sign.h). The signature dictionary is written with a /ByteRange
 * of four wide placeholders and /Contents of zeros; once the update is laid out the byte ranges
 * (everything but the /Contents string) are known, written over the placeholders, hashed, and the
 * CMS signature put in the zeros' place as hex.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "sign.h"
#include "pdfwrite.h"
#include "pagecontent.h"
#include "forms.h"

static PObj *ref_to(int num) { PObj *r = pobj_new(PO_REF); if (r) { r->u.ref.num = num; r->u.ref.gen = 0; } return r; }
static void now_str(char *t)
{
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char b[80];
  if (tm) snprintf(b, sizeof b, "%04d%02d%02d%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  else strcpy(b, "20000101000000");
  memcpy(t, b, 14); t[14] = 0;
}
/* the appearance: who signed, when, why */
static int appearance(PDoc *d, const double *r, const char *who, const char *when, const char *reason, const char *label)
{
  double bb[4], w = r[2] - r[0], h = r[3] - r[1], fs;
  Buf b;
  PObj *res = pobj_new(PO_DICT), *fonts = pobj_new(PO_DICT);
  char lat[200], line[300];
  int num;
  if (!res || !fonts) { pobj_free(res); pobj_free(fonts); return 0; }
  bb[0] = bb[1] = 0; bb[2] = w > 0 ? w : 0; bb[3] = h > 0 ? h : 0;
  buf_init(&b);
  if (w > 0 && h > 0) {
    pobj_dict_set(fonts, "F1", om_std_font("Helvetica"));
    pobj_dict_set(fonts, "F2", om_std_font("Helvetica-Bold"));
    pobj_dict_set(res, "Font", fonts); fonts = NULL;
    fs = h / 4.6; if (fs > 11) fs = 11; if (fs < 3) fs = 3;
    buf_adds(&b, "q 0.1 0.15 0.5 RG 0.6 w 0.3 0.3 "); om_num(&b, w - 0.6); om_num(&b, h - 0.6); buf_adds(&b, "re S Q\n");
    buf_adds(&b, "BT 0.05 0.1 0.35 rg /F1 "); om_num(&b, fs * 0.85); buf_adds(&b, "Tf 4 "); om_num(&b, h - fs * 1.2); buf_adds(&b, "Td ");
    om_str(&b, label ? label : "Digitally signed by", (int)strlen(label ? label : "Digitally signed by")); buf_adds(&b, " Tj ET\n");
    utf8_to_latin1(who, strlen(who), lat, sizeof lat);
    buf_adds(&b, "BT 0 0 0 rg /F2 "); om_num(&b, fs * 1.3); buf_adds(&b, "Tf 4 "); om_num(&b, h - fs * 2.6); buf_adds(&b, "Td ");
    om_str(&b, lat, (int)strlen(lat)); buf_adds(&b, " Tj ET\n");
    snprintf(line, sizeof line, "%.4s.%.2s.%.2s %.2s:%.2s:%.2s", when, when + 4, when + 6, when + 8, when + 10, when + 12);
    buf_adds(&b, "BT 0 0 0 rg /F1 "); om_num(&b, fs * 0.85); buf_adds(&b, "Tf 4 "); om_num(&b, h - fs * 3.7); buf_adds(&b, "Td ");
    om_str(&b, line, (int)strlen(line)); buf_adds(&b, " Tj ET\n");
    if (reason && reason[0]) {
      utf8_to_latin1(reason, strlen(reason), lat, sizeof lat);
      buf_adds(&b, "BT 0 0 0 rg /F1 "); om_num(&b, fs * 0.85); buf_adds(&b, "Tf 4 "); om_num(&b, h - fs * 4.6); buf_adds(&b, "Td ");
      om_str(&b, lat, (int)strlen(lat)); buf_adds(&b, " Tj ET\n");
    }
  }
  pobj_free(fonts);
  num = b.oom ? 0 : om_form(d, b.data ? b.data : "", b.len, bb, res);
  buf_free(&b);
  return num;
}
static PObj *acroform_of(PDoc *d)
{
  PObj *root = pdf_dget(d, d->trailer, "Root"), *af;
  if (!root || root->type != PO_DICT) return NULL;
  af = pdf_dget(d, root, "AcroForm");
  if (af && af->type == PO_DICT) return af;
  if (!(af = pobj_new(PO_DICT))) return NULL;
  pobj_dict_set(root, "AcroForm", af);
  return af;
}
/* the field the signature goes into (new or found): its dictionary (field and widget in one,
   or the field with its first widget in *widget) */
static PObj *sig_field(PDoc *d, const SignOpts *o, const char *when, const char *who, PObj **widget, char *err, int ecap)
{
  PObj *af = acroform_of(d), *fields, *f, *pg, *annots, *ap;
  int num, apn, i;
  double r[4] = { 0, 0, 0, 0 };
  if (!af) { snprintf(err, (size_t)ecap, "the document has no catalog"); return NULL; }
  pobj_dict_set(af, "SigFlags", pobj_int(3));
  if (o->field && o->field[0]) {
    FForm ff;
    PObj *res = NULL;
    memset(&ff, 0, sizeof ff);
    form_load(d, &ff);
    for (i = 0; i < ff.n; i++) if (ff.f[i].type == FF_SIG && ff.f[i].name && !strcmp(ff.f[i].name, o->field) && !pdf_dget(d, ff.f[i].dict, "V")) {
      res = ff.f[i].dict;
      *widget = ff.f[i].nw ? ff.f[i].w[0].dict : res;
      if (ff.f[i].nw) memcpy(r, ff.f[i].w[0].rect, sizeof r);
      break;
    }
    form_free(&ff);
    if (!res) { snprintf(err, (size_t)ecap, "no empty signature field named %s", o->field); return NULL; }
    if (r[2] > r[0] && r[3] > r[1] && (apn = appearance(d, r, who, when, o->reason, o->label))) {
      if ((ap = pobj_new(PO_DICT))) { pobj_dict_set(ap, "N", ref_to(apn)); pobj_dict_set(*widget, "AP", ap); }
    }
    return res;
  }
  if (!(pg = pc_page(d, o->page))) { snprintf(err, (size_t)ecap, "no such page"); return NULL; }
  if (o->rect[2] > o->rect[0] && o->rect[3] > o->rect[1]) memcpy(r, o->rect, sizeof r);
  if (!(f = pobj_new(PO_DICT))) return NULL;
  pobj_dict_set(f, "Type", pobj_name("Annot"));
  pobj_dict_set(f, "Subtype", pobj_name("Widget"));
  pobj_dict_set(f, "FT", pobj_name("Sig"));
  {
    FForm ff;
    char name[40];
    int k = 1, clash;
    memset(&ff, 0, sizeof ff);
    form_load(d, &ff);
    do {
      snprintf(name, sizeof name, "Signature%d", k++);
      for (clash = 0, i = 0; i < ff.n; i++) if (ff.f[i].name && !strcmp(ff.f[i].name, name)) clash = 1;
    } while (clash && k < 1000);
    form_free(&ff);
    pobj_dict_set(f, "T", pobj_string(name, (int)strlen(name)));
  }
  pobj_dict_set(f, "Rect", om_arr(r, 4));
  pobj_dict_set(f, "F", pobj_int(132));                /* print, locked */
  pobj_dict_set(f, "P", ref_to(om_num_of(d, pg)));
  if ((apn = appearance(d, r, who, when, o->reason, o->label)) && (ap = pobj_new(PO_DICT))) { pobj_dict_set(ap, "N", ref_to(apn)); pobj_dict_set(f, "AP", ap); }
  num = pdf_new_object(d, f);
  annots = pdf_dget(d, pg, "Annots");
  if (!annots || annots->type != PO_ARRAY) { if (!(annots = pobj_new(PO_ARRAY))) return NULL; pobj_dict_set(pg, "Annots", annots); }
  pobj_array_add(annots, ref_to(num));
  fields = pdf_dget(d, af, "Fields");
  if (!fields || fields->type != PO_ARRAY) { if (!(fields = pobj_new(PO_ARRAY))) return NULL; pobj_dict_set(af, "Fields", fields); }
  pobj_array_add(fields, ref_to(num));
  *widget = f;
  return f;
}
static size_t find(const u8 *p, size_t n, size_t from, const char *s)
{
  size_t l = strlen(s), i;
  for (i = from; i + l <= n; i++) if (p[i] == (u8)s[0] && !memcmp(p + i, s, l)) return i;
  return (size_t)-1;
}
#define PLACE "[0 1000000000 1000000000 1000000000]"
u8 *pdf_sign(PDoc *d, const SignerId *id, const SignOpts *o, size_t *len, char *err, int ecap)
{
  PDoc *b = d;
  PObj *field, *widget = NULL, *v, *zeros;
  char when[16], dt[24], errb[160];
  Cert sc;
  u8 *out = NULL, *cms = NULL, *z = NULL;
  size_t olen = 0, cmslen = 0, at, a, e, i;
  long upd = 0;
  int N, hash = o->hash ? o->hash : HASH_SHA256, vnum, c;
  err[0] = 0;
  if (!id->ncerts || !cert_parse(id->certs[0], id->clen[0], &sc)) { snprintf(err, (size_t)ecap, "no certificate"); return NULL; }
  if (o->when && strlen(o->when) >= 14) { memcpy(when, o->when, 14); when[14] = 0; } else now_str(when);
  /* a document that can't be updated incrementally (new, encrypted or repaired) is written out first */
  if (!pdf_can_save_incremental(d)) {
    size_t l;
    u8 *data = pdf_save(d, NULL, 0, &l);
    if (!data) { snprintf(err, (size_t)ecap, "out of memory"); return NULL; }
    b = pdf_open(data, l, errb, sizeof errb);
    free(data);
    if (!b) { snprintf(err, (size_t)ecap, "%s", errb); return NULL; }
    pdf_ensure_pages(b);
  }
  if (!(field = sig_field(b, o, when, sc.subject_cn, &widget, err, ecap))) goto done;
  /* the signature dictionary, with room for the signature */
  for (N = 3000, c = 0; c < id->ncerts; c++) N += (int)id->clen[c];
  if (!(v = pobj_new(PO_DICT)) || !(z = (u8 *)calloc((size_t)N, 1)) || !(zeros = pobj_string((const char *)z, N))) { pobj_free(v); snprintf(err, (size_t)ecap, "out of memory"); goto done; }
  pobj_dict_set(v, "Type", pobj_name("Sig"));
  pobj_dict_set(v, "Filter", pobj_name("Adobe.PPKLite"));
  pobj_dict_set(v, "SubFilter", pobj_name("adbe.pkcs7.detached"));
  {
    PObj *br = pobj_new(PO_ARRAY);
    pobj_array_add(br, pobj_int(0)); pobj_array_add(br, pobj_int(1000000000L)); pobj_array_add(br, pobj_int(1000000000L)); pobj_array_add(br, pobj_int(1000000000L));
    pobj_dict_set(v, "ByteRange", br);
  }
  pobj_dict_set(v, "Contents", zeros);
  pobj_dict_set(v, "Name", om_text(sc.subject_cn));
  snprintf(dt, sizeof dt, "D:%s", when);
  pobj_dict_set(v, "M", pobj_string(dt, (int)strlen(dt)));
  if (o->reason && o->reason[0]) pobj_dict_set(v, "Reason", om_text(o->reason));
  if (o->location && o->location[0]) pobj_dict_set(v, "Location", om_text(o->location));
  if (o->certify >= 1 && o->certify <= 3) {
    PObj *refs = pobj_new(PO_ARRAY), *sr = pobj_new(PO_DICT), *tp = pobj_new(PO_DICT);
    pobj_dict_set(tp, "Type", pobj_name("TransformParams"));
    pobj_dict_set(tp, "P", pobj_int(o->certify));
    pobj_dict_set(tp, "V", pobj_name("1.2"));
    pobj_dict_set(sr, "Type", pobj_name("SigRef"));
    pobj_dict_set(sr, "TransformMethod", pobj_name("DocMDP"));
    pobj_dict_set(sr, "TransformParams", tp);
    pobj_array_add(refs, sr);
    pobj_dict_set(v, "Reference", refs);
  }
  vnum = pdf_new_object(b, v);
  pobj_dict_set(field, "V", ref_to(vnum));
  if (o->certify >= 1 && o->certify <= 3) {
    PObj *root = pdf_dget(b, b->trailer, "Root"), *perms = pobj_new(PO_DICT);
    pobj_dict_set(perms, "DocMDP", ref_to(vnum));
    pobj_dict_set(root, "Perms", perms);
  }
  b->changed = 1;
  if (!(out = pdf_save_incremental(b, &olen, &upd))) { snprintf(err, (size_t)ecap, "the document couldn't be written"); goto done; }
  /* the placeholders */
  if ((at = find(out, olen, (size_t)upd, "/ByteRange " PLACE)) == (size_t)-1) { snprintf(err, (size_t)ecap, "internal: no byte range"); goto fail; }
  for (a = (size_t)upd; ; a++) {
    a = find(out, olen, a, "/Contents <");
    if (a == (size_t)-1) break;
    a += 10;
    for (i = 1; a + i < olen && out[a + i] == '0'; i++) ;
    if (i == (size_t)(2 * N + 1) && a + i < olen && out[a + i] == '>') break;
  }
  if (a == (size_t)-1) { snprintf(err, (size_t)ecap, "internal: no room for the signature"); goto fail; }
  e = a + 2 * (size_t)N + 2;
  {
    char br[64];
    int l = snprintf(br, sizeof br, "[0 %lu %lu %lu", (unsigned long)a, (unsigned long)e, (unsigned long)(olen - e)), pl = (int)strlen(PLACE);
    while (l < pl - 1) br[l++] = ' ';
    br[l++] = ']';
    memcpy(out + at + 11, br, (size_t)pl);
  }
  {
    u8 *data = (u8 *)malloc(olen - (e - a));
    if (!data) { snprintf(err, (size_t)ecap, "out of memory"); goto fail; }
    memcpy(data, out, a); memcpy(data + a, out + e, olen - e);
    cms = cms_sign(&id->key, (const u8 *const *)id->certs, id->clen, id->ncerts, hash, data, olen - (e - a), when, &cmslen);
    free(data);
  }
  if (!cms) { snprintf(err, (size_t)ecap, "the signature couldn't be made (a key Folio can't use?)"); goto fail; }
  if (cmslen > (size_t)N) { snprintf(err, (size_t)ecap, "internal: the signature is too big"); goto fail; }
  for (i = 0; i < cmslen; i++) { static const char hx[] = "0123456789ABCDEF"; out[a + 1 + 2 * i] = (u8)hx[cms[i] >> 4]; out[a + 2 + 2 * i] = (u8)hx[cms[i] & 15]; }
  free(cms); free(z);
  if (b != d) pdf_close(b);
  *len = olen;
  return out;
fail:
  free(out); out = NULL;
done:
  free(cms); free(z);
  if (b != d) pdf_close(b);
  return NULL;
}
