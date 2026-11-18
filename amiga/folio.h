/*
 * folio.h - declarations shared by Folio's Amiga front end (MUI): the documents open and how each
 * is being viewed, the tools, the custom classes (page view, toolbars, tab strip, panes, Home and
 * Tools pages, status bar), the changes with their undo, and the dialogs' look.
 *
 * For newcomers to MUI: each custom class here is a BOOPSI class made with MUI_CreateCustomClass.
 * Its attributes (MUIA_) are tags read with get() and written with set(), and "notify" means
 * other objects can watch the attribute with MUIM_Notify. Its methods (MUIM_) are messages sent
 * with DoMethod(obj, MUIM_..., ...). All of Folio's tags sit in the TAG_USER range at QTAG.
 */
#ifndef FOLIO_AMIGA_H
#define FOLIO_AMIGA_H

#include <stdio.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include "../core/pdf.h"
#include "../core/render.h"
#include "../core/textpage.h"
#include "../core/pdfedit.h"

extern struct Library *MUIMasterBase, *CyberGfxBase;

#define QTAG (TAG_USER | 0x464F0000UL)

/* ------------------------------------------------------------------ tools */
enum { TL_SELECT, TL_HAND, TL_NOTE, TL_HIGHLIGHT, TL_UNDERLINE, TL_STRIKE, TL_TEXTBOX, TL_INK, TL_RECT, TL_OVAL, TL_LINE,
       /* (after 0.1.2) comment tools: */
       TL_SQUIGGLY, TL_ARROW, TL_CLOUD, TL_STAMP, TL_MEASURE, TL_AREA, TL_PERIM,
       /* page tools (doctools.c): */
       TL_ADDTEXT, TL_ADDIMAGE, TL_LINK, TL_REDACT, TL_FIELD, TL_CROP,
       /* Fill & Sign: */
       TL_FTEXT, TL_CHECK, TL_CROSS, TL_DOT, TL_DATE, TL_SIGN, TL_INITIALS, TL_N };
#define TL_LASTCOMMENT TL_PERIM
/* tools that take a click (the others a drag) */
#define TL_IS_CLICK(t) ((t) == TL_STAMP || (t) == TL_ADDTEXT || ((t) >= TL_FTEXT && (t) <= TL_INITIALS))
extern int tool;                       /* TL_ */
extern const char *const tool_names[TL_N];
typedef struct {
  u32 color[TL_N];                     /* each tool's colour */
  double width;                        /* drawing and shapes: line width (points) */
  double text_size;                    /* text boxes: points */
  char author[64];                     /* written into new comments (Latin-1) */
  /* (after 0.1.2) */
  int field_kind;                      /* the form field Add Field makes (FF_, 7 date, 8 table, 9 picture) */
  char stamp[32];                      /* the stamp's text */
  char signature[64], initials[16];    /* Fill & Sign (typed) */
  double scale;                        /* measuring: points per unit */
  char unit[8];
} Opts;
extern Opts opt;
void set_tool(int t);                  /* (main.c) the tool chosen: view and toolbars told */

/* ------------------------------------------------------------------ the documents (main.c) */
enum { VM_READ, VM_ORGANIZE };
enum { FIT_NONE, FIT_WIDTH, FIT_PAGE };
/* one step that can be undone */
enum { U_ADDANNOT, U_DELANNOT, U_ANNOTTEXT, U_ROTATE, U_DELPAGE, U_MOVEPAGE, U_INSERT, U_SNAPSHOT };
typedef struct { int kind, page, index, a, b, grp; PObj *obj; char *text; } Undo;
#define MAXUNDO 40
/* the text of a page, kept by the page's dictionary (so it survives pages moving) */
typedef struct { PObj *pg; TextPage tp; } PText;
typedef struct { PObj *pg; int rot; Pixmap pm; } Thumb;
typedef struct {
  PDoc *pdf;
  RCache *rc;
  char path[512];                      /* where it was opened from or saved to */
  char name[108];                      /* the tab's name (Latin-1) */
  int mode;                            /* VM_ */
  double zoom;                         /* 1.0: a point a pixel */
  int fit;                             /* FIT_: the zoom follows the window */
  int rot;                             /* the view turned this many quarter turns clockwise */
  LONG sx, sy;                         /* the scroll: the laid-out pages' pixel at the view's top left */
  int placed;                          /* zoom and scroll have been set for the window */
  int page;                            /* the page in view (the one most of the view shows) */
  PText *texts; int ntexts, captexts;  /* pages' text, made when wanted */
  int sel_page, sel_a, sel_b;          /* the text selected: page and characters (sel_page -1: none) */
  int find_page, find_a, find_b;       /* the match found last (-1: none) */
  int an_page, an_index;               /* the annotation chosen (an_page -1: none) */
  u8 *picked;                          /* organizing: the pages picked (npages of them) */
  Thumb *thumbs; int nthumbs, capthumbs;
  POutline *outline; int outline_done;
  Undo undo[MAXUNDO]; int nundo;
  int was_encrypted;
} DocView;
#define MAXDOCS 12
extern DocView docs[MAXDOCS];
extern int ndocs, curdoc;
enum { PM_HOME, PM_TOOLS, PM_DOC };
extern int page_mode;                  /* which of the window's pages shows: Home, Tools or a document */
DocView *cur_view(void);               /* NULL when nothing is open (or Home or Tools is shown) */
DocView *cur_doc_view(void);           /* the current document even while Home or Tools shows */
TextPage *page_text(DocView *v, int page);     /* the page's text, extracted when first wanted */
int  page_text_ready(DocView *v, int page);    /* it has been */
Thumb *thumb_get(DocView *v, int page, int make);   /* the page's thumbnail; made now if make */
#define THUMB_W 96
#define THUMB_H 124
void doc_pages_changed(DocView *v);    /* pages were added, taken out or moved: per-page state made again */
void page_changed(DocView *v, int page);   /* a page's look changed (annotations, rotation) */
void doc_changed(void);                /* after any change: view, panes, menus brought up to date */
void doc_changed_panes(void);          /* (0.1.1) the same but the view, for a change one page shows by itself */
void page_edited(DocView *v, int page, int added);   /* (0.1.1) as page_changed, the picture kept (or added to) */

