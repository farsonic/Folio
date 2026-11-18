# Folio 0.1.2

[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-farsonic-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/farsonic)

Folio is a PDF viewer and editor for the Amiga, laid out the way PDFCraft is. Tabs for Home,
Tools and each open document run along the top. Under them is the toolbar: open, save, find,
the page number, the select and hand tools, the zoom and quick comment tools. Page thumbnails
or bookmarks go on the left, the tools (or the comments) on the right, and the pages in the
middle, one under another. The menus are in the screen's title bar as usual. Folio is written
in C for MUI and draws its own interface and icons. None of the icons are copied from any other
program.

## Since 0.1.2 (not released yet)

- Damaged or hostile PDFs can no longer crash Folio, hang it or make it draw differently on the
  Amiga than on other machines. The fixes come from fuzzing Folio's PDF engine inside
  pdf.datatype, and from Folio's own fuzzer: a file whose table of contents points to the wrong
  places, or ends in the middle of a word; a pattern named where there is nothing to look it up
  in; dashed lines with a negative dash or dashes far finer than the line (these are drawn solid
  now); numbers too big for the Amiga's 32-bit integers; mesh shadings whose data runs out; JPEG
  pictures with impossible Huffman tables; coordinates and sizes far outside the page; names lost
  when memory runs out; saving a document with a very long number in it.
- The native tests gained `tests/robusttest.c` (one small PDF per bug, each of which crashed or
  hung before its fix; also built with a 32-bit `long` as on the Amiga), `tests/allocfail.c`
  (running out of memory at every point while opening and drawing a page) and a fuzzer,
  `tools/fuzz.c` (`FUZZ=1 sh tools/test.sh`, or `sh tools/fuzz.sh SECONDS`).
