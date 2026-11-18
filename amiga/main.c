/*
 * main.c - Folio, a PDF viewer and editor for the Amiga (MUI) laid out as PDFCraft is: the Home,
 * Tools and document tabs along the top, the toolbar under them (open, save, find, the page, the
 * select and hand tools, the zoom, quick comment tools), the comment or organize bar when one of
 * those tools is open, the page thumbnails or bookmarks on the left, the tools or comments on the
 * right, the pages in the middle, and the menus.
 *
 * This file holds the window, the menus, toolbars and their commands, the open documents, the
 * files (opening, with a password when one is asked for, and saving through core/pdfwrite.c),
 * finding text, and the program's start and end.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <clib/alib_protos.h>
#include "folio.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "splash.h"
#include "../core/font.h"
#include "../core/pdfwrite.h"

#ifndef MUIA_Window_DisableKeys
#define MUIA_Window_DisableKeys 0x80424c36UL
#endif
#ifndef MUIKEYF_WINDOW_CLOSE
#define MUIKEYF_WINDOW_CLOSE (1UL << MUIKEY_WINDOW_CLOSE)
#endif

size_t __stack = 131072;     /* (signatures' big numbers and the OCR want more than the 4K a Shell gives) */
const char verstag[] = "$VER: Folio 0.1.2 (28.10.2026) a PDF viewer and editor for the Amiga (MUI)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *MUIMasterBase, *LayersBase, *DiskfontBase, *KeymapBase, *UtilityBase, *CyberGfxBase;
extern struct WBStartup *_WBenchMsg;

FILE *qlog;
char pub_screen[64];
Object *app, *win, *view, *mainbar, *combar, *orgbar, *tabs, *navpane, *navstrip, *toolpane, *statusbar, *pagestr, *findstr, *findbar;
static Object *mainbar2, *pages, *home, *grid, *vscroll, *hscroll, *navscroll, *navgroup, *findstrip;
int pref_dark = 0, pref_lang = -1, show_nav = 0, show_tools = 1, nav_mode = 0, comment_mode;
DocView docs[MAXDOCS];
int ndocs, curdoc = -1, page_mode = PM_HOME;
Opts opt;
int tool = TL_SELECT;
static int fonts_assigned;
static char lastdir[512];
static char findtext[128];

const char *const tool_names[TL_N] = {
  "Select Text", "Hand", "Sticky Note", "Highlight Text", "Underline Text", "Strikethrough", "Add Text Box", "Draw", "Rectangle", "Oval", "Line",
  "Squiggly Underline", "Arrow", "Cloud", "Stamp", "Measure Distance", "Measure Area", "Measure Perimeter",
  "Add Text", "Add Picture", "Add Link", "Redact Area", "Add Form Field", "Crop Page",
  "Fill In Text", "Checkmark", "Cross", "Dot", "Date", "Signature", "Initials"
};

/* ------------------------------------------------------------------ the menus */
#define M(title) { NM_TITLE, (STRPTR)title, 0, 0, 0, 0 }
#define I(label, key, id) { NM_ITEM, (STRPTR)label, (STRPTR)key, 0, 0, (APTR)(id) }
#define H(label) { NM_ITEM, (STRPTR)label, 0, 0, 0, 0 }
#define S(label, key, id) { NM_SUB, (STRPTR)label, (STRPTR)key, 0, 0, (APTR)(id) }
#define T(label, key, id) { NM_ITEM, (STRPTR)label, (STRPTR)key, CHECKIT | MENUTOGGLE, 0, (APTR)(id) }
#define BAR { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }
static struct NewMenu menus[] = {
  M("File"),
  I("Open...", "O", ID_OPEN), I("New Blank Document...", "N", ID_NEWBLANK),
  H("Create PDF"), S("From Pictures...", 0, ID_FROMIMAGES), S("From a Text File...", 0, ID_FROMTEXT), S("From the Clipboard", 0, ID_FROMCLIP),
  S("Pictures to Separate PDFs...", 0, ID_SEPARATE),
  I("Close", "W", ID_CLOSE), I("Close All", 0, ID_CLOSEALL), BAR,
  I("Save", "S", ID_SAVE), I("Save As...", 0, ID_SAVEAS), I("Revert to Saved", 0, ID_REVERT), BAR,
  H("Export"), S("Page as PNG Picture...", 0, ID_EXPPAGE), S("Text...", 0, ID_EXPTEXT), S("Web Page (HTML)...", 0, ID_EXPHTML),
  S("Rich Text (RTF)...", 0, ID_EXPRTF), S("Word (.docx)...", 0, ID_EXPDOCX), S("PowerPoint (.pptx)...", 0, ID_EXPPPTX), S("All Pictures...", 0, ID_EXPIMAGES),
  I("Extract Pages...", 0, ID_EXTRACT), I("Split Document...", 0, ID_SPLIT), BAR,
  I("Protect with Password...", 0, ID_SAVEPW), I("Reduce File Size...", 0, ID_REDUCE), I("Save as PDF/A...", 0, ID_PDFA), BAR,
  I("Properties...", "D", ID_PROPS), I("Word Count", 0, ID_WORDCOUNT), BAR,
  I("Home", 0, ID_HOME), I("All Tools", 0, ID_TOOLS), BAR,
  I("About Folio...", "?", ID_ABOUT), I("Quit", "Q", ID_QUIT),
  M("Edit"),
  I("Undo", "Z", ID_UNDO), BAR,
  I("Copy", "C", ID_COPY), I("Select All", "A", ID_SELALL), I("Deselect", 0, ID_DESELECT), BAR,
  I("Find...", "F", ID_FIND), I("Find Next", "G", ID_FINDNEXT), I("Find Previous", 0, ID_FINDPREV), BAR,
  I("Edit Comment Text...", "E", ID_EDITNOTE), I("Delete Comment", 0, ID_DELETE), BAR,
  I("Comment Author...", 0, ID_AUTHOR),
  M("View"),
  I("Zoom In", "+", ID_ZOOMIN), I("Zoom Out", "-", ID_ZOOMOUT), I("Actual Size", "1", ID_ZOOM100), I("Fit Width", "2", ID_FITWIDTH), I("Fit Page", "0", ID_FITPAGE), BAR,
  I("Rotate View Clockwise", "R", ID_ROTVIEWR), I("Rotate View Anticlockwise", 0, ID_ROTVIEWL), BAR,
  I("First Page", 0, ID_FIRST), I("Previous Page", 0, ID_PREV), I("Next Page", 0, ID_NEXT), I("Last Page", 0, ID_LAST), I("Go to Page...", "J", ID_GOTO), BAR,
  T("Page Thumbnails", 0, ID_THUMBS), T("Bookmarks", 0, ID_BOOKMARKS), T("Tools Pane", 0, ID_TOOLSPANE), BAR,
  T("Dark Interface", 0, ID_DARKUI),
  H("Language"), { NM_SUB, (STRPTR)"As in Locale Prefs", 0, CHECKIT, ~1L & 63, (APTR)(ID_LANG0) },
  { NM_SUB, (STRPTR)"English", 0, CHECKIT, ~2L & 63, (APTR)(ID_LANG0 + 1) }, { NM_SUB, (STRPTR)"Deutsch", 0, CHECKIT, ~4L & 63, (APTR)(ID_LANG0 + 2) },
  { NM_SUB, (STRPTR)"Fran\347ais", 0, CHECKIT, ~8L & 63, (APTR)(ID_LANG0 + 3) }, { NM_SUB, (STRPTR)"Espa\361ol", 0, CHECKIT, ~16L & 63, (APTR)(ID_LANG0 + 4) },
  { NM_SUB, (STRPTR)"Italiano", 0, CHECKIT, ~32L & 63, (APTR)(ID_LANG0 + 5) },
  M("Tools"),
  I("Select Text", "V", ID_TOOL0 + TL_SELECT), I("Hand", "H", ID_TOOL0 + TL_HAND), BAR,
  I("Comment", "K", ID_COMMENTMODE), I("Organize Pages", 0, ID_ORGANIZE), I("Combine Files...", 0, ID_COMBINE), I("Compare Files...", 0, ID_COMPARE), BAR,
  I("Sticky Note", 0, ID_TOOL0 + TL_NOTE), I("Highlight Text", 0, ID_TOOL0 + TL_HIGHLIGHT), I("Underline Text", 0, ID_TOOL0 + TL_UNDERLINE),
  I("Strikethrough", 0, ID_TOOL0 + TL_STRIKE), I("Squiggly Underline", 0, ID_TOOL0 + TL_SQUIGGLY), I("Add Text Box", 0, ID_TOOL0 + TL_TEXTBOX), I("Draw", 0, ID_TOOL0 + TL_INK),
  I("Rectangle", 0, ID_TOOL0 + TL_RECT), I("Oval", 0, ID_TOOL0 + TL_OVAL), I("Line", 0, ID_TOOL0 + TL_LINE), I("Arrow", 0, ID_TOOL0 + TL_ARROW),
  I("Cloud", 0, ID_TOOL0 + TL_CLOUD), I("Stamp...", 0, ID_STAMPPICK),
  H("Measure"), S("Distance", 0, ID_TOOL0 + TL_MEASURE), S("Perimeter", 0, ID_TOOL0 + TL_PERIM), S("Area", 0, ID_TOOL0 + TL_AREA),
  S("Drawing Scale...", 0, ID_SCALE), S("Measurements...", 0, ID_MEASINFO), S("Export Measurements...", 0, ID_MEASEXPORT),
  I("Flatten Comments", 0, ID_FLATTENCOMMENTS), BAR,
  H("Fill & Sign"), S("Add Text", 0, ID_TOOL0 + TL_FTEXT), S("Checkmark", 0, ID_TOOL0 + TL_CHECK), S("Cross", 0, ID_TOOL0 + TL_CROSS),
  S("Dot", 0, ID_TOOL0 + TL_DOT), S("Line", 0, ID_TOOL0 + TL_LINE), S("Date", 0, ID_TOOL0 + TL_DATE), S("Signature", 0, ID_TOOL0 + TL_SIGN),
  S("Initials", 0, ID_TOOL0 + TL_INITIALS), S("Change Signature...", 0, ID_SIGCHANGE), S("Change Initials...", 0, ID_INITCHANGE), BAR,
  I("Comment Colour...", 0, ID_COLOR),
  M("Pages"),
  I("Rotate Anticlockwise", 0, ID_ROTL), I("Rotate Clockwise", 0, ID_ROTR), BAR,
  I("Delete Pages...", 0, ID_DELPAGES), I("Insert Blank Page...", 0, ID_INSBLANK), I("Insert Pages from File...", 0, ID_INSFILE), I("Replace Pages...", 0, ID_REPLACE),
  I("Extract Pages...", 0, ID_EXTRACT), I("Duplicate Page...", 0, ID_DUPLICATE), I("Reverse Pages", 0, ID_REVERSE), I("Split Document...", 0, ID_SPLIT), BAR,
  I("Move Page Earlier", 0, ID_MOVEUP), I("Move Page Later", 0, ID_MOVEDOWN), BAR,
  I("Crop Pages...", 0, ID_CROP), I("Crop Page to a Box", 0, ID_TOOL0 + TL_CROP), I("Set Page Boxes...", 0, ID_BOXES), I("Page Labels...", 0, ID_LABELS),
  M("Document"),
  I("Add Text", 0, ID_TOOL0 + TL_ADDTEXT), I("Add Picture", 0, ID_TOOL0 + TL_ADDIMAGE), I("Add Link", 0, ID_TOOL0 + TL_LINK), BAR,
  H("Header & Footer"), S("Add or Update...", 0, ID_HEADER), S("Remove", 0, ID_HEADERDEL),
  H("Watermark"), S("Add or Update...", 0, ID_WATERMARK), S("Remove", 0, ID_WATERMARKDEL),
  H("Background"), S("Add or Update...", 0, ID_BACKGROUND), S("Remove", 0, ID_BACKGROUNDDEL),
  H("Bates Numbering"), S("Add or Update...", 0, ID_BATES), S("Remove", 0, ID_BATESDEL), BAR,
  I("Attach File...", 0, ID_ATTACH), I("Attachments...", 0, ID_ATTACHMENTS), BAR,
  H("Bookmarks"), S("Add Bookmark...", 0, ID_BMADD), S("Rename Bookmark...", 0, ID_BMRENAME), S("Delete Bookmark...", 0, ID_BMDELETE), S("New Bookmarks from Headings", 0, ID_BMSTYLES), BAR,
  H("Accessibility"), S("Check...", 0, ID_A11YCHECK), S("Reading Options...", 0, ID_READOPTS), S("Automatically Tag", 0, ID_AUTOTAG),
  S("Find Untagged Content", 0, ID_UNTAGGED), S("Alternate Text...", 0, ID_ALTTEXT), S("Tags...", 0, ID_TAGTREE),
  I("Signatures...", 0, ID_SIGNATURES), BAR,
  H("Print Production"), S("Fix Hairlines...", 0, ID_HAIRLINES), S("Convert Colours to Grey", 0, ID_TOGRAY), S("Add Printer Marks...", 0, ID_MARKS), S("Set Page Boxes...", 0, ID_BOXES),
  H("Optimize"), S("Reduce File Size...", 0, ID_REDUCE), S("Advanced Optimization...", 0, ID_ADVOPT), S("Audit Space Usage...", 0, ID_AUDIT),
  M("Forms"),
  I("Field List...", 0, ID_FIELDLIST), BAR,
  H("Add Field"), S("Text Field", 0, ID_FIELDTOOL + 0), S("Text Field Table", 0, ID_FIELDTOOL + 8), S("Check Box", 0, ID_FIELDTOOL + 1),
  S("Radio Button", 0, ID_FIELDTOOL + 2), S("Drop-down List", 0, ID_FIELDTOOL + 4), S("List Box", 0, ID_FIELDTOOL + 5), S("Button", 0, ID_FIELDTOOL + 3),
  S("Picture Field", 0, ID_FIELDTOOL + 9), S("Date Field", 0, ID_FIELDTOOL + 7), S("Signature Field", 0, ID_FIELDTOOL + 6),
  I("Detect Form Fields", 0, ID_DETECT), I("Field Properties...", 0, ID_FIELDPROPS), I("Tab Order by Rows", 0, ID_TABROWS), I("Tab Order by Columns", 0, ID_TABCOLS),
  I("Calculation Order...", 0, ID_CALCORDER), BAR,
  I("Clear Form", 0, ID_CLEARFORM), I("Flatten Form", 0, ID_FLATTENFORM), I("Import Data...", 0, ID_IMPORTDATA), I("Export Data...", 0, ID_EXPORTDATA),
  M("Protect"),
  I("Protect with Password...", 0, ID_SAVEPW), I("Security Properties...", 0, ID_SECPROPS), I("Remove Security", 0, ID_REMSEC), BAR,
  I("Redact Area", 0, ID_TOOL0 + TL_REDACT), I("Find Text and Redact...", 0, ID_REDACTTEXT), I("Redact Page", 0, ID_REDACTPAGE),
  I("Redaction Properties...", 0, ID_REDACTPROPS), I("Apply Redactions", 0, ID_REDACTAPPLY), BAR,
  I("Remove Hidden Information...", 0, ID_HIDDEN), I("Sanitize Document...", 0, ID_SANITIZE),
  M("Window"),
  I("Next Document", "`", ID_NEXTDOC), I("Previous Document", 0, ID_PREVDOC),
  { NM_END, 0, 0, 0, 0, 0 }
};

