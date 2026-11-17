/*
 * fonttest.c - tests for pfont (font programs) and pdfenc (encodings, glyph names).
 * Outlines, advances, cmaps, names, encodings and CIDs are checked against what fontTools makes of the same fonts
 * (tests/fonts/expected.txt, written by tests/fonts/mkfonts.py); then every font is truncated and corrupted a few
 * hundred times and loaded and drawn in full, for the sanitizers to watch.
 * Build (from the project root):
 *   cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/geom.c core/pfont.c \
 *      core/pdfenc.c tests/fonttest.c -lm
 * Run from the project root, or give the fonts directory as the argument (then the number of variants per font, and
 * a random seed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "core/pfont.h"
#include "core/pdfenc.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
    if (fails < 60) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static const char *dir = "tests/fonts";

/* ------------------------------------------------------------------ */
/* outlines as contours of points (start, line ends, curve points), the closing point dropped as fontTools does */
typedef struct { int n; double *xy; } Contour;
typedef struct { int n; Contour c[512]; } Shape;
static void shape_free(Shape *s)
{
  int i;
  for (i = 0; i < s->n; i++) free(s->c[i].xy);
  s->n = 0;
}
/* the path's contours: lone moves are left out, as are closing points equal to the start */
static void shape_of(const Path *p, Shape *s)
{
  int i, k = 0, open = 0;
  Contour cur;
  cur.n = 0; cur.xy = NULL;
  s->n = 0;
  for (i = 0; i <= p->nops; i++) {
    int op = i < p->nops ? p->ops[i] : 'M', np = op == 'M' || op == 'L' ? 1 : op == 'C' ? 3 : 0;
    if ((op == 'M' || op == 'Z' || i == p->nops) && open) {
      if (cur.n > 1 && fabs(cur.xy[0] - cur.xy[2 * cur.n - 2]) < 1e-9 && fabs(cur.xy[1] - cur.xy[2 * cur.n - 1]) < 1e-9) cur.n--;
      if (cur.n > 1 && s->n < 512) s->c[s->n++] = cur; else free(cur.xy);
      cur.n = 0; cur.xy = NULL; open = 0;
    }
    if (i == p->nops) break;
    if (np) {
      if (!open) { cur.xy = (double *)malloc(sizeof(double) * 2 * (size_t)(p->npts + 1)); cur.n = 0; open = 1; }
      memcpy(cur.xy + 2 * cur.n, p->pts + 2 * k, sizeof(double) * 2 * (size_t)np);
      cur.n += np;
    }
    k += np;
  }
}
/* the contours are the same, each maybe starting at another of its points */
static int same_contour(const Contour *a, const Contour *b, double tol)
{
  int s, i;
  if (a->n != b->n) return 0;
  for (s = 0; s < a->n; s++) {
    for (i = 0; i < a->n; i++) {
      int j = (i + s) % a->n;
      if (fabs(a->xy[2 * i] - b->xy[2 * j]) > tol || fabs(a->xy[2 * i + 1] - b->xy[2 * j + 1]) > tol) break;
    }
    if (i == a->n) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* the expected data */
static char *next_line(char **pp)
{
  char *s = *pp, *e;
  if (!s || !*s) return NULL;
  e = strchr(s, '\n');
  if (e) { *e = 0; *pp = e + 1; } else *pp = s + strlen(s);
  return s;
}
static u8 *load_file(const char *name, size_t *len)
{
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  return file_read(path, len);
}
/* checks one glyph's lines against the font */
static void check_glyph(PFont *f, const char *fname, char *line, char **pp)
{
  int gid, nc, i;
  char name[128];
  double adv, got;
  Path p;
  Shape s, e;
  if (sscanf(line, "glyph %d %127s %lf %d", &gid, name, &adv, &nc) != 4) { CHECK(0, "bad line %s", line); return; }
  path_init(&p);
  CHECK(pf_glyph_outline(f, gid, &p), "%s: glyph %d (%s) didn't draw", fname, gid, name);
  shape_of(&p, &s);
  e.n = 0;
  for (i = 0; i < nc; i++) {
    char *l = next_line(pp), *q;
    int n, k;
    if (!l || strncmp(l, "c ", 2)) { CHECK(0, "%s: expected data short at glyph %d", fname, gid); break; }
    n = (int)strtol(l + 2, &q, 10);
    e.c[e.n].n = n; e.c[e.n].xy = (double *)malloc(sizeof(double) * 2 * (size_t)n);
    for (k = 0; k < 2 * n; k++) e.c[e.n].xy[k] = strtod(q, &q);
    if (e.n < 511) e.n++;
  }
  CHECK(s.n == e.n, "%s: glyph %d (%s): %d contours, expected %d", fname, gid, name, s.n, e.n);
  if (s.n == e.n)
    for (i = 0; i < s.n; i++)
      CHECK(same_contour(&s.c[i], &e.c[i], 0.02), "%s: glyph %d (%s): contour %d differs (%d points, expected %d)", fname, gid, name, i,
            s.c[i].n, e.c[i].n);
  got = pf_glyph_advance(f, gid);
  CHECK(fabs(got - adv) < 0.01, "%s: glyph %d (%s): advance %g, expected %g", fname, gid, name, got, adv);
  if (strcmp(name, "-")) {
    const char *gn = pf_glyph_name(f, gid);
    int bg = pf_glyph_by_name(f, name);
    CHECK(gn && !strcmp(gn, name), "%s: glyph %d named %s, expected %s", fname, gid, gn ? gn : "(none)", name);
    CHECK(bg >= 0 && pf_glyph_name(f, bg) && !strcmp(pf_glyph_name(f, bg), name), "%s: glyph by name %s: %d", fname, name, bg);
  }
  shape_free(&s); shape_free(&e);
  path_free(&p);
}
/* every font in expected.txt against its lines */
static void check_expected(void)
{
  size_t len;
  char *text = (char *)load_file("expected.txt", &len), *pp = text, *line;
  PFont *f = NULL;
  char fname[128] = "";
  int stdenc_seen = 0, glyphs = 0;
  if (!text) { CHECK(0, "no %s/expected.txt", dir); return; }
  while ((line = next_line(&pp)) != NULL) {
    if (!strncmp(line, "font ", 5)) {
      int n;
      size_t fl;
      u8 *data;
      char err[128];
      pf_free(f); f = NULL;
      if (sscanf(line, "font %127s %d", fname, &n) != 2) continue;
      data = load_file(fname, &fl);
      CHECK(data != NULL, "can't read %s", fname);
      if (!data) continue;
      f = pf_load(data, fl, err, sizeof err);
      free(data);
      CHECK(f != NULL, "%s didn't load: %s", fname, err);
      if (!f) continue;
      CHECK(pf_num_glyphs(f) == n, "%s: %d glyphs, expected %d", fname, pf_num_glyphs(f), n);
      printf("%-12s kind %d, %d glyphs\n", fname, pf_kind(f), pf_num_glyphs(f));
      stdenc_seen = 0;
    } else if (!f) continue;
    else if (!strncmp(line, "matrix ", 7)) {
      double m[6], g[6];
      int i;
      sscanf(line + 7, "%lf %lf %lf %lf %lf %lf", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
      pf_font_matrix(f, g);
      for (i = 0; i < 6; i++) CHECK(fabs(m[i] - g[i]) < 1e-9, "%s: FontMatrix[%d] %g, expected %g", fname, i, g[i], m[i]);
    } else if (!strncmp(line, "glyph ", 6)) { check_glyph(f, fname, line, &pp); glyphs++; }
    else if (!strncmp(line, "cmap ", 5)) {
      int pl, en, g;
      unsigned code;
      sscanf(line + 5, "%d %d %u %d", &pl, &en, &code, &g);
      CHECK(pf_cmap(f, pl, en, code) == g, "%s: cmap (%d,%d) %u -> %d, expected %d", fname, pl, en, code, pf_cmap(f, pl, en, code), g);
    } else if (!strncmp(line, "name ", 5)) {
      int g;
      char nm[128];
      const char *gn;
      sscanf(line + 5, "%d %127s", &g, nm);
      gn = pf_glyph_name(f, g);
      CHECK(gn && !strcmp(gn, nm), "%s: name of %d: %s, expected %s", fname, g, gn ? gn : "(none)", nm);
    } else if (!strncmp(line, "enc ", 4)) {
      int code, g;
      const int *e = pf_builtin_encoding(f);
      sscanf(line + 4, "%d %d", &code, &g);
      CHECK(e && e[code] == g, "%s: encoding %d -> %d, expected %d", fname, code, e ? e[code] : -9, g);
    } else if (!strcmp(line, "stdenc")) {
      CHECK(pf_builtin_encoding(f) == NULL, "%s: a built-in encoding where StandardEncoding was expected", fname);
      stdenc_seen = 1;
    } else if (!strcmp(line, "cidfont")) CHECK(pf_is_cid(f), "%s: not CID-keyed", fname);
    else if (!strncmp(line, "cid ", 4)) {
      int c, g;
      sscanf(line + 4, "%d %d", &c, &g);
      CHECK(pf_cid_to_gid(f, c) == g, "%s: CID %d -> %d, expected %d", fname, c, pf_cid_to_gid(f, c), g);
    }
  }
  (void)stdenc_seen;
  pf_free(f);
  free(text);
  printf("%d glyphs compared\n", glyphs);
}

/* ------------------------------------------------------------------ */
/* things the expected data doesn't cover */
static void check_misc(void)
{
  size_t len;
  u8 *d;
  char err[128];
  PFont *f;
  /* garbage */
  CHECK(pf_load((const u8 *)"abc", 3, err, sizeof err) == NULL && err[0], "3 bytes loaded");
  CHECK(pf_load((const u8 *)"\0\1\0\0\0\0\0\0\0\0\0\0", 12, err, sizeof err) == NULL, "an empty sfnt loaded");
  CHECK(pf_load((const u8 *)"%!PS-AdobeFont eexec 0123", 25, err, sizeof err) == NULL, "an empty Type 1 loaded");
  /* the CID fonts: unknown CIDs */
  if ((d = load_file("cid3.cff", &len)) != NULL) {
    f = pf_load(d, len, err, sizeof err);
    CHECK(f && pf_cid_to_gid(f, 11) == -1 && pf_cid_to_gid(f, 9000) == 31 && pf_cid_to_gid(f, -5) == -1 && pf_cid_to_gid(f, 70000) == -1,
          "CID lookups");
    CHECK(f && pf_builtin_encoding(f) == NULL && pf_glyph_name(f, 3) == NULL, "CID font names/encoding");
    pf_free(f); free(d);
  }
  /* bare CFF: the identity "CID" map of a name-keyed font, names */
  if ((d = load_file("enc.cff", &len)) != NULL) {
    f = pf_load(d, len, err, sizeof err);
    CHECK(f && !pf_is_cid(f) && pf_cid_to_gid(f, 5) == 5 && pf_cid_to_gid(f, 37) == -1, "name-keyed cid_to_gid");
    CHECK(f && pf_glyph_by_name(f, "Folio.two") == 35 && pf_glyph_by_name(f, "nonesuch") == -1, "custom CFF names");
    CHECK(f && pf_kind(f) == PF_CFF && pf_cmap(f, 3, 1, 65) == -1, "bare CFF: kind, no cmap");
    pf_free(f); free(d);
  }
  /* TrueType: names from post, out-of-range glyphs */
  if ((d = load_file("sans.ttf", &len)) != NULL) {
    Path p;
    f = pf_load(d, len, err, sizeof err);
    path_init(&p);
    CHECK(f && pf_kind(f) == PF_TRUETYPE && pf_glyph_by_name(f, "Aacute") > 0 && pf_glyph_by_name(f, "space") == pf_cmap(f, 3, 1, 32), "post names");
    CHECK(f && !pf_glyph_outline(f, -1, &p) && !pf_glyph_outline(f, 99999, &p) && pf_glyph_advance(f, 99999) == 0, "glyph range");
    CHECK(f && pf_glyph_outline(f, pf_cmap(f, 3, 1, 32), &p) && p.nops == 0, "space is empty");
    CHECK(f && pf_builtin_encoding(f) == NULL, "TrueType has no built-in encoding");
    path_free(&p);
    pf_free(f); free(d);
  }
  /* hostile fonts: a subroutine bomb, endless recursion, a composite made of itself; cut short, not crashed or hung */
  if ((d = load_file("bomb.cff", &len)) != NULL) {
    Path p;
    clock_t t0 = clock();
    f = pf_load(d, len, err, sizeof err);
    path_init(&p);
    CHECK(f && pf_num_glyphs(f) == 4, "bomb.cff loads");
    CHECK(f && !pf_glyph_outline(f, 1, &p) && p.nops == 0, "subroutine bomb drawn");
    CHECK(f && !pf_glyph_outline(f, 2, &p) && !pf_glyph_outline(f, 3, &p), "endless recursion drawn");
    CHECK((double)(clock() - t0) / CLOCKS_PER_SEC < 5, "bomb.cff took %g s", (double)(clock() - t0) / CLOCKS_PER_SEC);
    path_free(&p);
    pf_free(f); free(d);
  }
  if ((d = load_file("selfref.ttf", &len)) != NULL) {
    Path p;
    clock_t t0 = clock();
    f = pf_load(d, len, err, sizeof err);
    path_init(&p);
    CHECK(f && !pf_glyph_outline(f, pf_cmap(f, 3, 1, 'A'), &p) && pf_glyph_outline(f, pf_cmap(f, 3, 1, 'B'), &p), "self-referencing composite");
    CHECK((double)(clock() - t0) / CLOCKS_PER_SEC < 5, "selfref.ttf took %g s", (double)(clock() - t0) / CLOCKS_PER_SEC);
    path_free(&p);
    pf_free(f); free(d);
  }
  /* the synthetic Type 1: lenIV 2, its encoding, the font matrix */
  if ((d = load_file("synth.pfb", &len)) != NULL) {
    const int *e;
    f = pf_load(d, len, err, sizeof err);
    e = f ? pf_builtin_encoding(f) : NULL;
    CHECK(e && e[65] == pf_glyph_by_name(f, "A") && e[0xC1] == pf_glyph_by_name(f, "Aacute") && e[32] == -1, "Type 1 encoding");
    pf_free(f); free(d);
  }
}

/* the encodings and the glyph list */
static void check_enc(void)
{
  static const struct { const char *n; u32 u; } t[] = {
    { "A", 0x41 }, { "space", 0x20 }, { "eacute", 0xE9 }, { "Euro", 0x20AC }, { "bullet", 0x2022 }, { "fi", 0xFB01 },
    { "alpha", 0x3B1 }, { "Omega", 0x3A9 }, { "Delta", 0x394 }, { "integral", 0x222B }, { "arrowright", 0x2192 },
    { "a1", 0x2701 }, { "a191", 0x27BE }, { "a71", 0x25CF }, { "uni20AC", 0x20AC }, { "uni00410042", 0x41 }, { "u1F600", 0x1F600 },
    { "u0041", 0x41 }, { "A.sc", 0x41 }, { "a.alt1", 0x61 }, { "f_i", 0xFB01 }, { "f_x", 0x66 }, { "T_h.liga", 0x54 }, { "g123", 0 },
    { "cid42", 0 }, { ".notdef", 0 }, { "", 0 }, { "nosuchglyph", 0 }, { "uniD800", 0 }, { "Zcaron", 0x17D }, { "afii10017", 0x410 },
    { "Wgrave", 0x1E80 }, { "Ccaron", 0x10C }, { "copyrightsans", 0xF8E9 }, { "uni", 0 }, { "u12", 0 }, { "quotesingle", 0x27 },
  };
  const char *const *tabs[7];
  int i, k;
  tabs[0] = enc_standard; tabs[1] = enc_winansi; tabs[2] = enc_macroman; tabs[3] = enc_macexpert; tabs[4] = enc_pdfdoc;
  tabs[5] = enc_symbol; tabs[6] = enc_zapf;
  for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++)
    CHECK(agl_unicode(t[i].n) == t[i].u, "agl_unicode(%s) = %X, expected %X", t[i].n, (unsigned)agl_unicode(t[i].n), (unsigned)t[i].u);
  CHECK(agl_unicode(NULL) == 0, "agl_unicode(NULL)");
  /* every name in every table has a code point; the WinAnsi ones come back by agl_name */
  for (k = 0; k < 7; k++)
    for (i = 0; i < 256; i++)
      if (tabs[k][i]) CHECK(agl_unicode(tabs[k][i]) != 0, "table %d code %d: %s has no code point", k, i, tabs[k][i]);
  for (i = 32; i < 256; i++) {
    const char *n = enc_winansi[i], *b;
    if (!n) continue;
    b = agl_name(agl_unicode(n));
    CHECK(b && agl_unicode(b) == agl_unicode(n), "agl_name(agl_unicode(%s)) = %s", n, b ? b : "(none)");
  }
  CHECK(agl_name(0x41) && !strcmp(agl_name(0x41), "A"), "agl_name A");
  CHECK(agl_name(0xE9) && !strcmp(agl_name(0xE9), "eacute"), "agl_name eacute");
  CHECK(agl_name(0x20AC) && !strcmp(agl_name(0x20AC), "Euro"), "agl_name Euro");
  CHECK(agl_name(0x2019) && !strcmp(agl_name(0x2019), "quoteright"), "agl_name quoteright");
  CHECK(agl_name(0x10FFFF) == NULL && agl_name(0) == NULL, "agl_name of nothing");
  CHECK(!strcmp(enc_standard[0x41], "A") && !strcmp(enc_standard[0xC2], "acute") && !enc_standard[0x80], "StandardEncoding");
  CHECK(!strcmp(enc_winansi[0x80], "Euro") && !strcmp(enc_winansi[0x92], "quoteright") && !strcmp(enc_winansi[0xE9], "eacute"), "WinAnsi");
  CHECK(!strcmp(enc_macroman[0x8E], "eacute") && !strcmp(enc_macroman[0xDE], "fi"), "MacRoman");
  CHECK(!strcmp(enc_pdfdoc[0xA0], "Euro") && !strcmp(enc_pdfdoc[0x18], "breve"), "PDFDoc");
  CHECK(!strcmp(enc_symbol[0x61], "alpha") && !strcmp(enc_symbol[0xF2], "integral"), "Symbol");
  CHECK(!strcmp(enc_zapf[0x21], "a1") && !strcmp(enc_zapf[0xFE], "a191") && !enc_zapf[0x7F], "ZapfDingbats");
  CHECK(enc_macexpert[0x21] && !strcmp(enc_macexpert[0x21], "exclamsmall"), "MacExpert");
}

/* ------------------------------------------------------------------ */
/* fuzzing: each font truncated and corrupted, then loaded and asked everything */
static u32 rng = 12345;
static long fz_loaded, fz_glyphs, fz_drawn;
static u32 rnd(void) { rng = rng * 1103515245u + 12345u; return (rng >> 8) & 0xFFFFFF; }
static void exercise(const u8 *d, size_t len)
{
  char err[128];
  PFont *f = pf_load(d, len, err, sizeof err);
  Path p;
  int g, n;
  if (!f) return;
  fz_loaded++;
  path_init(&p);
  n = pf_num_glyphs(f);
  for (g = 0; g < n; g++) {
    path_reset(&p);
    fz_glyphs++;
    if (pf_glyph_outline(f, g, &p)) fz_drawn++;
    pf_glyph_advance(f, g);
    pf_glyph_name(f, g);
  }
  pf_glyph_by_name(f, "A"); pf_glyph_by_name(f, "Aacute");
  for (g = 0; g < 300; g += 7) { pf_cmap(f, 3, 1, (u32)g); pf_cmap(f, 1, 0, (u32)g); pf_cmap(f, 3, 10, (u32)g * 977); pf_cid_to_gid(f, g); }
  pf_builtin_encoding(f);
  path_free(&p);
  pf_free(f);
}
static void fuzz(const char *name, int variants)
{
  size_t len;
  u8 *d = load_file(name, &len), *m;
  int v;
  if (!d) { CHECK(0, "can't read %s", name); return; }
  if (!(m = (u8 *)malloc(len + 1))) { free(d); return; }
  for (v = 0; v < variants; v++) {
    size_t n = len;
    int kind = v % 5, k;
    memcpy(m, d, len);
    if (kind == 0) n = rnd() % len;                                   /* truncated */
    else if (kind == 1) { for (k = 0; k < 1 + (int)(rnd() % 8); k++) m[rnd() % len] ^= (u8)(1 + rnd() % 255); }   /* a few bytes */
    else if (kind == 2) { for (k = 0; k < 1 + (int)(rnd() % 4); k++) m[rnd() % (len < 512 ? len : 512)] = (u8)rnd(); }  /* the header */
    else if (kind == 3) {                                             /* a run overwritten */
      size_t at = rnd() % len, l = 1 + rnd() % 64;
      for (k = 0; k < (int)l && at + k < len; k++) m[at + k] = (u8)(rnd() % 3 == 0 ? 0xFF : rnd());
    } else { for (k = 0; k < 40; k++) m[rnd() % len] = (u8)rnd(); n = len - rnd() % (len / 4 + 1); }  /* both */
    exercise(m, n);
  }
  /* and pure noise with the right magic */
  for (v = 0; v < variants / 10; v++) {
    size_t k;
    for (k = 0; k < len; k++) m[k] = (u8)rnd();
    memcpy(m, d, 4);
    exercise(m, len);
  }
  free(m); free(d);
}

int main(int argc, char **argv)
{
  static const char *fonts[] = { "sans.ttf", "carlito.ttf", "test.ttc", "sans.otf", "sans.cff", "enc.cff", "cid3.cff", "cid0.cff",
                                 "synth.pfb", "synth.pfa", "synth.t1", "charter.pfb",
                                 "enc0.cff", "bomb.cff", "selfref.ttf" };
  int i, variants = argc > 2 ? atoi(argv[2]) : 300;
  if (argc > 1) dir = argv[1];
  if (argc > 3) rng = (u32)strtoul(argv[3], NULL, 10);
  check_enc();
  check_misc();
  check_expected();
  for (i = 0; i < (int)(sizeof fonts / sizeof fonts[0]); i++) {
    fz_loaded = fz_glyphs = fz_drawn = 0;
    fuzz(fonts[i], variants);
    printf("fuzzed %-12s %d variants: %ld loaded, %ld of %ld glyphs drawn\n", fonts[i], variants + variants / 10, fz_loaded, fz_drawn, fz_glyphs);
  }
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