/* ------------------------------------------------------------------ the changes (edits.c; the page text and thumbnails too) */
void undo_begin(void);                 /* the steps pushed from here on undo together */
void undo_push(DocView *v, int kind, int page, int index, int a, int b, PObj *obj, const char *text);
void undo_clear(DocView *v);
int  undo_last(DocView *v);            /* the last step undone; 0 if there is none */
const char *undo_name(DocView *v);     /* what Undo would undo ("" none) */
/* annotations made from what the view collected (page default user space) */
void edit_markup(DocView *v, int page, int kind, const double *quads, int nquads);
void edit_note(DocView *v, int page, double x, double y);
void edit_textbox(DocView *v, int page, const double *rect);
void edit_ink(DocView *v, int page, const double *pts, int npts);
void edit_shape(DocView *v, int page, int kind, const double *c);
void edit_delete_annot(DocView *v);
void edit_annot_text(DocView *v);
/* pages: the picked ones (organizing) or the one in view */
void edit_rotate(DocView *v, int quarter);
void edit_delete_pages(DocView *v);
void edit_move(DocView *v, int from, int to);
void edit_insert_blank(DocView *v, int at);
void edit_insert_file(DocView *v, int at);
/* the selected text as quads for a markup (8 numbers each); its count */
int  selection_quads(DocView *v, double **quads);

/* ------------------------------------------------------------------ the page view (view.c) */
#define MUIA_View_VProp     (QTAG + 1)     /* (init) the flat scroll bars it keeps up to date */
#define MUIA_View_HProp     (QTAG + 2)
#define MUIA_View_Top       (QTAG + 3)     /* (set) from the scroll bars */
#define MUIA_View_Left      (QTAG + 4)
#define MUIA_View_Changed   (QTAG + 5)     /* (notify) the document was changed */
#define MUIA_View_Page      (QTAG + 6)     /* (notify) the page in view changed */
#define MUIA_View_Zoomed    (QTAG + 7)     /* (notify) the zoom changed */
#define MUIA_View_Picked    (QTAG + 8)     /* (notify) the annotation or text chosen changed */
#define MUIA_View_Link      (QTAG + 9)     /* (notify) a web link was clicked: view_uri */
#define MUIA_View_OrgPick   (QTAG + 10)    /* (notify) organizing: the pages picked changed */
#define MUIA_View_EditNote  (QTAG + 11)    /* (notify) a comment was double-clicked */
#define MUIM_View_Refresh   (QTAG + 0x100) /* another document, or the pages changed: laid out and drawn again */
#define MUIM_View_Redraw    (QTAG + 0x101) /* the same pages, drawn again (the cache kept) */
#define MUIM_View_Zoom      (QTAG + 0x102) /* struct MUIP_View_Zoom */
struct MUIP_View_Zoom { ULONG MethodID; LONG permille; LONG how; };  /* how: 0 as given, 1 a step in, -1 out, 2 fit width, 3 fit page */
#define MUIM_View_GoPage    (QTAG + 0x103) /* struct MUIP_View_GoPage: the page (and a point on it) shown */
struct MUIP_View_GoPage { ULONG MethodID; LONG page; LONG top; };   /* top: the page's y (points, from its bottom) to show at the view's top, -1 the page's top */
#define MUIM_View_Tick      (QTAG + 0x104) /* (timer) the next page drawn */
#define MUIM_View_Tool      (QTAG + 0x105) /* the tool changed: what it was doing put away */
#define MUIM_View_Forget    (QTAG + 0x106) /* struct MUIP_View_Forget: a document's pictures dropped */
struct MUIP_View_Forget { ULONG MethodID; DocView *v; PObj *pg; };  /* pg NULL: all its pages */
#define MUIM_View_ShowMatch (QTAG + 0x107) /* the match found shown */
/* 0.1.1: a page's look changed (pg in v). added: an annotation was added at the end of its list, which
   is drawn over the page's picture at once; otherwise the picture stays on screen until drawn again */