/* the menus' words in the language chosen: their English kept the first time, each label then
   set to its translation (the menus are made once, when the window is) */
#define NMENUS (int)(sizeof menus / sizeof menus[0])
static void menus_translate(void)
{
  static const char *en[NMENUS];
  static int kept;
  int i;
  for (i = 0; i < NMENUS; i++) {
    if (!kept) en[i] = (const char *)menus[i].nm_Label;
    if (menus[i].nm_Type != NM_END && en[i] && menus[i].nm_Label != NM_BARLABEL &&
        (menus[i].nm_UserData < (APTR)(ID_LANG0 + 1) || menus[i].nm_UserData > (APTR)(ID_LANG0 + 5)))
      menus[i].nm_Label = (STRPTR)tr(en[i]);
  }
  kept = 1;
}

/* ------------------------------------------------------------------ the toolbars */
static const StripItem bar1[] = {
  { SI_ICON, ID_OPEN, IC_F_OPEN, "Open", "Open a PDF file." },
  { SI_ICON, ID_SAVE, IC_F_SAVE, "Save", "Save the changes to the file." },
  { SI_ICON, ID_FIND, IC_F_FIND, "Find Text", "Search the document for words." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_PREV, IC_F_PAGEUP, "Previous Page", NULL },
  { SI_ICON, ID_NEXT, IC_F_PAGEDOWN, "Next Page", NULL },
  { SI_END, 0, 0, NULL, NULL }
};
static const StripItem bar2[] = {
  { SI_TEXT, ID_PAGELABEL, 0, NULL, "Go to Page" },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_TOOL0 + TL_SELECT, IC_F_SELECT, "Select Text", "Select text to copy it, and choose comments to edit or delete them." },
  { SI_ICON, ID_TOOL0 + TL_HAND, IC_F_HAND, "Hand", "Drag the pages to scroll them." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_ZOOMOUT, IC_F_ZOOMOUT, "Zoom Out", NULL },
  { SI_TEXT, ID_ZOOMMENU, 0, "Zoom", "Choose a zoom from a list." },
  { SI_ICON, ID_ZOOMIN, IC_T_ZOOM, "Zoom In", NULL },
  { SI_ICON, ID_FITWIDTH, IC_PAGEWIDTH, "Fit Width", "The pages as wide as the window." },
  { SI_ICON, ID_FITPAGE, IC_ONEPAGE, "Fit Page", "A whole page in the window." },
  { SI_GAP, 0, 0, NULL, NULL },
  { SI_ICON, ID_TOOL0 + TL_NOTE, IC_F_NOTE, "Sticky Note", "Click on the page to add a comment there." },
  { SI_ICON, ID_TOOL0 + TL_HIGHLIGHT, IC_F_HIGHLIGHT, "Highlight Text", "Drag across text to highlight it." },
  { SI_ICON, ID_TOOL0 + TL_INK, IC_F_INK, "Draw", "Draw freehand on the page." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_TOOLSPANE, IC_F_THUMBS, "Tools Pane", "Show or hide the tools on the right." },
  { SI_END, 0, 0, NULL, NULL }
};
static const StripItem combar_items[] = {
  { SI_ICON, ID_TOOL0 + TL_SELECT, IC_F_SELECT, "Select Text", "Select text, and choose comments to edit or delete them." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_TOOL0 + TL_NOTE, IC_F_NOTE, "Sticky Note", "Click on the page to add a comment there." },
  { SI_ICON, ID_TOOL0 + TL_HIGHLIGHT, IC_F_HIGHLIGHT, "Highlight Text", "Drag across text to highlight it." },
  { SI_ICON, ID_TOOL0 + TL_UNDERLINE, IC_F_UNDERLINE, "Underline Text", "Drag across text to underline it." },
  { SI_ICON, ID_TOOL0 + TL_STRIKE, IC_F_STRIKE, "Strikethrough", "Drag across text to strike it out." },
  { SI_ICON, ID_TOOL0 + TL_TEXTBOX, IC_F_TEXTBOX, "Add Text Box", "Click or drag a box on the page, then type the text." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_TOOL0 + TL_INK, IC_F_INK, "Draw", "Draw freehand on the page." },
  { SI_ICON, ID_TOOL0 + TL_RECT, IC_F_RECT, "Rectangle", "Drag to draw a rectangle." },
  { SI_ICON, ID_TOOL0 + TL_OVAL, IC_F_OVAL, "Oval", "Drag to draw an oval." },
  { SI_ICON, ID_TOOL0 + TL_LINE, IC_F_LINE, "Line", "Drag to draw a line." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_COLOR, ID_COLOR, 0, "Colour", "The colour of the tool chosen." },
  { SI_TEXT, ID_WIDTH, 0, "Line Width", "The width of drawn lines and shapes." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_DELETE, IC_F_DELETE, "Delete Comment", "Delete the comment chosen." },
  { SI_ICON, ID_UNDO, IC_F_UNDO, "Undo", NULL },
  { SI_GAP, 0, 0, NULL, NULL },
  { SI_LABEL, ID_DONE, IC_F_CLOSE, "Close", "Close the comment tools." },
  { SI_END, 0, 0, NULL, NULL }
};
static const StripItem orgbar_items[] = {
  { SI_ICON, ID_ROTL, IC_F_ROTL, "Rotate Anticlockwise", "Turn the pages picked." },
  { SI_ICON, ID_ROTR, IC_F_ROTR, "Rotate Clockwise", "Turn the pages picked." },
  { SI_ICON, ID_DELPAGES, IC_F_DELETE, "Delete Pages", "Delete the pages picked." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_INSBLANK, IC_F_BLANK, "Insert Blank Page", "An empty page after the one picked." },
  { SI_LABEL, ID_INSFILE, IC_F_INSERT, "Insert", "Insert the pages of another PDF after the one picked." },
  { SI_LABEL, ID_EXTRACT, IC_F_EXTRACT, "Extract", "Save the pages picked as a new PDF." },
  { SI_SEP, 0, 0, NULL, NULL },
  { SI_ICON, ID_MOVEUP, IC_ARROWLEFT, "Move Earlier", NULL },
  { SI_ICON, ID_MOVEDOWN, IC_ARROWRIGHT, "Move Later", NULL },
  { SI_ICON, ID_UNDO, IC_F_UNDO, "Undo", NULL },
  { SI_GAP, 0, 0, NULL, NULL },
  { SI_LABEL, ID_DONE, IC_F_CLOSE, "Close", "Back to reading." },
  { SI_END, 0, 0, NULL, NULL }
};
static const StripItem nav_items[] = {
  { SI_ICON, ID_THUMBS, IC_F_THUMBS, "Page Thumbnails", NULL },
  { SI_ICON, ID_BOOKMARKS, IC_BOOKMARK, "Bookmarks", NULL },
  { SI_GAP, 0, 0, NULL, NULL },
  { SI_END, 0, 0, NULL, NULL }
};
static const StripItem find_items[] = {
  { SI_ICON, ID_FINDPREV, IC_F_PAGEUP, "Find Previous", NULL },
  { SI_ICON, ID_FINDNEXT, IC_F_PAGEDOWN, "Find Next", NULL },
  { SI_ICON, ID_FINDCLOSE, IC_F_CLOSE, "Close", NULL },
  { SI_END, 0, 0, NULL, NULL }
};
static const double zoomvals[] = { 0.25, 0.5, 0.75, 1, 1.25, 1.5, 2, 3, 4 };
static const char *const zoomlabels[] = { "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "Fit Width", "Fit Page" };
static const double widths[] = { 0.5, 1, 2, 3, 4, 6, 8 };
static const char *const widthlabels[] = { "0.5 pt", "1 pt", "2 pt", "3 pt", "4 pt", "6 pt", "8 pt" };

/* ------------------------------------------------------------------ small helpers */
DocView *cur_view(void) { return page_mode == PM_DOC && curdoc >= 0 && curdoc < ndocs ? &docs[curdoc] : NULL; }
DocView *cur_doc_view(void) { return curdoc >= 0 && curdoc < ndocs ? &docs[curdoc] : NULL; }
static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
/* a requester: the message (fmt translated, with arg for its one %s) broken into lines of at most
   WRAP characters at spaces, so that no language's sentence runs wider than a 640-pixel screen */
#define WRAP 66
int ask(const char *gadgets, const char *fmt, const char *arg)
{
  static char m[2400];
  int i, start = 0, sp = -1;
  snprintf(m, sizeof m, tr(fmt), arg ? arg : "");
  for (i = 0; m[i]; i++) {
    if (m[i] == '\n') { start = i + 1; sp = -1; continue; }
    if (m[i] == ' ') sp = i;
    if (i - start >= WRAP && sp > start) { m[sp] = '\n'; start = sp + 1; sp = -1; }
  }
  return (int)MUI_Request(app, win, 0, (STRPTR)"Folio", (STRPTR)tr(gadgets), (STRPTR)"%s", (ULONG)m);
}
static void menu_set(ULONG id, ULONG attr, ULONG val)
{
  Object *strip = win ? (Object *)xget(win, MUIA_Window_Menustrip) : NULL, *it;
  if (strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, id))) set(it, attr, val);
}
int file_request(int save, const char *titletext, char *path, size_t cap, const char *pattern)
{
  struct FileRequester *fr;
  char dir[400], file[108];
  int ok = 0;
  strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  strncpy(file, path[0] ? (const char *)FilePart((STRPTR)path) : "", sizeof file - 1); file[sizeof file - 1] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(titletext),
        ASLFR_DoSaveMode, save, ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file,
        (ULONG)(pattern ? ASLFR_InitialPattern : TAG_IGNORE), (ULONG)pattern, (ULONG)(pattern ? ASLFR_DoPatterns : TAG_IGNORE), TRUE, TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)xget(win, MUIA_Window_Window), TAG_DONE)) {
    strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
    ok = AddPart((STRPTR)path, fr->fr_File, cap) && fr->fr_File[0];
  }
  MUI_FreeAslRequest(fr);
  return ok;
}
u8 *read_file(const char *path, size_t *len)
{
  FILE *f = fopen(path, "rb");
  long n;
  u8 *buf;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n <= 0 || !(buf = (u8 *)malloc((size_t)n))) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
  fclose(f);
  *len = (size_t)n;
  return buf;
}
int write_file(const char *path, const u8 *data, size_t len)
{
  FILE *f = fopen(path, "wb");
  int ok;
  if (!f) return 0;
  ok = fwrite(data, 1, len, f) == len;
  if (fclose(f)) ok = 0;
  return ok;
}

