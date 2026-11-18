/*
 * export.c - text, deck and picture export (see export.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "export.h"
#include "zip.h"
#include "pngout.h"
#include "contentedit.h"

/* ------------------------------------------------------------------ paragraphs */
typedef struct { int level; int page; char *text; } Para;
typedef struct { Para *p; int n, cap; } Paras;
static void para_add(Paras *ps, int level, int page, const char *s, size_t n)
{
  Para *np;
  if (ps->n >= ps->cap) { int nc = ps->cap ? ps->cap * 2 : 64; if (!(np = (Para *)realloc(ps->p, (size_t)nc * sizeof(Para)))) return; ps->p = np; ps->cap = nc; }
  ps->p[ps->n].level = level; ps->p[ps->n].page = page;
  ps->p[ps->n].text = str_ndup(s, n);
  ps->n++;
}
static float rsize(float s) { return (float)floor(s * 2 + 0.5) / 2; }
static void build(PDoc *d, ExportText get, void *ud, Paras *ps)
{
  struct { float size; long chars; } sc[64];
  int nsc = 0, p, i, k;
  float body = 0, lev[3] = { 0, 0, 0 };
  long best = 0;
  memset(ps, 0, sizeof *ps);
  for (p = 0; p < d->npages; p++) {
    TextPage *t = get(ud, p);
    if (!t) continue;
    for (i = 0; i < t->no; i++) {
      float s;
      if (t->o[i].u <= 32) continue;
      s = rsize(t->o[i].size);
      for (k = 0; k < nsc && sc[k].size != s; k++) ;
      if (k == nsc) { if (nsc == 64) continue; sc[nsc].size = s; sc[nsc].chars = 0; nsc++; }
      sc[k].chars++;
    }
  }
  for (k = 0; k < nsc; k++) if (sc[k].chars > best) { best = sc[k].chars; body = sc[k].size; }
  for (k = 0; k < nsc; k++) {
    float s = sc[k].size;
    if (s < body * 1.15f || sc[k].chars < 2) continue;
    if (s > lev[0]) { lev[2] = lev[1]; lev[1] = lev[0]; lev[0] = s; }
    else if (s > lev[1] && s != lev[0]) { lev[2] = lev[1]; lev[1] = s; }
    else if (s > lev[2] && s != lev[1] && s != lev[0]) lev[2] = s;
  }
  for (p = 0; p < d->npages; p++) {
    TextPage *t = get(ud, p);
    Buf cur;
    int a = 0, curlev = -1;
    float last_y0 = 0, last_size = 0;
    if (!t) continue;
    buf_init(&cur);
    for (i = 0; i <= t->no; i++) {
      char *line;
      int level = 0, j, first = -1;
      size_t l;
      if (i < t->no && t->o[i].u != 10) continue;
      if (i <= a) { a = i + 1; continue; }
      for (j = a; j < i; j++) if (t->o[j].u > 32) { first = j; break; }
      if (first < 0) { a = i + 1; continue; }
      {
        float s = rsize(t->o[first].size);
        for (k = 0; k < 3; k++) if (lev[k] > 0 && s == lev[k]) level = k + 1;
      }
      line = textpage_text(t, a, i - 1);
      l = line ? strlen(line) : 0;
      while (l && (line[l - 1] == ' ' || line[l - 1] == '\n')) line[--l] = 0;
      if (line && l) {
        /* a new paragraph: another level, a heading, or a gap above more than a line's */
        int join = cur.len && level == 0 && curlev == 0 && last_y0 - t->o[first].y1 < last_size * 0.9f;
        if (!join && cur.len) { para_add(ps, curlev, p, cur.data, cur.len); cur.len = 0; }
        if (join) {
          if (cur.data[cur.len - 1] == '-' && line[0] >= 'a' && line[0] <= 'z') cur.len--;
          else buf_addc(&cur, ' ');
        }
        buf_add(&cur, line, l);
        curlev = level;
        last_y0 = t->o[first].y0; last_size = t->o[first].size;
        if (level) { para_add(ps, level, p, cur.data, cur.len); cur.len = 0; }
      }
      free(line);
      a = i + 1;
    }
    if (cur.len) para_add(ps, curlev < 0 ? 0 : curlev, p, cur.data, cur.len);
    buf_free(&cur);
  }
}
static void paras_free(Paras *ps) { int i; for (i = 0; i < ps->n; i++) free(ps->p[i].text); free(ps->p); }
static void xml_esc(Buf *b, const char *s)
{
  for (; s && *s; s++) {
    if (*s == '<') buf_adds(b, "&lt;"); else if (*s == '>') buf_adds(b, "&gt;"); else if (*s == '&') buf_adds(b, "&amp;");
    else if (*s == '"') buf_adds(b, "&quot;"); else if ((u8)*s >= 32 || *s == '\t') buf_addc(b, *s);
  }
}
static void rtf_esc(Buf *b, const char *s)
{
  size_t n = strlen(s), off = 0;
  while (off < n) {
    int used;
    u32 c = utf8_get(s + off, n - off, &used);
    off += (size_t)(used > 0 ? used : 1);
    if (c == '\\' || c == '{' || c == '}') { buf_addc(b, '\\'); buf_addc(b, (int)c); }
    else if (c < 32) continue;
    else if (c < 128) buf_addc(b, (int)c);
    else if (c < 0x10000) buf_printf(b, "\\u%d?", c < 0x8000 ? (int)c : (int)c - 65536);
    else buf_adds(b, "?");
  }
}
static const char *docx_types =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
  "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
  "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
  "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
  "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
  "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
  "</Types>";
