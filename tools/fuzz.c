/*
 * fuzz.c - (native, with ASan/UBSan) Folio's mutation fuzzer: seed PDFs (those in tests/gen) with
 * random damage (bits flipped, bytes set, removed or repeated, numbers and PDF tokens put in,
 * pieces of other seeds spliced in, the end cut off) opened, their first pages drawn small with
 * their text collected, the outline and information read and the document saved again. Each
 * input has a work bound (the drawing's abort callback stops a page after FUZZ_PAGE_MS of CPU
 * time); one that still takes over FUZZ_SECS is a hang. Before each run the input is written to
 * OUTDIR/fuzz-current.pdf, so a crash or hang leaves it there (tools/fuzz.sh keeps it and goes on).
 *
 *   fuzz OUTDIR RUNS SEED SEED.pdf...      RUNS mutated inputs from random seed SEED
 *   fuzz -r FILE.pdf...                    each file as it is (a crash found before, again)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "core/pdf.h"
#include "core/render.h"
#include "core/pdfwrite.h"
#include "core/font.h"

#define FUZZ_SECS 30                        /* a hang: one input taking longer than this */
#define FUZZ_PAGE_MS 1500                   /* the work bound: a page's drawing stopped after this */
#define MAXSEED (2u << 20)

/* (ASan: a huge allocation fails as it would on the Amiga, rather than ending the run) */
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "allocator_may_return_null=1:max_allocation_size_mb=512:detect_leaks=1"; }

static unsigned long long rs = 88172645463325252ULL;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }

typedef struct { u8 *d; size_t n; } Blob;
static Blob *seeds;
static int nseeds;

static const char *const toks[] = { "0", "-1", "-0.5", "1000000000000", "99999999999999999999", "2147483648", "4294967295",
  "0.0000001", "100000000000000000000000000000000000000", "<<", ">>", "[", "]", "(", ")", "<", ">", "{", "}", "/", "R", "obj",
  "endobj", "stream", "endstream", "xref", "trailer", "startxref", "q", "Q", "cm", "re", "f", "S", "W n", "d", "Do", "BT", "ET",
  "Tj", "TJ", "Tf", "Tm", "Tz", "TL", "BI", "ID", "EI", "sh", "gs", "cs", "scn", "sc", "SCN", "/Pattern cs /P0 scn", "c", "v",
  "[1 -2] 0 d", "[0.00001] 0 d", "/Length 0", "/Length -5", "/Filter /FlateDecode", "/Filter /DCTDecode", "/Filter /LZWDecode",
  "/Filter /CCITTFaxDecode", "/Filter /RunLengthDecode", "/Filter /ASCII85Decode", "/Predictor 15", "/Columns 100000",
  "/Colors 32", "/Width 65535", "/Height 65535", "/BitsPerComponent 16", "/BitsPerComponent 32", "/Prev 0", "/Size 99999999",
  "/Type /Pages", "/Kids [1 0 R]", "/Count 999999", "/Parent 1 0 R", "/Encrypt 1 0 R", "/FunctionType 4", "/FunctionType 0",
  "/ShadingType 4", "/ShadingType 6", "/ShadingType 7", "/BitsPerCoordinate 32", "/BitsPerFlag 8", "/PatternType 1",
  "/XStep 0.001", "/Subtype /Type3", "/Subtype /Image", "/ImageMask true", "/FontFile2", "/Indexed", "/ColorSpace /DeviceN",
  "/Rotate 90", "/MediaBox [0 0 1 1]", "/Annots [1 0 R]", "/Type /ObjStm", "/Type /XRef", "/W [1 4 2]", "/Index [0 1000000]",
  "1 0 R", "0 0 R", "%", "\\", "\n", "\r" };