/* ------------------------------------------------------------------ modal dialogs */
ULONG run_modal(Object *w, const ULONG *ends, void (*each)(ULONG id))
{
  ULONG sigs = 0, id = 0;
  int i, done = 0;
  if (!w) return ID_DLGCANCEL;
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
  set(win, MUIA_Window_Sleep, TRUE);
  set(w, MUIA_Window_Open, TRUE);
  while (!done) {
    id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
    if (id == ID_DLGCANCEL || id == MUIV_Application_ReturnID_Quit) { id = ID_DLGCANCEL; break; }
    for (i = 0; ends[i]; i++) if (id == ends[i]) done = 1;
    if (done) break;
    if (id && each) each(id);
    if (sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
  }
  set(w, MUIA_Window_Open, FALSE);
  set(win, MUIA_Window_Sleep, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)w);
  return id;
}
Object *dialog_window(const char *t, Object *contents, const char *oklabel)
{
  Object *ok, *cancel, *w;
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(t), MUIA_Window_RefWindow, (ULONG)win, MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
    MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
    WindowContents, VGroup, QDLG,
      Child, contents,
      Child, HGroup, Child, HSpace(0), Child, ok = QButton(tr(oklabel ? oklabel : "OK")), Child, cancel = QButton(tr("Cancel")), End,
    End,
  End;
  if (!w) { if (contents) MUI_DisposeObject(contents); return NULL; }
  DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
  return w;
}
static int ask_field(const char *t, const char *label, char *buf, int cap, const char *width)
{
  Object *str, *w;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  int ok;
  w = dialog_window(t, HGroup, Child, QLabel(tr(label), MUIO_Label_SingleFrame),
                    Child, str = StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_MaxLen, cap, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)width, End, End, NULL);
  if (!w) return 0;
  DoMethod(str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)str);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) { strncpy(buf, (const char *)xget(str, MUIA_String_Contents), cap - 1); buf[cap - 1] = 0; }
  MUI_DisposeObject(w);
  return ok;
}
int ask_string(const char *t, const char *label, char *buf, int cap) { return ask_field(t, label, buf, cap, "MMMMMMMMMMMMMMMMMMMM"); }
/* a longer text: "\n" typed as a backslash and n starts a new line */
int ask_text(const char *t, const char *label, char *buf, int cap)
{
  char *p;
  if (!ask_field(t, label, buf, cap, "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")) return 0;
  while ((p = strstr(buf, "\\n"))) { *p = '\n'; memmove(p + 1, p + 2, strlen(p + 2) + 1); }
  return 1;
}
LONG pick_color(const char *t, ULONG start)
{
  Object *ca, *hex, *w;
  ULONG rgb[3];
  char buf[16];
  LONG c = -1;
  rgb[0] = ((start >> 16) & 255) * 0x01010101UL; rgb[1] = ((start >> 8) & 255) * 0x01010101UL; rgb[2] = (start & 255) * 0x01010101UL;
  sprintf(buf, "%06lX", (unsigned long)(start & 0xFFFFFF));
  w = dialog_window(t, VGroup,
        Child, ca = ColoradjustObject, MUIA_Coloradjust_RGB, (ULONG)rgb, End,
        Child, HGroup, Child, QLabel(tr("Hex #"), MUIO_Label_SingleFrame),
          Child, hex = StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEF", MUIA_CycleChain, 1, End,
          Child, HSpace(0), End,
      End, NULL);
  if (!w) return -1;
  DoMethod(hex, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
  for (;;) {
    static const ULONG ends2[] = { ID_DLGOK, ID_DLGCHANGE, 0 };
    ULONG id = run_modal(w, ends2, NULL);
    if (id == ID_DLGCHANGE) {
      ULONG v = strtoul((const char *)xget(hex, MUIA_String_Contents), NULL, 16);
      rgb[0] = ((v >> 16) & 255) * 0x01010101UL; rgb[1] = ((v >> 8) & 255) * 0x01010101UL; rgb[2] = (v & 255) * 0x01010101UL;
      set(ca, MUIA_Coloradjust_RGB, (ULONG)rgb);
      continue;
    }
    if (id == ID_DLGOK) {
      ULONG *p = (ULONG *)xget(ca, MUIA_Coloradjust_RGB);
      if (p) c = (LONG)(((p[0] >> 24) << 16) | ((p[1] >> 24) << 8) | (p[2] >> 24));
    }
    break;
  }
  MUI_DisposeObject(w);
  return c;
}

/* ------------------------------------------------------------------ the strips' states */
static int needs_doc(ULONG id)
{
  return !(id == ID_OPEN || id == ID_HOME || id == ID_TOOLS || id == ID_ABOUT || id == ID_QUIT || id == ID_DARKUI || id == ID_AUTHOR ||
           id == ID_TOOLSPANE || id == ID_COMMENTMODE || id == ID_ORGANIZE || id == ID_COMBINE || id == ID_EXPORTTOOL || id == ID_FIND ||
           id == ID_EXTRACT || id == ID_PROPS || id == ID_INSBLANK || (id >= ID_TOOL0 && id < ID_TOOL0 + TL_N) || id == ID_COLOR || id == ID_WIDTH);
}
ULONG strip_state(ULONG id)
{
  DocView *v = cur_view(), *dv = cur_doc_view();
  ULONG s = 0;
  if (!dv && (id == ID_EXTRACT || id == ID_PROPS || id == ID_INSBLANK)) return SS_DISABLED;
  if (!v && needs_doc(id)) return SS_DISABLED;
  if (id >= ID_TOOL0 && id < ID_TOOL0 + TL_N) return (int)(id - ID_TOOL0) == tool ? SS_CHECKED : 0;
  switch (id) {
  case ID_SAVE: if (!v->pdf->changed) s = SS_DISABLED; break;
  case ID_PREV: case ID_FIRST: if (v->page <= 0 || v->mode != VM_READ) s = SS_DISABLED; break;
  case ID_NEXT: case ID_LAST: if (v->page + 1 >= v->pdf->npages || v->mode != VM_READ) s = SS_DISABLED; break;
  case ID_FITWIDTH: if (v->fit == FIT_WIDTH) s = SS_CHECKED; break;
  case ID_FITPAGE: if (v->fit == FIT_PAGE) s = SS_CHECKED; break;
  case ID_ZOOMIN: case ID_ZOOMOUT: case ID_ZOOMMENU: if (v->mode != VM_READ) s = SS_DISABLED; break;
  case ID_THUMBS: if (show_nav && nav_mode == 0) s = SS_CHECKED; break;
  case ID_BOOKMARKS: if (show_nav && nav_mode == 1) s = SS_CHECKED; break;
  case ID_TOOLSPANE: if (show_tools) s = SS_CHECKED; break;
  case ID_UNDO: if (!v->nundo) s = SS_DISABLED; break;
  case ID_DELETE: case ID_EDITNOTE: if (v->an_page < 0) s = SS_DISABLED; break;
  case ID_COPY: if (v->sel_page < 0 || v->sel_a < 0) s = SS_DISABLED; break;
  case ID_MOVEUP: if (v->page <= 0) s = SS_DISABLED; break;
  case ID_MOVEDOWN: if (v->page + 1 >= v->pdf->npages) s = SS_DISABLED; break;
  case ID_COMMENTMODE: if (comment_mode) s = SS_CHECKED; break;
  case ID_ORGANIZE: if (v && v->mode == VM_ORGANIZE) s = SS_CHECKED; break;
  case ID_FINDNEXT: case ID_FINDPREV: if (!findtext[0]) s = SS_DISABLED; break;
  }
  return s;
}
const char *strip_text(ULONG id)
{
  static char buf[32];
  DocView *v = cur_view();
  buf[0] = 0;
  switch (id) {
  case ID_PAGELABEL: if (v) sprintf(buf, "/ %d", v->pdf->npages); else strcpy(buf, "/ -"); break;
  case ID_ZOOMMENU: if (v) sprintf(buf, "%ld%%", (long)(v->zoom * 100 + 0.5)); else strcpy(buf, "100%"); break;
  case ID_WIDTH: sprintf(buf, opt.width < 1 ? "%.1f pt" : "%.0f pt", opt.width); break;
  }
  return buf;
}
static int colour_tool(void) { return tool == TL_SELECT || tool == TL_HAND ? TL_INK : tool; }
ULONG strip_color(ULONG id) { return id == ID_COLOR ? opt.color[colour_tool()] : 0; }

/* ------------------------------------------------------------------ keeping everything up to date */
static void set_title(void)
{
  static char t[200];
  DocView *v = cur_view();
  if (v) snprintf(t, sizeof t, "Folio - %s%s", v->name, v->pdf->changed ? " *" : "");
  else strcpy(t, "Folio");
  if (win) set(win, MUIA_Window_Title, (ULONG)t);
}
static void update_menus(void)
{
  static const ULONG ids[] = { ID_CLOSE, ID_CLOSEALL, ID_SAVE, ID_SAVEAS, ID_EXPTEXT, ID_EXPPAGE, ID_EXTRACT, ID_PROPS, ID_UNDO, ID_COPY, ID_SELALL,
    ID_DESELECT, ID_FIND, ID_FINDNEXT, ID_FINDPREV, ID_EDITNOTE, ID_DELETE, ID_ZOOMIN, ID_ZOOMOUT, ID_ZOOM100, ID_FITWIDTH, ID_FITPAGE, ID_ROTVIEWR,
    ID_ROTVIEWL, ID_FIRST, ID_PREV, ID_NEXT, ID_LAST, ID_GOTO, ID_COMMENTMODE, ID_ORGANIZE, ID_COMBINE, ID_ROTL, ID_ROTR, ID_DELPAGES, ID_INSBLANK,
    ID_INSFILE, ID_MOVEUP, ID_MOVEDOWN, 0 };
  static char undo[80];
  DocView *v = cur_view();
  int i;
  if (!win) return;
  for (i = 0; ids[i]; i++) menu_set(ids[i], MUIA_Menuitem_Enabled, v && !(strip_state(ids[i]) & SS_DISABLED));
  menu_set(ID_SAVE, MUIA_Menuitem_Enabled, v != NULL);
  snprintf(undo, sizeof undo, "%s %s", tr("Undo"), v && v->nundo ? tr(undo_name(v)) : "");
  menu_set(ID_UNDO, MUIA_Menuitem_Title, (ULONG)undo);
  menu_set(ID_THUMBS, MUIA_Menuitem_Checked, show_nav && nav_mode == 0);
  menu_set(ID_BOOKMARKS, MUIA_Menuitem_Checked, show_nav && nav_mode == 1);
  menu_set(ID_TOOLSPANE, MUIA_Menuitem_Checked, show_tools);
  menu_set(ID_DARKUI, MUIA_Menuitem_Checked, pref_dark);
  for (i = 0; i < 6; i++) menu_set(ID_LANG0 + i, MUIA_Menuitem_Checked, pref_lang + 1 == i);
}
static void page_field(void)
{
  DocView *v = cur_view();
  char buf[12];
  if (v) sprintf(buf, "%d", v->page + 1); else buf[0] = 0;
  if (pagestr) SetAttrs(pagestr, MUIA_NoNotify, TRUE, MUIA_String_Contents, (ULONG)buf, TAG_DONE);
}
void refresh_bars(void)
{
  DoMethod(mainbar, MUIM_Strip_Refresh);
  DoMethod(mainbar2, MUIM_Strip_Refresh);
  DoMethod(combar, MUIM_Strip_Refresh);
  DoMethod(orgbar, MUIM_Strip_Refresh);
  DoMethod(navstrip, MUIM_Strip_Refresh);
  DoMethod(findstrip, MUIM_Strip_Refresh);
  DoMethod(tabs, MUIM_Tabs_Refresh);
  DoMethod(statusbar, MUIM_Status_Refresh);
  page_field();
  set_title();
  update_menus();
}
void doc_changed(void)
{
  DoMethod(view, MUIM_View_Redraw);
  doc_changed_panes();
}
/* (see folio.h) the panes, bars and menus after a change the view has shown already */
void doc_changed_panes(void)
{
  DoMethod(toolpane, MUIM_Pane_Refresh);
  DoMethod(navpane, MUIM_Nav_Refresh);
  refresh_bars();
}
/* which bars and panes show, after a mode or document change */
static void show_parts(void)
{
  DocView *v = cur_view();
  int org = v && v->mode == VM_ORGANIZE;
  if (!v) comment_mode = 0;
  set(combar, MUIA_ShowMe, v && comment_mode && !org);
  set(orgbar, MUIA_ShowMe, org);
  set(navgroup, MUIA_ShowMe, show_nav);
  set(toolpane, MUIA_ShowMe, show_tools);
  set(navpane, MUIA_Nav_Mode, nav_mode);
  set(pages, MUIA_Group_ActivePage, page_mode == PM_HOME ? 0 : page_mode == PM_TOOLS ? 1 : 2);
}
static void refresh_all(void)
{
  show_parts();
  DoMethod(view, MUIM_View_Refresh);
  DoMethod(toolpane, MUIM_Pane_Refresh);
  DoMethod(navpane, MUIM_Nav_Refresh);
  DoMethod(home, MUIM_Home_Refresh);
  refresh_bars();
}
void set_tool(int t)
{
  if (t < 0 || t >= TL_N) return;
  tool = t;
  DoMethod(view, MUIM_View_Tool);
  refresh_bars();
}

/* ------------------------------------------------------------------ documents */
static void add_recent(const char *path)
{
  int i;
  for (i = 0; i < nrecent; i++) if (!Stricmp((STRPTR)recent[i], (STRPTR)path)) break;
  if (i == nrecent && nrecent < MAXRECENT) nrecent++;
  if (i >= MAXRECENT) i = MAXRECENT - 1;
  memmove(recent[1], recent[0], (size_t)i * sizeof recent[0]);
  strncpy(recent[0], path, sizeof recent[0] - 1); recent[0][sizeof recent[0] - 1] = 0;
}
PDoc *open_pdf(const char *path, int *encrypted)
{
  size_t len = 0;
  u8 *data;
  char err[160], msg[400];
  PDoc *d;
  int again = 0;
  *encrypted = 0;
  if (!(data = read_file(path, &len))) {
    snprintf(msg, sizeof msg, tr("%s couldn't be read."), (const char *)FilePart((STRPTR)path));
    ask("OK", "%s", msg);
    return NULL;
  }
  d = pdf_open(data, len, err, sizeof err);
  free(data);
  if (!d) {
    snprintf(msg, sizeof msg, tr("%s couldn't be opened:\n%s"), (const char *)FilePart((STRPTR)path), err[0] ? err : tr("not a PDF file"));
    ask("OK", "%s", msg);
    return NULL;
  }
  *encrypted = d->crypt != 0;
  while (d->need_password) {
    char pw[128];
    int ok;
    set(app, MUIA_Application_Sleep, FALSE);
    if (!dlg_password((const char *)FilePart((STRPTR)path), pw, sizeof pw, again)) { pdf_close(d); return NULL; }
    set(app, MUIA_Application_Sleep, TRUE);
    ok = pdf_password(d, pw);
    if (!ok) {
      /* (AES-256 passwords are UTF-8: what was typed in Latin-1 tried that way too) */
      char u[400];
      size_t n = 0, k;
      for (k = 0; pw[k] && n + 3 < sizeof u; k++) n += (size_t)utf8_put(u + n, (u8)pw[k]);
      u[n] = 0;
      if (strcmp(u, pw)) ok = pdf_password(d, u);
    }
    if (ok) { pdf_ensure_pages(d); break; }
    again = 1;
  }
  if (d->npages < 1) {
    snprintf(msg, sizeof msg, tr("%s has no pages Folio can find."), (const char *)FilePart((STRPTR)path));
    ask("OK", "%s", msg);
    pdf_close(d);
    return NULL;
  }
  return d;
}
static void doc_free(DocView *v)
{
  int i;
  DoMethod(view, MUIM_View_Forget, (ULONG)v, 0);
  for (i = 0; i < v->ntexts; i++) textpage_free(&v->texts[i].tp);
  free(v->texts);
  for (i = 0; i < v->nthumbs; i++) free(v->thumbs[i].pm.rgb);
  free(v->thumbs);
  free(v->picked);
  if (v->outline) pdf_outline_free(v->outline);
  undo_clear(v);
  if (v->rc) rcache_free(v->rc);
  if (v->pdf) pdf_close(v->pdf);
  memset(v, 0, sizeof *v);
}
static int doc_show(PDoc *d, const char *path, int encrypted);
/* (doctools.c) a document it made, shown in a tab */
void doc_show_new(PDoc *d, const char *path, int encrypted) { add_recent(path); doc_show(d, path, encrypted); }
static int doc_show(PDoc *d, const char *path, int encrypted)
{
  DocView *v;
  if (ndocs >= MAXDOCS) { pdf_close(d); ask("OK", "Folio can have %s documents open at once.\nClose one to open another.", "12"); return 0; }
  v = &docs[ndocs];
  memset(v, 0, sizeof *v);
  v->pdf = d;
  v->rc = rcache_new(d);
  v->zoom = 1; v->fit = FIT_WIDTH;
  v->sel_page = v->find_page = v->an_page = -1;
  v->was_encrypted = encrypted;
  v->picked = (u8 *)calloc((size_t)d->npages, 1);
  strncpy(v->path, path, sizeof v->path - 1);
  strncpy(v->name, (const char *)FilePart((STRPTR)path), sizeof v->name - 1);
  curdoc = ndocs++;
  page_mode = PM_DOC;
  refresh_all();
  return 1;
}
static void doc_switch(int i)
{
  if (i < 0 || i >= ndocs) return;
  curdoc = i;
  page_mode = PM_DOC;
  refresh_all();
}
static void open_path(const char *path)
{
  int i, enc;
  PDoc *d;
  for (i = 0; i < ndocs; i++) if (!Stricmp((STRPTR)docs[i].path, (STRPTR)path)) { doc_switch(i); return; }
  set(app, MUIA_Application_Sleep, TRUE);
  d = open_pdf(path, &enc);
  set(app, MUIA_Application_Sleep, FALSE);
  strncpy(lastdir, path, sizeof lastdir - 1);
  if (!d) return;
  add_recent(path);
  doc_show(d, path, enc);
}
static void open_file(void)
{
  char path[512];
  strcpy(path, lastdir);
  if (!file_request(0, "Open a PDF file", path, sizeof path, "#?.pdf")) return;
  open_path(path);
}
static int save_to(DocView *v, const char *path)
{
  u8 *data;
  size_t len;
  char msg[300];
  int ok;
  if (v->was_encrypted && v->was_encrypted != 2) {
    if (ask("Save|Cancel", "%s", tr("This document is password protected. Folio saves it without the protection:\nanyone will be able to open the copy saved.")) != 1) return 0;
    v->was_encrypted = 2;
  }
  set(app, MUIA_Application_Sleep, TRUE);
  data = pdf_save(v->pdf, NULL, 0, &len);
  ok = data && write_file(path, data, len);
  free(data);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!ok) {
    snprintf(msg, sizeof msg, tr("%s couldn't be saved (out of memory, or the disk is full or write-protected)."), (const char *)FilePart((STRPTR)path));
    ask("OK", "%s", msg);
    return 0;
  }
  v->pdf->changed = 0;
  return 1;
}
static int save_as(void);
static int save(void)
{
  DocView *v = cur_view();
  if (!v) return 0;
  if (!v->path[0]) return save_as();
  if (!save_to(v, v->path)) return 0;
  refresh_bars();
  return 1;
}
static int save_as(void)
{
  DocView *v = cur_view();
  char path[512];
  if (!v) return 0;
  strcpy(path, v->path[0] ? v->path : lastdir);
  if (!file_request(1, "Save the document as", path, sizeof path, "#?.pdf")) return 0;
  if (!strchr((const char *)FilePart((STRPTR)path), '.')) strncat(path, ".pdf", sizeof path - 1 - strlen(path));
  if (!save_to(v, path)) return 0;
  strcpy(v->path, path);
  strncpy(v->name, (const char *)FilePart((STRPTR)path), sizeof v->name - 1);
  add_recent(path);
  refresh_bars();
  return 1;
}
static int may_discard(int i)
{
  char msg[300];
  int r;
  if (!docs[i].pdf->changed) return 1;
  doc_switch(i);
  snprintf(msg, sizeof msg, tr("Save the changes to %s before closing?"), docs[i].name);
  r = ask("Save|Don't Save|Cancel", "%s", msg);
  if (r == 1) return save();
  return r == 2;
}
static int doc_close(int i)
{
  if (i < 0 || i >= ndocs) return 1;
  if (!may_discard(i)) return 0;
  doc_free(&docs[i]);
  memmove(&docs[i], &docs[i + 1], (size_t)(ndocs - i - 1) * sizeof docs[0]);
  memset(&docs[ndocs - 1], 0, sizeof docs[0]);
  ndocs--;
  /* the documents after it moved down a place: the view's pictures of them forgotten */
  { int k; for (k = i; k < ndocs; k++) DoMethod(view, MUIM_View_Forget, (ULONG)&docs[k + 1], 0); }
  if (curdoc >= ndocs) curdoc = ndocs - 1;
  else if (curdoc > i) curdoc--;
  if (curdoc < 0) page_mode = PM_HOME;
  refresh_all();
  return 1;
}
static int close_all(void)
{
  while (ndocs) if (!doc_close(ndocs - 1)) return 0;
  return 1;
}