static const char *docx_rels =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
  "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
  "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
  "</Relationships>";
static const char *docx_docrels =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
  "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
  "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
  "</Relationships>";
static const char *docx_styles =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
  "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
  "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:pPr><w:spacing w:after=\"120\"/></w:pPr><w:rPr><w:sz w:val=\"22\"/></w:rPr></w:style>"
  "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:keepNext/><w:spacing w:before=\"240\"/><w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"36\"/></w:rPr></w:style>"
  "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:keepNext/><w:spacing w:before=\"200\"/><w:outlineLvl w:val=\"1\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"30\"/></w:rPr></w:style>"
  "<w:style w:type=\"paragraph\" w:styleId=\"Heading3\"><w:name w:val=\"heading 3\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:keepNext/><w:spacing w:before=\"160\"/><w:outlineLvl w:val=\"2\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"26\"/></w:rPr></w:style>"
  "</w:styles>";
u8 *export_text(PDoc *d, int format, ExportText get, void *ud, size_t *len)
{
  Paras ps;
  Buf b;
  int i, page = 0;
  char *title = pdf_info(d, "Title");
  build(d, get, ud, &ps);
  buf_init(&b);
  switch (format) {
  case EXP_HTML:
    buf_adds(&b, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
    xml_esc(&b, title ? title : "Document");
    buf_adds(&b, "</title>\n<style>body{font-family:sans-serif;max-width:46em;margin:2em auto;line-height:1.4} hr{border:0;border-top:1px dashed #aaa}</style>\n</head>\n<body>\n");
    for (i = 0; i < ps.n; i++) {
      if (ps.p[i].page != page) { buf_adds(&b, "<hr>\n"); page = ps.p[i].page; }
      if (ps.p[i].level) buf_printf(&b, "<h%d>", ps.p[i].level); else buf_adds(&b, "<p>");
      xml_esc(&b, ps.p[i].text);
      if (ps.p[i].level) buf_printf(&b, "</h%d>\n", ps.p[i].level); else buf_adds(&b, "</p>\n");
    }
    buf_adds(&b, "</body>\n</html>\n");
    break;
  case EXP_RTF:
    buf_adds(&b, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl{\\f0\\fswiss Helvetica;}}\n");
    if (title) { buf_adds(&b, "{\\info{\\title "); rtf_esc(&b, title); buf_adds(&b, "}}\n"); }
    for (i = 0; i < ps.n; i++) {
      static const int sz[4] = { 22, 36, 30, 26 };
      if (ps.p[i].page != page) { buf_adds(&b, "\\page\n"); page = ps.p[i].page; }
      buf_printf(&b, "{\\pard\\sa120%s\\fs%d ", ps.p[i].level ? "\\keepn\\b" : "", sz[ps.p[i].level & 3]);
      rtf_esc(&b, ps.p[i].text);
      buf_adds(&b, "\\par}\n");
    }
    buf_adds(&b, "}\n");
    break;
  case EXP_DOCX: {
    ZipWriter z;
    Buf doc;
    buf_init(&doc);
    buf_adds(&doc, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>");
    for (i = 0; i < ps.n; i++) {
      if (ps.p[i].page != page) { buf_adds(&doc, "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"); page = ps.p[i].page; }
      buf_adds(&doc, "<w:p>");
      if (ps.p[i].level) buf_printf(&doc, "<w:pPr><w:pStyle w:val=\"Heading%d\"/></w:pPr>", ps.p[i].level);
      buf_adds(&doc, "<w:r><w:t xml:space=\"preserve\">");
      xml_esc(&doc, ps.p[i].text);
      buf_adds(&doc, "</w:t></w:r></w:p>");
    }
    buf_adds(&doc, "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>");
    zip_begin(&z);
    zip_add(&z, "[Content_Types].xml", docx_types, strlen(docx_types));
    zip_add(&z, "_rels/.rels", docx_rels, strlen(docx_rels));
    zip_add(&z, "word/_rels/document.xml.rels", docx_docrels, strlen(docx_docrels));
    zip_add(&z, "word/styles.xml", docx_styles, strlen(docx_styles));
    zip_add(&z, "word/document.xml", doc.data ? doc.data : "", doc.len);
    buf_free(&doc);
    if (!zip_finish(&z) || doc.oom) { zip_free(&z); buf_free(&b); paras_free(&ps); free(title); return NULL; }
    buf_free(&b);
    b = z.out;
    buf_free(&z.cdir);
    break; }
  default:
    for (i = 0; i < ps.n; i++) {
      if (ps.p[i].page != page) { buf_adds(&b, "\f"); page = ps.p[i].page; }
      buf_adds(&b, ps.p[i].text); buf_adds(&b, "\n\n");
    }
  }
  paras_free(&ps);
  free(title);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ------------------------------------------------------------------ PowerPoint */
static const char *pp_theme =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
  "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Folio\"><a:themeElements>"
  "<a:clrScheme name=\"Folio\"><a:dk1><a:srgbClr val=\"000000\"/></a:dk1><a:lt1><a:srgbClr val=\"FFFFFF\"/></a:lt1><a:dk2><a:srgbClr val=\"1F2937\"/></a:dk2><a:lt2><a:srgbClr val=\"F3F4F6\"/></a:lt2>"
  "<a:accent1><a:srgbClr val=\"2563EB\"/></a:accent1><a:accent2><a:srgbClr val=\"DC2626\"/></a:accent2><a:accent3><a:srgbClr val=\"16A34A\"/></a:accent3><a:accent4><a:srgbClr val=\"CA8A04\"/></a:accent4>"
  "<a:accent5><a:srgbClr val=\"9333EA\"/></a:accent5><a:accent6><a:srgbClr val=\"0891B2\"/></a:accent6><a:hlink><a:srgbClr val=\"2563EB\"/></a:hlink><a:folHlink><a:srgbClr val=\"7C3AED\"/></a:folHlink></a:clrScheme>"
  "<a:fontScheme name=\"Folio\"><a:majorFont><a:latin typeface=\"Arial\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont><a:latin typeface=\"Arial\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme>"
  "<a:fmtScheme name=\"Folio\"><a:fillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:fillStyleLst>"
  "<a:lnStyleLst><a:ln w=\"9525\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln><a:ln w=\"12700\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln><a:ln w=\"19050\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln></a:lnStyleLst>"
  "<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>"
  "<a:bgFillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:bgFillStyleLst></a:fmtScheme>"
  "</a:themeElements></a:theme>";
static const char *pp_master =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
  "<p:sldMaster xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
  "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/></p:spTree></p:cSld>"
  "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
  "<p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst></p:sldMaster>";
static const char *pp_layout =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
  "<p:sldLayout xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" type=\"blank\">"
  "<p:cSld name=\"Blank\"><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>";
#define RELS_HEAD "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
#define RT "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
u8 *export_pptx(PDoc *d, RCache *rc, int dpi, size_t *len)
{
  ZipWriter z;
  Buf t, pres, prels;
  int p, ok = 1;
  double box[4];
  int rot;
  long cx, cy;
  if (dpi < 36) dpi = 36;
  pdf_page_box(d, 0, box, &rot);
  cx = (long)((rot % 180 ? box[3] - box[1] : box[2] - box[0]) * 12700); cy = (long)((rot % 180 ? box[2] - box[0] : box[3] - box[1]) * 12700);
  zip_begin(&z);
  buf_init(&t); buf_init(&pres); buf_init(&prels);
  buf_adds(&t, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
               "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/>"
               "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
               "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
               "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
               "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>");
  buf_adds(&pres, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
                  "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst><p:sldIdLst>");
  buf_adds(&prels, RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/><Relationship Id=\"rId2\" Type=\"" RT "theme\" Target=\"theme/theme1.xml\"/>");
  for (p = 0; p < d->npages && ok; p++) {
    RenderOpts o;
    Pixmap pm;
    char name[64], xml[2048];
    u8 *png;
    size_t pl;
    memset(&o, 0, sizeof o); memset(&pm, 0, sizeof pm);
    o.scale = dpi / 72.0; o.annots = 1; o.paper = 0xFFFFFF;
    if (!pdf_render(d, rc, p, &o, &pm)) { free(pm.rgb); ok = 0; break; }
    png = png_write_rgb(pm.rgb, pm.w, pm.h, dpi, &pl);
    free(pm.rgb);
    if (!png) { ok = 0; break; }
    snprintf(name, sizeof name, "ppt/media/page%d.png", p + 1);
    ok &= zip_add_stored(&z, name, png, pl);
    free(png);
    snprintf(xml, sizeof xml, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
             "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
             "<p:pic><p:nvPicPr><p:cNvPr id=\"2\" name=\"Page %d\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr><p:blipFill><a:blip r:embed=\"rId2\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
             "<p:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>"
             "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>", p + 1, cx, cy);
    snprintf(name, sizeof name, "ppt/slides/slide%d.xml", p + 1);
    ok &= zip_add(&z, name, xml, strlen(xml));
    snprintf(xml, sizeof xml, RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/><Relationship Id=\"rId2\" Type=\"" RT "image\" Target=\"../media/page%d.png\"/></Relationships>", p + 1);
    snprintf(name, sizeof name, "ppt/slides/_rels/slide%d.xml.rels", p + 1);
    ok &= zip_add(&z, name, xml, strlen(xml));
    buf_printf(&t, "<Override PartName=\"/ppt/slides/slide%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>", p + 1);
    buf_printf(&pres, "<p:sldId id=\"%d\" r:id=\"rId%d\"/>", 256 + p, p + 3);
    buf_printf(&prels, "<Relationship Id=\"rId%d\" Type=\"" RT "slide\" Target=\"slides/slide%d.xml\"/>", p + 3, p + 1);
  }
  buf_adds(&t, "</Types>");
  buf_printf(&pres, "</p:sldIdLst><p:sldSz cx=\"%ld\" cy=\"%ld\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>", cx, cy);
  buf_adds(&prels, "</Relationships>");
  ok &= zip_add(&z, "[Content_Types].xml", t.data, t.len);
  ok &= zip_add(&z, "_rels/.rels", RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "officeDocument\" Target=\"ppt/presentation.xml\"/></Relationships>",
                strlen(RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "officeDocument\" Target=\"ppt/presentation.xml\"/></Relationships>"));
  ok &= zip_add(&z, "ppt/presentation.xml", pres.data, pres.len);
  ok &= zip_add(&z, "ppt/_rels/presentation.xml.rels", prels.data, prels.len);
  ok &= zip_add(&z, "ppt/slideMasters/slideMaster1.xml", pp_master, strlen(pp_master));
  ok &= zip_add(&z, "ppt/slideMasters/_rels/slideMaster1.xml.rels", RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/><Relationship Id=\"rId2\" Type=\"" RT "theme\" Target=\"../theme/theme1.xml\"/></Relationships>",
                strlen(RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/><Relationship Id=\"rId2\" Type=\"" RT "theme\" Target=\"../theme/theme1.xml\"/></Relationships>"));
  ok &= zip_add(&z, "ppt/slideLayouts/slideLayout1.xml", pp_layout, strlen(pp_layout));
  ok &= zip_add(&z, "ppt/slideLayouts/_rels/slideLayout1.xml.rels", RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/></Relationships>",
                strlen(RELS_HEAD "<Relationship Id=\"rId1\" Type=\"" RT "slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/></Relationships>"));
  ok &= zip_add(&z, "ppt/theme/theme1.xml", pp_theme, strlen(pp_theme));
  buf_free(&t); buf_free(&pres); buf_free(&prels);
  if (!ok || !zip_finish(&z)) { zip_free(&z); return NULL; }
  *len = z.out.len;
  buf_free(&z.cdir);
  return (u8 *)buf_take(&z.out);
}

/* ------------------------------------------------------------------ pictures */
static void collect(PDoc *d, PObj *res, int *nums, int *n, int cap, int depth)
{
  PObj *xo = res ? pdf_dget(d, res, "XObject") : NULL;
  int i, k;
  if (!xo || xo->type != PO_DICT || depth > 8) return;
  for (i = 0; i < xo->u.dict.n; i++) {
    PObj *r = xo->u.dict.v[i], *x = pdf_resolve(d, r);
    const char *st;
    if (!x || x->type != PO_STREAM || r->type != PO_REF) continue;
    st = pdf_dname(d, x, "Subtype");
    if (st && !strcmp(st, "Image")) {
      for (k = 0; k < *n && nums[k] != r->u.ref.num; k++) ;
      if (k == *n && *n < cap) nums[(*n)++] = r->u.ref.num;
    } else if (st && !strcmp(st, "Form")) collect(d, pdf_dget(d, x, "Resources"), nums, n, cap, depth + 1);
  }
}
int export_images(PDoc *d, int (*each)(void *ud, const char *name, const u8 *data, size_t n), void *ud)
{
  int nums[4096], n = 0, p, i, made = 0;
  for (p = 0; p < d->npages; p++) collect(d, pdf_resolve(d, pdf_page_attr(d, d->pages[p], "Resources")), nums, &n, 4096, 0);
  for (i = 0; i < n; i++) {
    PObj *img = pdf_get(d, nums[i]);
    const char *imf = NULL;
    PObj *parms = NULL;
    size_t len;
    u8 *raw = pdf_stream_data_raw(d, img, &len, &imf, &parms);
    char name[48];
    if (raw && imf && (!strcmp(imf, "DCTDecode") || !strcmp(imf, "DCT"))) {
      snprintf(name, sizeof name, "image-%d.jpg", nums[i]);
      if (each(ud, name, raw, len)) made++;
      free(raw);
      continue;
    }
    free(raw);
    {
      int w, h, comps, bpc, rgb, x;
      u8 *s = ce_image_samples(d, img, &w, &h, &comps, &bpc, &rgb), *out, *png;
      size_t pl, row;
      PObj *cs = pdf_dget(d, img, "ColorSpace");
      const u8 *lut = NULL;
      int hival = 0, bcomps = 3;
      if (!s) continue;
      if (cs && cs->type == PO_ARRAY && cs->u.arr.n >= 4 && pobj_is_name(pdf_resolve(d, cs->u.arr.v[0]), "Indexed")) {
        PObj *l = pdf_resolve(d, cs->u.arr.v[3]), *base = pdf_resolve(d, cs->u.arr.v[1]);
        hival = dbl_int(pobj_num(pdf_resolve(d, cs->u.arr.v[2])));
        if (pobj_is_name(base, "DeviceGray")) bcomps = 1;
        if (l && l->type == PO_STRING && l->u.str.len >= (hival + 1) * bcomps) lut = (const u8 *)l->u.str.s;
        if (!lut) { free(s); continue; }
      }
      if (!(out = (u8 *)malloc((size_t)w * h * 3))) { free(s); continue; }
      row = ((size_t)w * comps * bpc + 7) / 8;
      for (x = 0; x < w * h; x++) {
        int px = x % w, py = x / w, k;
        u8 v[4] = { 0, 0, 0, 0 }, *o = out + (size_t)x * 3;
        for (k = 0; k < comps && k < 4; k++) {
          long bit = ((long)px * comps + k) * bpc;
          const u8 *q = s + row * py;
          unsigned val = bpc == 8 ? q[bit / 8] : bpc == 16 ? q[bit / 8] : (unsigned)((q[bit / 8] >> (8 - bpc - bit % 8)) & ((1 << bpc) - 1));
          v[k] = (u8)(bpc >= 8 || lut ? val : val * 255 / ((1 << bpc) - 1));
        }
        if (lut) { int ix = v[0] > hival ? hival : v[0]; if (bcomps == 1) o[0] = o[1] = o[2] = lut[ix]; else memcpy(o, lut + ix * 3, 3); }
        else if (comps == 1) o[0] = o[1] = o[2] = v[0];
        else if (comps == 4) { o[0] = (u8)(255 - (v[0] + v[3] > 255 ? 255 : v[0] + v[3])); o[1] = (u8)(255 - (v[1] + v[3] > 255 ? 255 : v[1] + v[3])); o[2] = (u8)(255 - (v[2] + v[3] > 255 ? 255 : v[2] + v[3])); }
        else memcpy(o, v, 3);
      }
      free(s);
      png = png_write_rgb(out, w, h, 0, &pl);
      free(out);
      if (!png) continue;
      snprintf(name, sizeof name, "image-%d.png", nums[i]);
      if (each(ud, name, png, pl)) made++;
      free(png);
    }
  }
  return made;
}
