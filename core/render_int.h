/*
 * render_int.h - what Folio's rendering files share (render.c, pimage.c, shade.c): the renderer's
 * state, colour spaces, the clip and the painting primitives. Not for the front end.
 */
#ifndef FOLIO_RENDER_INT_H
#define FOLIO_RENDER_INT_H

#include "render.h"
#include "raster.h"
#include "font.h"
#include "func.h"

/* colour spaces */
enum { CS_GRAY, CS_RGB, CS_CMYK, CS_LAB, CS_INDEXED, CS_SEP, CS_PATTERN, CS_NONE };
typedef struct CSpace {
  int kind, n;              /* n: components a colour has */
  struct CSpace *base;      /* Indexed: its base; Separation/DeviceN: the alternate; Pattern: the uncoloured base */
  u8 *lookup; int hival;    /* Indexed */
  PFunc *tint;              /* Separation/DeviceN: tint transform */
  double range[8];          /* Lab: a and b ranges; white point */
  int refs;
} CSpace;
CSpace *cs_load(PDoc *d, PObj *o, PObj *resources, int depth);   /* NULL: unusable */
CSpace *cs_device(int kind);                                     /* the shared DeviceGray/RGB/CMYK (never freed) */
void    cs_release(CSpace *c);
CSpace *cs_retain(CSpace *c);
/* comps (in the space's own terms) to RGB 0-1 */
void    cs_to_rgb(const CSpace *c, const double *comps, double *rgb);
void    cs_default(const CSpace *c, double *comps);              /* the initial colour (black) */

/* the clip: bounds in render-area pixels (exclusive) and, for anything not a rectangle, a mask */
typedef struct Clip { int refs; int x0, y0, x1, y1; u8 *mask; } Clip;

#define MAXDASH 16
typedef struct {
  Mat ctm;
  CSpace *fcs, *scs; double fc[32], sc[32];
  PObj *fpat, *spat;        /* the pattern chosen with scn (borrowed) */
  u8 frgb[3], srgb[3];
  double lw, miter; int cap, join; double dash[MAXDASH]; int ndash; double phase;
  double fa, sa;            /* constant alpha for filling and stroking */
  int blend;                /* 0 normal, 1 multiply, 2 screen, 3 darken, 4 lighten */
  PdfFont *font; double size, cs, ws, th, tl, rise; int tmode;
  Clip *clip;
  int knockout_text;        /* (unused) */
} GState;

#define MAXSAVE 64
typedef struct Render {
  PDoc *d; RCache *c; const RenderOpts *o;
  int W, H;                 /* the area drawn */
  u8 *rgb;                  /* W * H * 3 */
  u8 *alpha;                /* (drawing a tiling pattern's cell: coverage recorded here, W * H) */
  Mat base;                 /* default user space -> the area's pixels */
  Mat page_inv;             /* the area's pixels -> default user space (for the text) */
  GState gs[MAXSAVE]; int ngs;
  Path path; int clip_pending;
  Path textclip; int textclip_used;
  Mat tm, tlm;
  int depth, hidden, stopped;
  long ops;
  int t3_uncoloured;        /* inside a d1 Type 3 glyph: colours are ignored */
  PObj *ocoff;              /* the optional content groups that are off (array, borrowed) */
} Render;
#define GS(r) (&(r)->gs[(r)->ngs])

/* painting primitives (render.c) */
typedef struct { Render *r; const u8 *rgb; int alpha; const Clip *clip; int blend;
                 void (*shade)(void *sud, double x, double y, u8 *rgb, int *alpha); void *sud; } Paint;
void paint_span(void *ud, int y, int x0, int x1, const u8 *cov);
void put_pixel(Render *r, int x, int y, const u8 *rgb, int a, int blend);
int  clip_box(const Render *r, int *x0, int *y0, int *x1, int *y1);  /* the current clip's bounds; 0 if empty */
u8   clip_at(const Clip *c, int W, int x, int y);                     /* 0-255 */
void run_content(Render *r, const u8 *data, size_t len, PObj *resources);
void run_form(Render *r, PObj *form, PObj *resources, const Mat *extra);

/* a separate drawing of W x H with its own transparent canvas (a tiling pattern's cell) */
Render *render_sub(Render *parent, int W, int H, Mat base, const RenderOpts *o);
void render_sub_free(Render *s);

/* images (pimage.c) */
void draw_image(Render *r, PObj *img, PObj *resources, PObj *inline_dict, const u8 *inline_data, size_t inline_len);
/* shadings and patterns (shade.c) */
void draw_shading(Render *r, PObj *sh, const Path *area, int evenodd, int stroke_path);   /* area NULL: the whole clip ("sh") */
int  fill_with_pattern(Render *r, PObj *pat, const Path *devpath, int evenodd, int stroke);  /* 0 if it couldn't */
#endif