/* ------------------------------------------------------------------ finding text */
static void set_mode(DocView *v, int mode);
static void show_find(void)
{
  set(findbar, MUIA_ShowMe, TRUE);
  set(win, MUIA_Window_ActiveObject, (ULONG)findstr);
}
static void find(int dir)
{
  DocView *v = cur_view();
  int p, start, from, a, b, tries;
  const char *s = (const char *)xget(findstr, MUIA_String_Contents);
  if (!v) return;
  if (s && s[0]) {
    char u[400];
    size_t n = 0, k;
    for (k = 0; s[k] && n + 3 < sizeof u; k++) n += (size_t)utf8_put(u + n, (u8)s[k]);
    u[n] = 0;
    strncpy(findtext, u, sizeof findtext - 1);
  }
  if (!findtext[0]) { show_find(); return; }
  if (v->mode == VM_ORGANIZE) set_mode(v, VM_READ);
  set(app, MUIA_Application_Sleep, TRUE);
  if (v->find_page >= 0) { p = v->find_page; from = dir > 0 ? v->find_a + 1 : v->find_a; }
  else { p = v->page; from = dir > 0 ? 0 : 1 << 30; }
  start = p;
  for (tries = 0; tries <= v->pdf->npages; tries++) {
    TextPage *t = page_text(v, p);
    int ok = t && (dir > 0 ? textpage_find(t, findtext, from, &a, &b) : textpage_find_back(t, findtext, from, &a, &b));
    if (ok) {
      int old = v->find_page;
      v->find_page = p; v->find_a = a; v->find_b = b;
      set(app, MUIA_Application_Sleep, FALSE);
      if (old >= 0 && old != p) DoMethod(view, MUIM_View_Redraw);
      if (p != v->page) DoMethod(view, MUIM_View_GoPage, p, -1);
      DoMethod(view, MUIM_View_ShowMatch);
      refresh_bars();
      return;
    }
    p = dir > 0 ? (p + 1) % v->pdf->npages : (p + v->pdf->npages - 1) % v->pdf->npages;
    from = dir > 0 ? 0 : 1 << 30;
    if (p == start && tries > 0) break;
  }
  set(app, MUIA_Application_Sleep, FALSE);
  v->find_page = -1;
  DoMethod(view, MUIM_View_Redraw);
  {
    char lat[200];
    to_latin1(findtext, lat, sizeof lat);
    ask("OK", tr("\"%s\" wasn't found in the document."), lat);
  }
}