static size_t mutate(u8 *buf, size_t n, size_t cap)
{
  int k, m = 1 + (int)(rnd() % 8);
  for (k = 0; k < m && n; k++) {
    size_t at = rnd() % n, len, i;
    switch (rnd() % 11) {
    case 0: case 1: buf[at] ^= (u8)(1 << (rnd() % 8)); break;
    case 2: buf[at] = (u8)rnd(); break;
    case 3: len = 1 + rnd() % 64; if (at + len > n) len = n - at; memmove(buf + at, buf + at + len, n - at - len); n -= len; break;
    case 4: len = 1 + rnd() % 256; if (at + len > n) len = n - at; if (n + len <= cap) { memmove(buf + at + len, buf + at, n - at); n += len; } break;
    case 5: case 6: {
      const char *t = toks[rnd() % (sizeof toks / sizeof toks[0])];
      size_t tl = strlen(t);
      if (n + tl + 1 <= cap) { memmove(buf + at + tl + 1, buf + at, n - at); memcpy(buf + at, t, tl); buf[at + tl] = ' '; n += tl + 1; }
      break; }
    case 7:                                   /* a run of digits changed */
      for (i = at; i < n && i < at + 40; i++) if (buf[i] >= '0' && buf[i] <= '9') { buf[i] = (u8)('0' + rnd() % 10); if (rnd() % 3 == 0) break; }
      break;
    case 8: {                                 /* a piece of another seed */
      Blob *s = &seeds[rnd() % nseeds];
      size_t from = rnd() % s->n;
      len = 1 + rnd() % 512;
      if (from + len > s->n) len = s->n - from;
      if (at + len > n) len = n - at;
      memcpy(buf + at, s->d + from, len);
      break; }
    case 9: n = at + (n - at) * (rnd() % 4) / 4; if (!n) n = 1; break;   /* the end cut off */
    case 10:                                  /* a number made much bigger or smaller */
      for (i = at; i < n && i < at + 200 && !(buf[i] >= '0' && buf[i] <= '9'); i++) ;
      if (i < n && n + 12 <= cap) { const char *t = rnd() & 1 ? "00000000000" : "-.000000001"; memmove(buf + i + 11, buf + i, n - i); memcpy(buf + i, t, 11); n += 11; }
      break;
    }
  }
  return n;
}

/* the work bound: polled by the drawing every 256 operators */
static clock_t page_start;
static int stop_page(void *ud) { (void)ud; return (clock() - page_start) * 1000 / CLOCKS_PER_SEC > FUZZ_PAGE_MS; }

static unsigned long sink;
static void one(const u8 *data, size_t len, int scale_pick, int page_pick)
{
  char err[200];
  PDoc *d = pdf_open(data, len, err, sizeof err);
  RCache *c;
  int i, first, last;
  if (!d) return;
  if (d->need_password) { pdf_password(d, scale_pick & 1 ? "user" : ""); pdf_ensure_pages(d); }
  {
    POutline *o = pdf_outline(d);
    char *t = pdf_info(d, "Title");
    if (o) pdf_outline_free(o);
    free(t);
  }
  c = rcache_new(d);
  first = page_pick % 4 == 0 && d->npages > 1 ? (int)(rnd() % (unsigned)d->npages) : 0;
  last = first + 2 < d->npages ? first + 2 : d->npages;
  for (i = first; c && i < last; i++) {
    RenderOpts o;
    Pixmap pm;
    TextPage tp;
    double box[4];
    int rot;
    memset(&o, 0, sizeof o);
    pdf_page_box(d, i, box, &rot);
    o.scale = scale_pick % 8 ? 0.25 : 0.05 + (rnd() % 100) / 50.0;
    o.rotate = (int)(rnd() % 4) * 90;
    o.paper = 0xFFFFFF; o.annots = 1;
    o.abort = stop_page;
    if (scale_pick % 5 == 0) { o.x = 3; o.y = 5; o.w = 40; o.h = 30; }
    textpage_init(&tp);
    o.text = &tp;
    pm.rgb = NULL;
    page_start = clock();
    if (pdf_render(d, c, i, &o, &pm) && pm.rgb) {
      size_t k, np = (size_t)pm.w * pm.h * 3;
      for (k = 0; k < np; k += 61) sink += pm.rgb[k];   /* (the pixels read: unset memory would show under MSan/valgrind) */
    }
    free(pm.rgb);
    textpage_finish(&tp);
    if (tp.no > 0) { char *t = textpage_text(&tp, 0, tp.no - 1); if (t) sink += strlen(t); free(t); }
    textpage_free(&tp);
  }
  if (c) rcache_free(c);
  if (page_pick % 3 == 0) {
    size_t n;
    u8 *out = pdf_save(d, NULL, 0, &n);
    if (out) {
      /* what was saved opens again */
      PDoc *d2 = pdf_open(out, n, err, sizeof err);
      if (d2) pdf_close(d2);
      free(out);
    }
  }
  pdf_close(d);
}