#define MUIM_View_PageEdited (QTAG + 0x108)
struct MUIP_View_PageEdited { ULONG MethodID; DocView *v; PObj *pg; LONG added; };
struct MUI_CustomClass *view_class(void);
void view_class_free(void);
extern char view_uri[512];             /* the web link clicked */
extern char view_info[96];             /* what the pointer is over, for the status bar */

/* ------------------------------------------------------------------ toolbars (bars.c) */
enum { SI_END, SI_ICON, SI_LABEL, SI_TEXT, SI_SEP, SI_GAP, SI_COLOR };
typedef struct { UBYTE kind; ULONG id; int icon; const char *label, *help; } StripItem;
#define MUIA_Strip_Items    (QTAG + 0x200) /* (init) const StripItem *, SI_END ended */
#define MUIA_Strip_Vertical (QTAG + 0x201) /* (init) a column of icons */
#define MUIA_Strip_Pick     (QTAG + 0x202) /* (notify) an item was clicked: its id */
#define MUIA_Strip_Size     (QTAG + 0x203) /* (init) the icons' size (16) */
#define MUIM_Strip_Refresh  (QTAG + 0x204) /* the items' states changed */
struct MUI_CustomClass *strip_class(void);
/* (main.c) an item's state for the strips: SS_ bits; and the text of an SI_TEXT, the colour of an SI_COLOR */
#define SS_DISABLED 1
#define SS_CHECKED  2
ULONG strip_state(ULONG id);
const char *strip_text(ULONG id);
ULONG strip_color(ULONG id);
/* the tab strip: Home, Tools, then a tab for each document */
#define MUIA_Tabs_Pick      (QTAG + 0x210) /* (notify) a tab clicked: -2 Home, -1 Tools, else the document */
#define MUIA_Tabs_Close     (QTAG + 0x211) /* (notify) a document tab's cross clicked */
#define MUIM_Tabs_Refresh   (QTAG + 0x212)
struct MUI_CustomClass *tabs_class(void);
/* the status bar */
#define MUIM_Status_Refresh (QTAG + 0x220)
struct MUI_CustomClass *status_class(void);
#define MUIA_FlatScroll_Horiz (QTAG + 0x360)  /* (init) across instead of up and down */
#define MUIA_Popup_Chosen   (QTAG + 0x370) /* (set by popup_list) the entry chosen */
struct MUI_CustomClass *flatscroll_class(void);
void bars_free(void);

/* ------------------------------------------------------------------ panes and pages (panes.c) */
/* the navigation pane: the pages' thumbnails, or the bookmarks */
#define MUIA_Nav_Mode       (QTAG + 0x300) /* 0 thumbnails, 1 bookmarks */
#define MUIA_Nav_Go         (QTAG + 0x301) /* (notify) a page was chosen (get: nav_page, nav_top) */
#define MUIA_Nav_VProp      (QTAG + 0x302) /* (init) its scroll bar */
#define MUIA_Nav_Top        (QTAG + 0x303) /* (set) from the scroll bar */
#define MUIM_Nav_Refresh    (QTAG + 0x304)
#define MUIM_Nav_Tick       (QTAG + 0x305) /* (timer) the next thumbnail made */
struct MUI_CustomClass *nav_class(void);
extern int nav_page; extern double nav_top;
/* the tools pane on the right: the tools, or (commenting) the comments */
#define MUIA_Pane_Pick      (QTAG + 0x310) /* (notify) a tool clicked: its command; ID_COMMENTPICK a comment (pane_page, pane_index) */
#define MUIM_Pane_Refresh   (QTAG + 0x311)
struct MUI_CustomClass *pane_class(void);
extern int pane_page, pane_index;
/* the Home page (open a file, the recent ones) and the Tools page (every tool as a card) */
#define MUIA_Home_Pick      (QTAG + 0x320) /* (notify) a command: ID_OPEN, ID_RECENT0 + n, ID_TOOLS ... */
#define MUIM_Home_Refresh   (QTAG + 0x321)
struct MUI_CustomClass *home_class(void);
#define MUIA_Grid_Pick      (QTAG + 0x330) /* (notify) a tool's command */
struct MUI_CustomClass *grid_class(void);
void panes_free(void);
#define MAXRECENT 8
extern char recent[MAXRECENT][512];
extern int nrecent;