/* ------------------------------------------------------------------ commands */
static void copy_selection(void)
{
  DocView *v = cur_view();
  TextPage *t;
  char *u, *lat;
  size_t n;
  if (!v || v->sel_page < 0 || v->sel_a < 0 || !(t = page_text(v, v->sel_page))) return;
  u = textpage_text(t, v->sel_a, v->sel_b);
  n = strlen(u);
  if ((lat = (char *)malloc(n * 3 + 4))) {
    size_t m = utf8_to_latin1(u, n, lat, n * 3 + 4);
    clip_write(lat, (long)m);
    free(lat);
  }
  free(u);
}
static void popup_at_pointer(Object *owner, const char *const *labels, int n, int cur, ULONG attr)
{
  struct Window *w = (struct Window *)xget(win, MUIA_Window_Window);
  static const char *tl[16];
  int i;
  if (!w) return;
  for (i = 0; i < n && i < 16; i++) tl[i] = tr(labels[i]);
  popup_list(owner, w->LeftEdge + w->MouseX - 30, w->TopEdge + w->MouseY + 10, 80, tl, n, cur, attr);
}
/* a tool from the Tools page, the pane or the Home page: a document opened first if there is none */
static int want_doc(void)
{
  if (!cur_doc_view()) open_file();
  if (!cur_doc_view()) return 0;
  if (page_mode != PM_DOC) { page_mode = PM_DOC; refresh_all(); }
  return cur_view() != NULL;
}
static void set_mode(DocView *v, int mode)
{
  int p = v->page;
  v->mode = mode;
  if (mode == VM_ORGANIZE) {
    comment_mode = 0;
    if (v->picked) { memset(v->picked, 0, (size_t)v->pdf->npages); v->picked[v->page] = 1; }
  }
  refresh_all();
  DoMethod(view, MUIM_View_GoPage, p, -1);
}
static void command(ULONG id)
{
  DocView *v = cur_view();
  if (id >= ID_RECENT0 && id < ID_RECENT0 + MAXRECENT) { int k = (int)(id - ID_RECENT0); if (k < nrecent) { char p[512]; strcpy(p, recent[k]); open_path(p); } return; }
  if (id >= ID_TOOL0 && id < ID_TOOL0 + TL_N) {
    int t = (int)(id - ID_TOOL0);
    if (!want_doc()) return;
    v = cur_view();
    if (v->mode == VM_ORGANIZE) set_mode(v, VM_READ);
    /* the comment tools open the comment bar, as PDFCraft does */
    if (((t >= TL_NOTE && t <= TL_LASTCOMMENT) || t >= TL_FTEXT) && !comment_mode) { comment_mode = 1; refresh_all(); }
    if (t == TL_STAMP && !opt.stamp[0]) doc_command(ID_STAMPPICK);
    set_tool(t);
    return;
  }
  if (doc_command(id)) return;
  switch (id) {
  case ID_OPEN: open_file(); return;
  case ID_HOME: page_mode = PM_HOME; refresh_all(); return;
  case ID_TOOLS: page_mode = PM_TOOLS; refresh_all(); return;
  case ID_ABOUT:
    { static char m[1200];
      snprintf(m, sizeof m, "\33c%s\n\n%s", "Folio 0.1.2", tr("A PDF viewer and editor for the Amiga, laid out as PDFCraft is.\n\nReads PDF 1.0 to 2.0 with its own renderer and fonts; finds, copies\nand edits text; comments, forms, signatures, page tools, redaction,\nexport, OCR, accessibility and print production.\n\nWritten in C for MUI; drawn with its own icons."));
      ask("OK", "%s", m); }
    return;
  case ID_DARKUI:
    pref_dark = !pref_dark;
    set(win, MUIA_Window_Open, FALSE);
    theme_set_dark(pref_dark);
    set(win, MUIA_Window_Open, TRUE);
    refresh_all();
    return;
  case ID_AUTHOR: {
    char buf[64];
    strcpy(buf, opt.author);
    if (ask_string("Comment Author", "Your name, written into comments:", buf, sizeof buf)) strcpy(opt.author, buf);
    return; }
  case ID_LANG0: case ID_LANG0 + 1: case ID_LANG0 + 2: case ID_LANG0 + 3: case ID_LANG0 + 4: case ID_LANG0 + 5:
    pref_lang = (int)(id - ID_LANG0) - 1;
    lang_set(pref_lang >= 0 ? pref_lang : lang_system());
    refresh_all();
    ask("OK", "%s", tr("The menus change to the new language the next time Folio starts."));
    return;
  case ID_NEXTDOC: if (ndocs > 1) doc_switch((curdoc + 1) % ndocs); else if (ndocs) doc_switch(0); return;
  case ID_PREVDOC: if (ndocs > 1) doc_switch((curdoc + ndocs - 1) % ndocs); else if (ndocs) doc_switch(0); return;
  case ID_TOOLSPANE: show_tools = !show_tools; show_parts(); refresh_bars(); return;
  case ID_THUMBS: case ID_BOOKMARKS: {
    int m = id == ID_THUMBS ? 0 : 1;
    if (show_nav && nav_mode == m) show_nav = 0; else { show_nav = 1; nav_mode = m; }
    show_parts();
    DoMethod(navpane, MUIM_Nav_Refresh);
    refresh_bars();
    return; }
  case ID_COLOR: {
    int t = colour_tool();
    LONG c = pick_color(tool_names[t], opt.color[t]);
    if (c >= 0) opt.color[t] = (u32)c;
    refresh_bars();
    return; }
  case ID_WIDTH: {
    int i, cur = 2;
    for (i = 0; i < 7; i++) if (fabs(widths[i] - opt.width) < 0.01) cur = i;
    popup_at_pointer(combar, widthlabels, 7, cur, MUIA_Popup_Chosen);
    DoMethod(combar, MUIM_Notify, MUIA_Popup_Chosen, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_WIDTHCHOSEN);
    return; }
  case ID_COMMENTMODE:
    if (!want_doc()) return;
    v = cur_view();
    if (v->mode == VM_ORGANIZE) v->mode = VM_READ;
    comment_mode = !comment_mode;
    if (!comment_mode && tool >= TL_NOTE) tool = TL_SELECT;
    refresh_all();
    return;
  case ID_ORGANIZE:
    if (!want_doc()) return;
    v = cur_view();
    set_mode(v, v->mode == VM_ORGANIZE ? VM_READ : VM_ORGANIZE);
    return;
  case ID_DONE:
    if (v && v->mode == VM_ORGANIZE) { set_mode(v, VM_READ); return; }
    comment_mode = 0;
    if (tool >= TL_NOTE) tool = TL_SELECT;
    refresh_all();
    return;
  case ID_COMBINE: if (!want_doc()) return; v = cur_view(); edit_insert_file(v, v->pdf->npages); return;
  case ID_EXPORTTOOL: {
    int r;
    if (!want_doc()) return;
    r = ask("Page as PNG...|Text...|Cancel", "%s", tr("Export the page in view as a picture, or all the document's text?"));
    if (r == 1) dlg_export_page(); else if (r == 2) dlg_export_text();
    return; }
  case ID_EXTRACT: if (!want_doc()) return; dlg_extract(); return;
  case ID_PROPS: if (!want_doc()) return; dlg_properties(); refresh_bars(); return;
  case ID_INSBLANK: if (!want_doc()) return; v = cur_view(); edit_insert_blank(v, v->page + 1); return;
  case ID_FIND: if (!want_doc()) return; show_find(); return;
  case ID_QUIT: return;
  }
  if (!v) return;
  switch (id) {
  case ID_CLOSE: doc_close(curdoc); return;
  case ID_CLOSEALL: close_all(); return;
  case ID_SAVE: save(); return;
  case ID_SAVEAS: save_as(); return;
  case ID_EXPPAGE: dlg_export_page(); return;
  case ID_EXPTEXT: dlg_export_text(); return;
  case ID_UNDO: undo_last(v); return;
  case ID_COPY: copy_selection(); return;
  case ID_SELALL: {
    TextPage *t = page_text(v, v->page);
    if (t && t->no) { v->sel_page = v->page; v->sel_a = 0; v->sel_b = t->no - 1; DoMethod(view, MUIM_View_Redraw); }
    refresh_bars();
    return; }
  case ID_DESELECT: v->sel_page = -1; v->an_page = -1; DoMethod(view, MUIM_View_Redraw); refresh_bars(); return;
  case ID_FINDNEXT: find(1); return;
  case ID_FINDPREV: find(-1); return;
  case ID_FINDSTR: find(1); return;
  case ID_FINDCLOSE: set(findbar, MUIA_ShowMe, FALSE); v->find_page = -1; DoMethod(view, MUIM_View_Redraw); return;
  case ID_EDITNOTE: edit_annot_text(v); return;
  case ID_DELETE: edit_delete_annot(v); return;
  case ID_ZOOMIN: DoMethod(view, MUIM_View_Zoom, 0, 1); refresh_bars(); return;
  case ID_ZOOMOUT: DoMethod(view, MUIM_View_Zoom, 0, -1); refresh_bars(); return;
  case ID_ZOOM100: DoMethod(view, MUIM_View_Zoom, 1000, 0); refresh_bars(); return;
  case ID_FITWIDTH: DoMethod(view, MUIM_View_Zoom, 0, 2); refresh_bars(); return;
  case ID_FITPAGE: DoMethod(view, MUIM_View_Zoom, 0, 3); refresh_bars(); return;
  case ID_ZOOMMENU: {
    int i, cur = -1;
    for (i = 0; i < 9; i++) if (fabs(zoomvals[i] - v->zoom) < 0.005) cur = i;
    if (v->fit == FIT_WIDTH) cur = 9;
    if (v->fit == FIT_PAGE) cur = 10;
    popup_at_pointer(mainbar2, zoomlabels, 11, cur, MUIA_Popup_Chosen);
    return; }
  case ID_ZOOMCHOSEN: {
    int k = (int)xget(mainbar2, MUIA_Popup_Chosen);
    if (k < 9) DoMethod(view, MUIM_View_Zoom, (LONG)(zoomvals[k] * 1000 + 0.5), 0);
    else DoMethod(view, MUIM_View_Zoom, 0, k == 9 ? 2 : 3);
    refresh_bars();
    return; }
  case ID_WIDTHCHOSEN: { int k = (int)xget(combar, MUIA_Popup_Chosen); if (k >= 0 && k < 7) opt.width = widths[k]; refresh_bars(); return; }
  case ID_ROTVIEWR: case ID_ROTVIEWL:
    v->rot = (v->rot + (id == ID_ROTVIEWR ? 1 : 3)) % 4;
    DoMethod(view, MUIM_View_Refresh);
    refresh_bars();
    return;
  case ID_FIRST: DoMethod(view, MUIM_View_GoPage, 0, -1); return;
  case ID_PREV: if (v->page > 0) DoMethod(view, MUIM_View_GoPage, v->page - 1, -1); return;
  case ID_NEXT: if (v->page + 1 < v->pdf->npages) DoMethod(view, MUIM_View_GoPage, v->page + 1, -1); return;
  case ID_LAST: DoMethod(view, MUIM_View_GoPage, v->pdf->npages - 1, -1); return;
  case ID_GOTO: case ID_PAGELABEL: dlg_goto(); return;
  case ID_PAGEFIELD: {
    int p = atoi((const char *)xget(pagestr, MUIA_String_Contents));
    if (p < 1) p = 1;
    if (p > v->pdf->npages) p = v->pdf->npages;
    set(win, MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_None);
    DoMethod(view, MUIM_View_GoPage, p - 1, -1);
    refresh_bars();
    return; }
  case ID_ROTL: edit_rotate(v, -1); return;
  case ID_ROTR: edit_rotate(v, 1); return;
  case ID_DELPAGES: edit_delete_pages(v); return;
  case ID_INSFILE: edit_insert_file(v, v->page + 1); return;
  case ID_MOVEUP: if (v->page > 0) edit_move(v, v->page, v->page - 1); return;
  case ID_MOVEDOWN: if (v->page + 1 < v->pdf->npages) edit_move(v, v->page, v->page + 1); return;
  case ID_COMMENTPICK:
    v->an_page = pane_page; v->an_index = pane_index;
    DoMethod(view, MUIM_View_GoPage, pane_page, -1);
    DoMethod(toolpane, MUIM_Pane_Refresh);
    refresh_bars();
    return;
  }
}