static const char *current = "fuzz-current.pdf";
static void on_alarm(int s)
{
  (void)s;
  fprintf(stderr, "fuzz: HANG (over %d s): the input is %s\n", FUZZ_SECS, current);
  _exit(3);
}

int main(int argc, char **argv)
{
  long runs, r;
  int i;
  u8 *buf;
  size_t cap = 0;
  static char path[600];
  time_t t0 = time(NULL);
  if (getenv("FUZZ_FONTS")) font_set_dir(getenv("FUZZ_FONTS"));
  signal(SIGALRM, on_alarm);
  if (argc >= 3 && !strcmp(argv[1], "-r")) {
    for (i = 2; i < argc; i++) {
      size_t n;
      u8 *d = file_read(argv[i], &n);
      int k;
      if (!d) { fprintf(stderr, "fuzz: can't read %s\n", argv[i]); return 2; }
      current = argv[i];
      alarm(FUZZ_SECS * 4);
      for (k = 0; k < 8; k++) one(d, n, k, k);
      alarm(0);
      free(d);
    }
    font_free_substitutes();
    printf("fuzz: %d files again, no crash or hang\n", argc - 2);
    return 0;
  }
  if (argc < 5) { fprintf(stderr, "usage: fuzz OUTDIR RUNS SEED SEED.pdf...  or  fuzz -r FILE.pdf...\n"); return 2; }
  snprintf(path, sizeof path, "%s/fuzz-current.pdf", argv[1]);
  current = path;
  runs = atol(argv[2]);
  rs ^= (unsigned long long)strtoul(argv[3], NULL, 10) * 0x9E3779B97F4A7C15ULL;
  if (!rs) rs = 1;
  seeds = (Blob *)calloc((size_t)argc, sizeof *seeds);
  for (i = 4; i < argc; i++) {
    size_t n;
    u8 *d = file_read(argv[i], &n);
    if (!d || !n || n > MAXSEED) { free(d); continue; }
    seeds[nseeds].d = d; seeds[nseeds].n = n; nseeds++;
    if (n > cap) cap = n;
  }
  if (!nseeds) return 2;
  cap = cap * 2 + 65536;
  buf = (u8 *)malloc(cap);
  for (r = 0; r < runs; r++) {
    Blob *s = &seeds[rnd() % nseeds];
    size_t n;
    FILE *f;
    int sp = (int)(rnd() % 1000), pp = (int)(rnd() % 1000);
    memcpy(buf, s->d, s->n);
    n = mutate(buf, s->n, cap);
    if (rnd() % 4 == 0) n = mutate(buf, n, cap);
    if ((f = fopen(path, "wb"))) { fwrite(buf, 1, n, f); fclose(f); }
    alarm(FUZZ_SECS);
    one(buf, n, sp, pp);
    alarm(0);
    if (r && r % 2000 == 0) { printf("fuzz: %ld runs, %ld s\n", r, (long)(time(NULL) - t0)); fflush(stdout); }
  }
  remove(path);
  printf("fuzz: %ld runs of %d seeds in %ld s, no crash or hang\n", runs, nseeds, (long)(time(NULL) - t0));
  for (i = 0; i < nseeds; i++) free(seeds[i].d);
  free(seeds); free(buf);
  font_free_substitutes();
  return sink == 1;
}