/* ------------------------------------------------------------------ drawing helpers (widgets.c) */
void popup_list(Object *owner, LONG x, LONG y, LONG w, const char *const *labels, int n, int cur, ULONG attr);
void popup_free(void);
void draw_rgb_box(struct RastPort *rp, struct Screen *scr, LONG x0, LONG y0, LONG x1, LONG y1, ULONG rgb);
void draw_rgb(struct RastPort *rp, struct Screen *scr, const UBYTE *rgb, LONG mod, LONG x, LONG y, LONG w, LONG h);
int  truecolor(struct Screen *scr);
void pens_free(void);
LONG rgb_pen(struct Screen *scr, ULONG c);
/* text cut short to fit maxw (Latin-1) with its top at y; a UTF-8 string as Latin-1 */
void text_fit(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG maxw, LONG pen);
void to_latin1(const char *u, char *out, size_t cap);

/* ------------------------------------------------------------------ main.c helpers */
extern Object *app, *win, *view, *mainbar, *combar, *orgbar, *tabs, *navpane, *navstrip, *toolpane, *statusbar, *pagestr, *findstr, *findbar;
extern int pref_dark, show_nav, show_tools, nav_mode;
extern char pub_screen[64];
void refresh_bars(void);              /* toolbars, tabs, status bar and menus brought up to date */
int    ask(const char *gadgets, const char *fmt, const char *arg);
ULONG  run_modal(Object *w, const ULONG *ends, void (*each)(ULONG id));
Object *dialog_window(const char *title, Object *contents, const char *oklabel);
int    ask_string(const char *title, const char *label, char *buf, int cap);
int    ask_text(const char *title, const char *label, char *buf, int cap);   /* several lines */
LONG   pick_color(const char *title, ULONG start);
int    file_request(int save, const char *title, char *path, size_t cap, const char *pattern);
u8    *read_file(const char *path, size_t *len);
int    write_file(const char *path, const u8 *data, size_t len);
/* a PDF read and opened, asking for its password if it has one; NULL (said why) if it can't be */
PDoc  *open_pdf(const char *path, int *encrypted);

/* doctools.c: PDFCraft's other tools */
int  doc_command(ULONG id);            /* 1 if it was one of its commands */
int  form_click(DocView *v, int page, double x, double y);   /* a form field there filled in: 1 */
void tool_click(DocView *v, int page, int t, double x, double y);
void tool_drag(DocView *v, int page, int t, const double *c, int moved);
void doc_snapshot(DocView *v);         /* before a change undo can only take back by the whole document */
void doc_restore(DocView *v, Undo *u); /* (edits.c's undo) */
ULONG doc_strip_state(ULONG id);       /* SS_ for its commands */
void doc_redraw_all(DocView *v);       /* every page's picture, text and thumbnail made again */

/* dialogs.c */
void dlg_properties(void);
void dlg_goto(void);
int  dlg_blank(double *w, double *h);
void dlg_export_page(void);
void dlg_export_text(void);
void dlg_extract(void);
int  dlg_password(const char *name, char *pw, int cap, int again);

/* the clipboard (clip.c): Latin-1 text as FTXT */
int   clip_write(const char *s, long len);

/* ------------------------------------------------------------------ shared bits */
#define QDLG (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(), \
             (ULONG)(theme_dialog_back() ? MUIA_Background : TAG_IGNORE), (ULONG)theme_dialog_back()
struct TextFont *theme_dialog_font(void);
const char *theme_dialog_back(void);
/* (qbutton.c) a dialog button, a label and a section title drawn in Folio's theme */
Object *QButton(const char *label);
Object *QLabel(const char *label, ULONG flags);
Object *QTitleObject(const char *t);
struct MUI_CustomClass *qbutton_class(void);
void qbutton_class_free(void);
#define QWindowObject WindowObject, (ULONG)(pub_screen[0] ? MUIA_Window_PublicScreen : TAG_IGNORE), (ULONG)pub_screen

/* debugging log (DEBUG=file), or NULL */
extern FILE *qlog;
#define LOG(...) do { if (qlog) { fprintf(qlog, __VA_ARGS__); fputc('\n', qlog); fflush(qlog); } } while (0)

#endif