/* ------------------------------------------------------------------ the window */
static Object *flat(int horiz) { return NewObject(flatscroll_class()->mcc_Class, NULL, MUIA_FlatScroll_Horiz, horiz, TAG_DONE); }
static Object *strip(const StripItem *items, int vertical)
{
  return NewObject(strip_class()->mcc_Class, NULL, MUIA_Strip_Items, (ULONG)items, MUIA_Strip_Vertical, vertical, MUIA_ShortHelp, (ULONG)" ", TAG_DONE);
}
static void pick_notify(Object *o, ULONG attr)
{
  DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, MUIV_TriggerValue);
}
static int win_make(void)
{
  Object *strip_menu;
  menus_translate();
  strip_menu = MUI_MakeObject(MUIO_MenustripNM, (ULONG)menus, 0);
  struct Screen *scr = LockPubScreen((STRPTR)(pub_screen[0] ? pub_screen : NULL));
  LONG sw = 800, sh = 580, top = 12;
  if (scr) { sw = scr->Width; top = scr->BarHeight + 1; sh = scr->Height - top; UnlockPubScreen(NULL, scr); }
  win = QWindowObject,
    MUIA_Window_Title, (ULONG)"Folio",
    MUIA_Window_ID, MAKE_ID('F','O','L','I'),
    MUIA_Window_DisableKeys, MUIKEYF_WINDOW_CLOSE,
    MUIA_Window_Menustrip, (ULONG)strip_menu,
    MUIA_Window_Width, sw,
    MUIA_Window_Height, sh,
    MUIA_Window_LeftEdge, 0,
    MUIA_Window_TopEdge, top,
    WindowContents, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
      Child, tabs = NewObject(tabs_class()->mcc_Class, NULL, TAG_DONE),
      Child, pages = VGroup, MUIA_Group_PageMode, TRUE, InnerSpacing(0, 0),
        Child, home = NewObject(home_class()->mcc_Class, NULL, TAG_DONE),
        Child, grid = NewObject(grid_class()->mcc_Class, NULL, TAG_DONE),
        Child, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
          Child, HGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, (ULONG)(theme_dialog_back() ? MUIA_Background : TAG_IGNORE), (ULONG)theme_dialog_back(),
            Child, mainbar = strip(bar1, 0),
            Child, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, MUIA_HorizWeight, 0,
              Child, VSpace(0),
              Child, pagestr = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 6, MUIA_FixWidthTxt, (ULONG)"0000", MUIA_String_Format, MUIV_String_Format_Right, MUIA_ShortHelp, (ULONG)tr("Page number"), End,
              Child, VSpace(0),
            End,
            Child, mainbar2 = strip(bar2, 0),
          End,
          Child, combar = strip(combar_items, 0),
          Child, orgbar = strip(orgbar_items, 0),
          Child, findbar = HGroup, MUIA_ShowMe, FALSE, InnerSpacing(4, 2), (ULONG)(theme_dialog_back() ? MUIA_Background : TAG_IGNORE), (ULONG)theme_dialog_back(),
            Child, QLabel(tr("Find:"), MUIO_Label_SingleFrame),
            Child, findstr = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
            Child, findstrip = NewObject(strip_class()->mcc_Class, NULL, MUIA_Strip_Items, (ULONG)find_items, MUIA_HorizWeight, 0, MUIA_FixWidth, 80, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
          End,
          Child, HGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
            Child, navstrip = strip(nav_items, 1),
            Child, navgroup = HGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, MUIA_HorizWeight, 0,
              Child, navpane = NewObject(nav_class()->mcc_Class, NULL, TAG_DONE),
              Child, navscroll = flat(0),
            End,
            Child, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
              Child, HGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
                Child, view = NewObject(view_class()->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE),
                Child, vscroll = flat(0),
              End,
              Child, hscroll = flat(1),
            End,
            Child, toolpane = NewObject(pane_class()->mcc_Class, NULL, MUIA_HorizWeight, 0, TAG_DONE),
          End,
        End,
      End,
      Child, statusbar = NewObject(status_class()->mcc_Class, NULL, TAG_DONE),
    End,
  End;
  if (!win) { if (strip_menu) MUI_DisposeObject(strip_menu); return 0; }
  DoMethod(app, OM_ADDMEMBER, (ULONG)win);
  SetAttrs(view, MUIA_View_VProp, (ULONG)vscroll, MUIA_View_HProp, (ULONG)hscroll, TAG_DONE);
  set(navpane, MUIA_Nav_VProp, (ULONG)navscroll);
  set(combar, MUIA_ShowMe, FALSE);
  set(orgbar, MUIA_ShowMe, FALSE);
  DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_QUIT);
  DoMethod(vscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)view, 3, MUIM_Set, MUIA_View_Top, MUIV_TriggerValue);
  DoMethod(hscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)view, 3, MUIM_Set, MUIA_View_Left, MUIV_TriggerValue);
  DoMethod(navscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)navpane, 3, MUIM_Set, MUIA_Nav_Top, MUIV_TriggerValue);
  DoMethod(view, MUIM_Notify, MUIA_View_Page, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_PAGECHG);
  DoMethod(view, MUIM_Notify, MUIA_View_Zoomed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_ZOOMED);
  DoMethod(view, MUIM_Notify, MUIA_View_Picked, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_ANPICK);
  DoMethod(view, MUIM_Notify, MUIA_View_Link, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LINK);
  DoMethod(view, MUIM_Notify, MUIA_View_OrgPick, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_ORGPICK);
  DoMethod(view, MUIM_Notify, MUIA_View_EditNote, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_EDITNOTE);
  DoMethod(tabs, MUIM_Notify, MUIA_Tabs_Pick, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABPICK);
  DoMethod(tabs, MUIM_Notify, MUIA_Tabs_Close, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABCLOSE);
  DoMethod(navpane, MUIM_Notify, MUIA_Nav_Go, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_NAVGO);
  pick_notify(mainbar, MUIA_Strip_Pick);
  pick_notify(mainbar2, MUIA_Strip_Pick);
  pick_notify(combar, MUIA_Strip_Pick);
  pick_notify(orgbar, MUIA_Strip_Pick);
  pick_notify(navstrip, MUIA_Strip_Pick);
  pick_notify(findstrip, MUIA_Strip_Pick);
  pick_notify(toolpane, MUIA_Pane_Pick);
  pick_notify(home, MUIA_Home_Pick);
  pick_notify(grid, MUIA_Grid_Pick);
  DoMethod(mainbar2, MUIM_Notify, MUIA_Popup_Chosen, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_ZOOMCHOSEN);
  DoMethod(pagestr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_PAGEFIELD);
  DoMethod(findstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FINDSTR);
  show_parts();
  set(win, MUIA_Window_Open, TRUE);
  if (!xget(win, MUIA_Window_Open)) return 0;
  {
    struct Window *w = (struct Window *)xget(win, MUIA_Window_Window);
    if (w) LOG("screen %ld x %ld, window %ld,%ld %ld x %ld", (long)w->WScreen->Width, (long)w->WScreen->Height, (long)w->LeftEdge, (long)w->TopEdge, (long)w->Width, (long)w->Height);
  }
  return 1;
}