- PDFCraft's other tools, in the new Document, Forms and Protect menus, more items in File, Tools
  and Pages, and as cards on the Tools page (each opens a list of its tools). `docs/pdfcraft-gap.md`
  lists every PDFCraft tool with what Folio has.
  - **Forms**: click a field with the Select tool to fill it in (text, check boxes, radio buttons,
    drop-down and list boxes). Folio draws each value into the field's appearance, so every reader
    shows it. Forms › Field List, Clear Form, Flatten Form, Import and Export Data (FDF or XFDF).
    Preparing a form: add text fields (and tables of them), check boxes, radio buttons, drop-down
    and list boxes, buttons, date, picture and signature fields by dragging a box; Detect Form
    Fields turns drawn blanks (`____`, `[ ]`) into fields; Field Properties, tab order, calculation
    order. Scripts in forms aren't run.
  - **Fill & Sign**: type text anywhere, tick, cross, dot, line, today's date, your signature and
    initials (typed, kept in Folio.prefs).
  - **Page content**: add text and pictures (JPEG, PNG) to a page; headers and footers (page
    number, page count, page label, date), Bates numbers, watermarks (text or picture, any angle,
    see-through) and backgrounds, each updated by adding it again and removable; links to a page
    or a web address; attached files (add, list, save, delete).
  - **Pages**: replace, split (every so many pages, at bookmarks, or into so many files), duplicate,
    reverse, crop (by margins, or to a box drawn), page boxes, page labels.
  - **Comments**: squiggly underline, arrow, cloud, stamps (twelve kinds or your own text),
    measuring distance, perimeter and area at a drawing scale (listed and exported as CSV), and
    Flatten Comments.
  - **Bookmarks**: add, rename, delete, and new bookmarks made from the headings (text set larger
    than the body text).
  - **Protect**: save a copy with a password (AES-256 or AES-128) and permissions; security
    properties; redaction (areas, pages or every match of some text are marked, then Apply
    Redactions removes the text and pixels under them for good and paints them over); Remove Hidden
    Information and Sanitize (metadata, scripts, attachments, comments, form fields, hidden layers,
    invisible text, thumbnails, private data, links).
  - **Smaller files**: Reduce File Size and Advanced Optimization (pictures at a lower resolution,
    identical data stored once, everything compressed, objects packed) and Audit Space Usage.
  - **Create and export**: a new blank document, a PDF from pictures, a text file or the
    clipboard; export as a web page, RTF, Word (.docx), PowerPoint (a picture of each page per
    slide), or all the pictures a document holds.
  - **Review**: Compare Files (the changes listed and highlighted); accessibility (check, reading
    options, automatic tagging, untagged content, alternate text, tags); signatures checked (has
    the signed part changed?); print production (fix hairlines, convert to grey, printer's marks);
    Save as PDF/A (fonts that aren't embedded are reported); Word Count; Revert to Saved.
  - Changes to the whole document (headers, redaction, optimizing...) are undone by Undo too: Folio
    keeps a copy of the document from before the last three of them.
  - Not tried on an Amiga yet: the native tests cover the core, and the Amiga build compiles and
    links, but the new windows and tools still need checking on a real or emulated Amiga.

## New in 0.1.2

- Drawing comments is smooth. An ink stroke, rectangle, oval, line or highlight shows up finished
  as soon as you let go. Before, the page went white and was drawn again from the start (seconds
  on a busy page) before the comment appeared.
- Dragging across text to select or highlight it only changes the letters that come into or go out
  of the selection, instead of drawing the whole page again at every move of the mouse.
- Deleting or undoing a comment leaves the page on screen while it is drawn again, rather than
  showing a white page meanwhile.

## New in 0.1.1

- Smoother scrolling. The pages are drawn off screen and copied to the window in one go, so the
  view no longer flashes the grey background before the pages when it redraws. When you scroll,
  the blitter (the Amiga's own, or the graphics card's) moves what is already on screen, and only
  the strip that comes into view is drawn.
- While Folio has nothing else to do, it renders the page after the one you are reading (and the
  one before), so paging down shows the next page straight away instead of a white page.
- A page interrupted while it was being rendered (because you scrolled or clicked) no longer
  flickers: what had been drawn stays on screen until a later try gets further.

## What it needs

- A 68040 or 68060 with its FPU. Folio is built for the 68040.
- MUI 3.8 or later (muimaster.library 19), or AROS or MorphOS, which have it built in.
- Memory: the document itself, plus up to about 7 MB of rendered pages, which are kept so
  scrolling back is instant. 16 MB of fast memory or more is comfortable.
- Screen: a graphics card screen (15 to 32 bits, through cybergraphics.library) shows pages
  exactly. On an AGA or ECS screen with 256 colours or fewer they are dithered.
- Size: Folio fits a 640 × 480 screen. Hide the thumbnails or the tools pane for more room.

## Installing

Open the Folio drawer and double-click **Install-Folio** (Commodore's Installer 43, or AROS's).
It copies Folio into a drawer of your choosing, or updates or removes an installed copy. You can
also just drag the Folio drawer wherever you like. Folio finds its Fonts and TrueType drawers
next to itself.

## The window

- **Tabs** (top): **Home** has an Open File button and the files opened lately. **Tools**
  shows every tool as a card. Each open document has a tab of its own, with a cross to close it.
  A `*` means it has changes that aren't saved yet.
- **Toolbar**: Open, Save, Find, previous and next page, the page number (type one and press
  Return to go there), then Select Text and Hand. Then come Zoom Out, the zoom (click it for a
  list), Zoom In, Fit Width and Fit Page. On the right: Sticky Note, Highlight and Draw, and a
  button that shows or hides the tools pane.
- **Comment bar** (while commenting): Select, Sticky Note, Highlight, Underline, Strikethrough,
  Add Text Box, Draw, Rectangle, Oval and Line. Then the colour of the tool chosen, the line
  width, Delete Comment, Undo, and Close.
- **Organize bar** (while organizing pages): Rotate Anticlockwise and Clockwise, Delete, Insert
  Blank Page, Insert (pages from another PDF), Extract, Move Earlier and Later, Undo, and Close.
- **Left**: the Page Thumbnails and Bookmarks buttons. Each opens its pane. Click a thumbnail or
  a bookmark to go there, and click a bookmark's triangle to open or close it.
- **Middle**: the pages. A page not drawn yet shows white for a moment. Folio draws the visible
  pages while you aren't doing anything, and stops a slow page for a click or a key, so it never
  holds you up.
- **Right**: the tools pane lists the tools. While commenting it lists every comment in the
  document instead: who made it, when, and what it says. Click one to go to it.
- **Status bar**: the page, its size, the zoom, and the tool in use or the link under the
  pointer.

Hover over any button for a short description.

## Reading

- Scroll with the scroll bars, the cursor keys (Shift for a screenful), Space, or the mouse
  wheel, or drag the pages with the Hand tool (or the middle button). The toolbar's arrows (and View >
  Previous Page and Next Page) go a page at a time.
- Zoom: View > Zoom In/Out, Actual Size (a point a pixel), Fit Width, Fit Page, or the list in
  the toolbar. View > Rotate View turns the pages on screen without changing the document.
- Links: click one with the Select or Hand tool. A link inside the document goes there. A web
  link shows its address, which you can copy.
- Find (Amiga-F, or the magnifier): type and press Return. Find Next (Amiga-G) and Find
  Previous move through the matches, page after page. Case doesn't matter.
- Select text by dragging across it with the Select tool (double-click picks a word), then
  Edit > Copy puts it on the clipboard. Edit > Select All selects the page's text.
- Password-protected documents ask for their password when opened (RC4 40 and 128-bit,
  AES-128 and AES-256).

## Commenting

Choose **Comment** in the tools pane (or Tools > Comment) to open the comment bar, then:

- **Sticky Note**: click where it goes and type the comment.
- **Highlight, Underline, Strikethrough**: drag across the text.
- **Add Text Box**: drag a box, or just click, then type the text (`\n` starts a new line).
- **Draw**: drag to draw freehand.
- **Rectangle, Oval, Line**: drag.
- **Colour** and **line width**: the swatch and the width beside it, for the tool chosen.

With the Select tool, click a comment to choose it. Delete (or the bin) deletes it, and a
double-click (or Edit > Edit Comment Text) changes its text. Edit > Comment Author sets the
name written into new comments. Every comment carries its own appearance, so other PDF readers
show it just as Folio does. Undo (Amiga-Z) takes back comments, deletions and page changes,
40 steps deep.

## Organizing pages

Choose **Organize Pages** in the tools pane (or Tools > Organize Pages) to see every page as a
thumbnail. Click a page to pick it (Shift-click picks more), then rotate, delete, extract or
insert pages after it. Drag a page to move it, and double-click one to go back to reading
there. **Combine Files** adds the pages of another PDF at the end. The Pages menu does the same
things to the page in view while reading.

## Files

- **Save** writes the document with its changes. **Save As** writes it under another name.
  Folio writes a clean PDF 1.7 that keeps everything the document
  holds: pages, fonts, pictures, bookmarks, links, forms and comments.
- A password-protected document is saved **without** its protection (Folio warns first).
- **File > Export**: the page in view as a PNG picture (72 to 300 dpi), or the whole document's
  text as a text file.
- **File > Extract Pages**: some of the pages as a new PDF (for example `2-5, 8`).
- **File > Properties**: the title, author, subject and keywords to edit, and the file, page
  size, PDF version, dates, application and security.

Folio reads PDF 1.0 to 2.0: compressed object and cross-reference streams, broken files (it
rebuilds the cross-reference table), every standard filter, Type 1, TrueType, OpenType/CFF and
Type 3 fonts (embedded or not), images (JPEG, CCITT fax and the rest), shadings and patterns,
blend modes and soft masks, optional content, and annotations' appearances. Fonts that
aren't embedded are drawn with the Liberation, Carlito and Caladea fonts in the TrueType
drawer, stretched to the document's own widths.

## Settings

Folio keeps a few settings in `PROGDIR:Folio.prefs`: the dark interface, which panes show, the
comment colours, line width and author, the files opened lately, the public screen and the last
drawer used. Start it with `PUBSCREEN=name` (or a PUBSCREEN tool type) to open on another
screen, and with file names to open them. `DEBUG=file` writes a log.

## Not included

Folio leaves out printing, ARexx and scripting, JavaScript (so forms' formats and calculations
aren't run), sound, video and 3D, and anything needing a network (cloud, AI, signing through a
server). It doesn't recognize text in scans (OCR), sign with a certificate, or edit the text and
pictures a page already has (it adds to pages and redacts). JBIG2 and JPEG 2000 pictures show as
blank.

## Source

The Source drawer holds Folio's C source. `core/` is portable C99: the PDF reader (objects,
cross-reference, filters, encryption), the fonts, the renderer, text extraction, and the writer
and editor. It has native tests (`tests/filtertest.c`, `tests/fonttest.c`, `tests/edittest.c`,
`tests/robusttest.c`, `tests/allocfail.c`, and for the tools after 0.1.2 `tests/savetest.c`,
`tests/formtest.c`, `tests/pagetest.c`, `tests/redacttest.c`, `tests/exporttest.c`,
`tests/reviewtest.c`, plus `tests/compare.py`, which compares pages with
poppler's) run under the address and undefined-behaviour sanitizers, and a fuzzer
(`tools/fuzz.c`, `tools/fuzz.sh`). `amiga/` is the MUI front end. `build.sh` runs the tests and
builds Folio with vbcc.

The fonts in the TrueType drawer are Liberation (SIL Open Font License), Carlito and Caladea
(SIL Open Font License); see `TrueType/LICENSE-fonts.txt`.

## Licence

The GNU General Public License, version 2 (GPL-2.0-only); see `LICENSE`. Parts by other people (such as fonts) keep their own licences, as noted above.