/* ------------------------------------------------------------------ starting up */
static BPTR home_lock, home_old;
static void wb_tooltypes(char *screen, int size)
{
  struct Library *IconBase;
  struct WBArg *wa = &_WBenchMsg->sm_ArgList[0];
  struct DiskObject *dob;
  BPTR old;
  STRPTR v;
  if (!(IconBase = OpenLibrary((STRPTR)"icon.library", 37))) return;
  old = CurrentDir(wa->wa_Lock);
  if ((dob = GetDiskObject((STRPTR)wa->wa_Name))) {
    if ((v = FindToolType((STRPTR *)dob->do_ToolTypes, (STRPTR)"PUBSCREEN")) && !screen[0]) { strncpy(screen, (char *)v, size - 1); screen[size - 1] = 0; }
    FreeDiskObject(dob);
  }
  CurrentDir(old);
  CloseLibrary(IconBase);
}
static int home_has_fonts(BPTR dir)
{
  BPTR old = CurrentDir(dir), l = Lock((STRPTR)"Fonts/QuillSans.font", ACCESS_READ);
  CurrentDir(old);
  if (l) UnLock(l);
  return l != 0;
}
/* PROGDIR: made right if a shell started Folio without it (it holds Fonts and TrueType) */
static void find_home(const char *argv0)
{
  char name[256];
  const char *tries[2];
  int i;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR pw = me->pr_WindowPtr;
  BPTR l;
  me->pr_WindowPtr = (APTR)-1;
  l = GetProgramDir() ? Lock((STRPTR)"PROGDIR:Fonts/QuillSans.font", ACCESS_READ) : 0;
  if (l) { UnLock(l); me->pr_WindowPtr = pw; return; }
  tries[0] = argv0;
  tries[1] = GetProgramName((STRPTR)name, sizeof name) ? name : NULL;
  for (i = 0; i < 2 && !home_lock; i++) {
    BPTR f, d;
    if (!tries[i] || !tries[i][0] || !(f = Lock((STRPTR)tries[i], ACCESS_READ))) continue;
    d = ParentDir(f);
    UnLock(f);
    if (d && home_has_fonts(d)) home_lock = d; else if (d) UnLock(d);
  }
  if (home_lock) home_old = SetProgramDir(home_lock);
  me->pr_WindowPtr = pw;
}
static void free_home(void)
{
  if (!home_lock) return;
  SetProgramDir(home_old);
  UnLock(home_lock);
  home_lock = 0;
}
static void add_fonts(void)
{
  BPTR l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ), sys;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR pw = me->pr_WindowPtr;
  if (!l) return;
  me->pr_WindowPtr = (APTR)-1;
  if ((sys = Lock((STRPTR)"FONTS:", ACCESS_READ))) {
    LONG same = SameLock(l, sys);
    UnLock(sys);
    if (same == LOCK_SAME) { UnLock(l); me->pr_WindowPtr = pw; return; }
  }
  me->pr_WindowPtr = pw;
  if (AssignAdd((STRPTR)"FONTS", l)) fonts_assigned = 1; else UnLock(l);
}
static void remove_fonts(void)
{
  BPTR l;
  if (!fonts_assigned) return;
  if ((l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ))) { RemAssignList((STRPTR)"FONTS", l); UnLock(l); }
  fonts_assigned = 0;
}
static void opts_defaults(void)
{
  opt.color[TL_NOTE] = 0xFFD700; opt.color[TL_HIGHLIGHT] = 0xFFE500; opt.color[TL_UNDERLINE] = 0x1E6FD9; opt.color[TL_STRIKE] = 0xE02020;
  opt.color[TL_TEXTBOX] = 0xD01818; opt.color[TL_INK] = 0xE02020; opt.color[TL_RECT] = 0xE02020; opt.color[TL_OVAL] = 0xE02020;
  opt.color[TL_LINE] = 0xE02020; opt.color[TL_SELECT] = opt.color[TL_HAND] = 0xE02020;
  opt.width = 2; opt.text_size = 12;
  strcpy(opt.author, "Amiga User");
  opt.color[TL_SQUIGGLY] = 0x20A020; opt.color[TL_ARROW] = 0xE02020; opt.color[TL_CLOUD] = 0x2050E0; opt.color[TL_STAMP] = 0x108020;
  opt.color[TL_MEASURE] = opt.color[TL_AREA] = opt.color[TL_PERIM] = 0xC020C0; opt.color[TL_ADDTEXT] = 0x000000;
  opt.color[TL_FTEXT] = opt.color[TL_CHECK] = opt.color[TL_CROSS] = opt.color[TL_DOT] = opt.color[TL_DATE] = 0x000000;
  opt.color[TL_SIGN] = opt.color[TL_INITIALS] = 0x102080;
  opt.scale = 72; strcpy(opt.unit, "in");
}
static void load_prefs(void)
{
  FILE *f = fopen("PROGDIR:Folio.prefs", "r");
  char line[600];
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "dark=", 5)) pref_dark = atoi(line + 5);
    else if (!strncmp(line, "language=", 9)) pref_lang = lang_by_code(line + 9);
    else if (!strncmp(line, "screen=", 7)) strncpy(pub_screen, line + 7, sizeof pub_screen - 1);
    else if (!strncmp(line, "dir=", 4)) strncpy(lastdir, line + 4, sizeof lastdir - 1);
    else if (!strncmp(line, "author=", 7)) strncpy(opt.author, line + 7, sizeof opt.author - 1);
    else if (!strncmp(line, "nav=", 4)) show_nav = atoi(line + 4);
    else if (!strncmp(line, "navmode=", 8)) nav_mode = atoi(line + 8) ? 1 : 0;
    else if (!strncmp(line, "tools=", 6)) show_tools = atoi(line + 6);
    else if (!strncmp(line, "width=", 6)) { double w = atof(line + 6); if (w > 0.1 && w < 20) opt.width = w; }
    else if (!strncmp(line, "signature=", 10)) strncpy(opt.signature, line + 10, sizeof opt.signature - 1);
    else if (!strncmp(line, "initials=", 9)) strncpy(opt.initials, line + 9, sizeof opt.initials - 1);
    else if (!strncmp(line, "scale=", 6)) { double sc = atof(line + 6); if (sc > 0) opt.scale = sc; }
    else if (!strncmp(line, "unit=", 5)) strncpy(opt.unit, line + 5, sizeof opt.unit - 1);
    else if (!strncmp(line, "recent=", 7) && nrecent < MAXRECENT) { strncpy(recent[nrecent], line + 7, sizeof recent[0] - 1); nrecent++; }
    else if (!strncmp(line, "color", 5) && line[5] >= '0' && line[5] <= '9') {
      int t = atoi(line + 5);
      char *eq = strchr(line, '=');
      if (eq && t >= 0 && t < TL_N) opt.color[t] = (u32)strtoul(eq + 1, NULL, 16);
    }
  }
  fclose(f);
}
static void save_prefs(void)
{
  FILE *f = fopen("PROGDIR:Folio.prefs", "w");
  int i;
  if (!f) return;
  fprintf(f, "dark=%d\nnav=%d\nnavmode=%d\ntools=%d\nwidth=%.1f\n", pref_dark, show_nav, nav_mode, show_tools, opt.width);
  fprintf(f, "author=%s\n", opt.author);
  if (pref_lang >= 0) fprintf(f, "language=%s\n", lang_code(pref_lang));
  if (pub_screen[0]) fprintf(f, "screen=%s\n", pub_screen);
  if (lastdir[0]) fprintf(f, "dir=%s\n", lastdir);
  for (i = 0; i < TL_N; i++) fprintf(f, "color%d=%06lX\n", i, (unsigned long)opt.color[i]);
  if (opt.signature[0]) fprintf(f, "signature=%s\n", opt.signature);
  if (opt.initials[0]) fprintf(f, "initials=%s\n", opt.initials);
  fprintf(f, "scale=%g\nunit=%s\n", opt.scale, opt.unit);
  for (i = 0; i < nrecent; i++) fprintf(f, "recent=%s\n", recent[i]);
  fclose(f);
}

int main(int argc, char **argv)
{
  ULONG sigs = 0;
  int rc = 20, i;
  char start[8][512];
  int nstart = 0;
  char arg_screen[64] = "";
  if (!(IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39)) ||
      !(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39)) ||
      !(LayersBase = OpenLibrary((STRPTR)"layers.library", 39)) ||
      !(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39)) ||
      !(KeymapBase = OpenLibrary((STRPTR)"keymap.library", 37))) goto done;
  if (!(MUIMasterBase = OpenLibrary((STRPTR)MUIMASTER_NAME, 19))) { printf("Folio needs MUI (muimaster.library 19 or later).\n"); goto done; }
  CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
  find_home(argc > 0 ? argv[0] : NULL);
  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "DEBUG=", 6)) qlog = fopen(argv[i] + 6, "w");
    else if (!Strnicmp((STRPTR)argv[i], (STRPTR)"PUBSCREEN=", 10)) strncpy(arg_screen, argv[i] + 10, sizeof arg_screen - 1);
    else if (nstart < 8) { strncpy(start[nstart], argv[i], 511); start[nstart++][511] = 0; }
  }
  if (argc == 0 && _WBenchMsg) {
    for (i = 1; i < _WBenchMsg->sm_NumArgs && nstart < 8; i++) {
      struct WBArg *wa = &_WBenchMsg->sm_ArgList[i];
      if (NameFromLock(wa->wa_Lock, (STRPTR)start[nstart], 512) && AddPart((STRPTR)start[nstart], wa->wa_Name, 512)) nstart++;
    }
    wb_tooltypes(arg_screen, sizeof arg_screen);
  }
  opts_defaults();
  load_prefs();
  if (arg_screen[0]) strcpy(pub_screen, arg_screen);
  if (pub_screen[0]) {
    struct Screen *sc = LockPubScreen((STRPTR)pub_screen);
    if (sc) UnlockPubScreen(NULL, sc); else pub_screen[0] = 0;
  }
  LOG("Folio starting, cybergraphics %p", (void *)CyberGfxBase);
  lang_set(pref_lang >= 0 ? pref_lang : lang_system());
  theme_set_dark(pref_dark);
  splash_open(pub_screen, "Folio 0.1.2");
  splash_step(5, tr("Adding Folio's fonts drawer"));
  DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39);
  add_fonts();
  font_set_dir("PROGDIR:TrueType");
  splash_step(40, tr("Preparing"));
  if (!view_class() || !strip_class() || !tabs_class() || !status_class() || !flatscroll_class() || !nav_class() || !pane_class() ||
      !home_class() || !grid_class() || !qbutton_class()) { printf("Folio: MUI couldn't make its classes\n"); goto done; }
  splash_step(60, tr("Building the window"));
  app = ApplicationObject,
    MUIA_Application_Title, (ULONG)"Folio",
    MUIA_Application_Version, (ULONG)verstag,
    MUIA_Application_Description, (ULONG)"A PDF viewer and editor",
    MUIA_Application_Base, (ULONG)"FOLIO",
  End;
  if (!app) { printf("Folio: the application could not be made (MUI classes missing?)\n"); goto done; }
  splash_step(75, tr("Opening the window"));
  if (!win_make()) { printf("Folio: the window could not be made\n"); goto done; }
  for (i = 0; i < nstart; i++) {
    static char what[300];
    snprintf(what, sizeof what, tr("Opening %s"), (const char *)FilePart((STRPTR)start[i]));
    splash_front();
    splash_step(85, what);
    open_path(start[i]);
  }
  splash_close();
  refresh_all();
  if (!theme_fonts_ok()) ask("OK", "%s", tr("Folio couldn't open its own screen fonts, so text is drawn in the\nsystem font. Keep the Fonts drawer next to Folio."));
  for (;;) {
    ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
    if (id) LOG("id %lu", (unsigned long)id);
    if (id == MUIV_Application_ReturnID_Quit || id == ID_QUIT) { if (close_all()) break; }
    else if (id == ID_PAGECHG) { DoMethod(navpane, MUIM_Nav_Refresh); refresh_bars(); }
    else if (id == ID_ZOOMED) refresh_bars();
    else if (id == ID_ANPICK || id == ID_ORGPICK) { if (comment_mode) DoMethod(toolpane, MUIM_Pane_Refresh); refresh_bars(); }
    else if (id == ID_TABPICK) {
      int k = (int)(LONG)xget(tabs, MUIA_Tabs_Pick);
      if (k == -2) { page_mode = PM_HOME; refresh_all(); }
      else if (k == -1) { page_mode = PM_TOOLS; refresh_all(); }
      else doc_switch(k);
    }
    else if (id == ID_TABCLOSE) doc_close((int)(LONG)xget(tabs, MUIA_Tabs_Close));
    else if (id == ID_NAVGO) DoMethod(view, MUIM_View_GoPage, nav_page, nav_top < 0 ? -1 : (LONG)nav_top);
    else if (id == ID_HOMEPICK || id == ID_GRIDPICK) ;
    else if (id == ID_LINK) {
      char msg[600];
      snprintf(msg, sizeof msg, "%s\n\n%s", tr("This link goes to a web page:"), view_uri);
      if (ask("Copy Address|OK", "%s", msg) == 1) clip_write(view_uri, (long)strlen(view_uri));
    }
    else if (id) command(id);
    if (sigs) {
      sigs = Wait(sigs | SIGBREAKF_CTRL_C);
      if (sigs & SIGBREAKF_CTRL_C) break;
    }
  }
  save_prefs();
  rc = 0;
done:
  for (i = 0; i < ndocs; i++) doc_free(&docs[i]);
  ndocs = 0; curdoc = -1;
  if (app) MUI_DisposeObject(app);
  pens_free();
  view_class_free();
  bars_free();
  panes_free();
  popup_free();
  qbutton_class_free();
  font_free_substitutes();
  theme_free_fonts();
  remove_fonts();
  free_home();
  splash_close();
  if (qlog) { fclose(qlog); qlog = NULL; }
  if (CyberGfxBase) CloseLibrary(CyberGfxBase);
  if (DiskfontBase) CloseLibrary(DiskfontBase);
  if (MUIMasterBase) CloseLibrary(MUIMasterBase);
  if (KeymapBase) CloseLibrary(KeymapBase);
  if (UtilityBase) CloseLibrary(UtilityBase);
  if (LayersBase) CloseLibrary(LayersBase);
  if (GfxBase) CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
  return rc;
}
