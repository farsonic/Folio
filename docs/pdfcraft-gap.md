# Folio against PDFCraft: what is missing

PDFCraft (github.com/storytold/pdfcraft, the Rust PDF editor, MIT OR Apache-2.0) is the program Folio follows. This list was made on 2026-11-18 from PDFCraft's `main` at commit `2251a75` (2026-11-17 22:47 -0600; the artcraft-sync report folio-2026-11-18 covers the 144 pull requests merged since 2026-10-27, up to bd0a21c): its tool catalogue (crates/engine/src/catalog.rs, the "All tools" groups every PDFCraft interface is built from: 152 entries in 23 groups), its menu bar (crates/ui-egui/src/menus.rs), its side panels, and its docs (docs/gaps.md, ROADMAP.md). Folio was checked command by command in its source: every ✅ cites the id (amiga/ids.h), tool (`TL_` in amiga/folio.h) or core function that does it; a ⬜ was looked for and not found.

**Branch `pcsync`** (from main 511adc5, Folio 0.1.2) added most of what was missing; each line says what Folio had at 511adc5 where that differs ("was ⬜"). The rule that Folio leaves out forms and editing page content was lifted for this work: filling and saving AcroForm forms (no JavaScript) and the page editing PDFCraft offers are now in.

Marks: ✅ Folio has it · 🟨 partly (what is missing is said) · ⬜ missing · ➖ left out on purpose (printing; ARexx, scripting and MCP; JavaScript; sound and video; network, cloud and AI services; signing with certificates over the network), or PDFCraft hasn't built it either (its catalogue marks those Planned; they are ➖ here unless Folio has them anyway).

## Summary

- **Tool catalogue:** 152 entries in PDFCraft, 131 of them applicable to Folio. Folio has **105** (✅), **14** partly (🟨) and lacks **12** (⬜); 21 are ➖. So Folio covers 80% of the applicable entries fully and 91% at least in part.
- **Before this branch (511adc5):** ✅ 24, 🟨 7, ⬜ 97, ➖ 24 of the same 152 (128 applicable: 19% fully).
- **Menus and keys:** 51: ✅ 27, 🟨 4, ⬜ 18, ➖ 2 (before: ✅ 22, 🟨 2, ⬜ 25, ➖ 2).
- **Side panels:** 11: ✅ 4, 🟨 6, ⬜ 1, ➖ 0 (before: ✅ 4, 🟨 0, ⬜ 7, ➖ 0).
- **Beyond the tools: features:** 22: ✅ 8, 🟨 2, ⬜ 7, ➖ 5 (before: ✅ 4, 🟨 2, ⬜ 11, ➖ 5).
- **Still missing whole:** Scan & OCR (text recognition is beyond a 68040), signing with a certificate, editing the page's own text and pictures in place, the Action Wizard; and in the views: two-up and single-page layouts, split view, full screen, layers panel, redo.
- **Partly:** typed (not drawn) signatures; perimeter and area of rectangles only; simple automatic tagging; PDF/A without font embedding; signature checks without certificate validation; Convert Colours to grey only; image and date fields without their picture chooser or JavaScript format.

| Group | Entries | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|---|
| Export a PDF | 9 | 7 | 0 | 0 | 2 |
| Fill & Sign | 10 | 8 | 2 | 0 | 0 |
| Edit a PDF | 14 | 13 | 0 | 1 | 0 |
| Create a PDF | 6 | 5 | 1 | 0 | 0 |
| Combine files | 1 | 1 | 0 | 0 | 0 |
| Organize pages | 13 | 12 | 0 | 0 | 1 |
| Add comments | 15 | 15 | 0 | 0 | 0 |
| Scan & OCR | 5 | 0 | 0 | 5 | 0 |
| Protect a PDF | 5 | 4 | 0 | 0 | 1 |
| Redact a PDF | 5 | 5 | 0 | 0 | 0 |
| Compress a PDF | 3 | 3 | 0 | 0 | 0 |
| Prepare a form | 20 | 17 | 3 | 0 | 0 |
| Add a stamp | 1 | 1 | 0 | 0 | 0 |
| Use a certificate | 5 | 0 | 1 | 3 | 1 |
| Use print production | 6 | 3 | 1 | 0 | 2 |
| Measure objects | 8 | 4 | 2 | 1 | 1 |
| Compare files | 1 | 1 | 0 | 0 | 0 |
| Use guided actions | 5 | 0 | 1 | 2 | 2 |
| Prepare for accessibility | 9 | 6 | 2 | 0 | 1 |
| Apply PDF standards | 4 | 0 | 1 | 0 | 3 |
| Add search index | 1 | 0 | 0 | 0 | 1 |
| Use JavaScript | 3 | 0 | 0 | 0 | 3 |
| AI assistant | 3 | 0 | 0 | 0 | 3 |
| **All** | **152** | **105** | **14** | **12** | **21** |

| Beyond the catalogue | Entries | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|---|
| Menus and keys | 51 | 27 | 4 | 18 | 2 |
| Side panels | 11 | 4 | 6 | 1 | 0 |
| Beyond the tools: features | 22 | 8 | 2 | 7 | 5 |

## 1. The tool catalogue, group by group

In PDFCraft's catalogue order. Each line: the mark, PDFCraft's entry and command id, Folio's evidence or what is missing. Some entries appear in two groups, as in PDFCraft (Rotate, Set page boxes, Preflight, Stamp): each is counted where it appears.

### Export a PDF (9: ✅ 7 · 🟨 0 · ⬜ 0 · ➖ 2)

- ✅ Microsoft Word (.docx) `export.docx` — File › Export › Word (`ID_EXPDOCX`): core/export.c `export_text(EXP_DOCX)` — paragraphs and up to three heading levels (by text size), page breaks; text only, no pictures or tables (tests/exporttest.c, read back by python-docx) (was ⬜)
- ➖ Spreadsheet (.xlsx) `export.xlsx` — PDFCraft hasn't built it (Planned M10)
- ✅ Presentation (.pptx) `export.pptx` — File › Export › PowerPoint (`ID_EXPPPTX`): `export_pptx` — a picture of each page per slide at 96 dpi, as PDFCraft's (tests/exporttest.c, python-pptx) (was ⬜)
- ✅ Image (PNG) `export.image` — File › Export › Page as PNG Picture (`ID_EXPPAGE`, dialogs.c `dlg_export_page`, 72-300 dpi)
- ✅ Export all images `export.all_images` — File › Export › All Pictures (`ID_EXPIMAGES`): `export_images` — JPEGs as stored, the rest (grey, RGB, CMYK, indexed) as PNG (was ⬜)
- ✅ HTML web page `export.html` — File › Export › Web Page (`ID_EXPHTML`): `export_text(EXP_HTML)`, headings and paragraphs (was ⬜)
- ✅ Rich Text Format (.rtf) `export.rtf` — File › Export › Rich Text (`ID_EXPRTF`): `export_text(EXP_RTF)` (was ⬜)
- ✅ Text (plain) `export.text` — File › Export › Text (`ID_EXPTEXT`, `dlg_export_text`)
- ➖ PostScript / EPS `export.ps` — PDFCraft hasn't built it (Planned M10); printing is left out anyway

### Fill & Sign (10: ✅ 8 · 🟨 2 · ⬜ 0 · ➖ 0)

- ✅ Add text `sign.fill.text` — Tools › Fill & Sign › Add Text (`TL_FTEXT`, doctools.c `tool_click`): borderless text box (was: only the comment Text Box) (was 🟨)
- ✅ Checkmark `sign.fill.check` — `TL_CHECK`: an ink tick where clicked (was ⬜)
- ✅ Cross `sign.fill.cross` — `TL_CROSS` (was ⬜)
- ✅ Dot `sign.fill.dot` — `TL_DOT` (was ⬜)
- ✅ Line `sign.fill.line` — the Line tool (`TL_LINE`), also in the Fill & Sign menu (was 🟨)
- ✅ Date `sign.fill.date` — `TL_DATE`: today's date as text (was ⬜)
- 🟨 Signature `sign.fill.signature` — `TL_SIGN`: the name kept in Folio.prefs, typed (no drawn or picture signature yet) (was ⬜)
- 🟨 Initials `sign.fill.initials` — `TL_INITIALS`: typed, as Signature (was ⬜)
- ✅ Change signature `sign.fill.signature.change` — `ID_SIGCHANGE` (was ⬜)
- ✅ Change initials `sign.fill.initials.change` — `ID_INITCHANGE` (was ⬜)

### Edit a PDF (14: ✅ 13 · 🟨 0 · ⬜ 1 · ➖ 0)

- ⬜ Edit text & images `edit.edit_text` — editing the page's own text and pictures in place: not done (Folio adds text and pictures, and redaction removes them)
- ✅ Rotate pages `page.rotate` — `ID_ROTL`/`ID_ROTR`
- ✅ Insert pages `page.insert` — `ID_INSFILE`, `ID_INSBLANK`
- ✅ Delete pages `page.delete` — `ID_DELPAGES`
- ✅ Extract pages `page.extract` — `ID_EXTRACT`
- ✅ Organize pages `page.organize` — `ID_ORGANIZE`
- ✅ Text `edit.text` — Document › Add Text (`TL_ADDTEXT`): core/pagetools.c `page_add_text` into the page's content (Helvetica, with widths) (was 🟨)
- ✅ Image `edit.image` — Document › Add Picture (`TL_ADDIMAGE`): `page_add_image`, JPEG kept as DCT, PNG as Flate (PNG transparency laid over white) (was ⬜)
- ✅ Header and footer `edit.header_footer` — Document › Header & Footer (`ID_HEADER`, `ID_HEADERDEL`): `hf_apply` with <<page>> <<pages>> <<label>> <<date>>, six positions, page range; Update = Add again (replaces), Remove (`marks_remove`) (was ⬜)
- ✅ Watermark `edit.watermark` — Document › Watermark (`ID_WATERMARK`): `watermark_apply`, text or picture, angle, opacity, behind; Update and Remove (was ⬜)
- ✅ Link `edit.link` — Document › Add Link (`TL_LINK`): `page_add_link`, to a page or a web address (was ⬜)
- ✅ Bates numbering `edit.bates` — Document › Bates Numbering (`ID_BATES`): prefix, start, digits; Remove (was ⬜)
- ✅ Background `edit.background` — Document › Background (`ID_BACKGROUND`): colour or picture under the pages; Update and Remove (was ⬜)
- ✅ Attach file `edit.attach` — Document › Attach File (`ID_ATTACH`): `attach_add` (embedded files name tree); Attachments lists, saves, deletes (was ⬜)

### Create a PDF (6: ✅ 5 · 🟨 1 · ⬜ 0 · ➖ 0)

- 🟨 Single file `create.file` — File › Create PDF from a picture or a text file (`ID_FROMIMAGES`, `ID_FROMTEXT`); Office documents can't be converted (no converter on the Amiga: Quill and Ledger export PDF themselves) (was ⬜)
- ✅ Multiple files `create.multiple` — pictures into one PDF (`doc_from_images`), PDFs with Combine Files (was 🟨)
- ✅ Separate PDFs `create.multiple_separate` — File › Create PDF › Pictures to Separate PDFs (`ID_SEPARATE`) (was ⬜)
- ✅ Images `create.images` — `ID_FROMIMAGES` (JPEG, PNG; each its own size) (was ⬜)
- ✅ Clipboard `create.clipboard` — `ID_FROMCLIP`: the clipboard's text (FTXT, amiga/clip.c `clip_read`) set as a PDF (was ⬜)
- ✅ Blank page `create.blank` — File › New Blank Document (`ID_NEWBLANK`, `doc_new_blank`); Insert Blank Page as before (was 🟨)

### Combine files (1: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Add files to combine `page.combine` — `ID_COMBINE`

### Organize pages (13: ✅ 12 · 🟨 0 · ⬜ 0 · ➖ 1)

- ✅ Page grid `page.organize` — `ID_ORGANIZE`
- ✅ Rotate `page.rotate` — as above
- ✅ Delete `page.delete` — as above
- ✅ Extract `page.extract` — as above
- ✅ Insert `page.insert` — as above
- ✅ Replace `page.replace` — Pages › Replace Pages (`ID_REPLACE`): `page_replace` from another PDF (was ⬜)
- ✅ Split `page.split` — File/Pages › Split Document (`ID_SPLIT`): every N pages, at top-level bookmarks, or into N files (`split_points`) (was ⬜)
- ✅ Set page boxes `page.boxes` — Pages › Set Page Boxes (`ID_BOXES`): media, crop, bleed, trim, art (`page_set_box`) (was ⬜)
- ✅ Crop pages `page.crop` — Pages › Crop Pages (`ID_CROP`, margins over a range) and Crop Page to a Box (`TL_CROP`) (was ⬜)
- ✅ Duplicate pages `page.duplicate` — Pages › Duplicate Page (`ID_DUPLICATE`) (was ⬜)
- ✅ Reverse pages `page.reverse` — Pages › Reverse Pages (`ID_REVERSE`) (was ⬜)
- ✅ Page labels `page.number` — Pages › Page Labels (`ID_LABELS`): ranges with style, prefix, start (`page_labels_set`, `page_label`) (was ⬜)
- ➖ Page transitions `page.transitions` — PDFCraft hasn't built it (Planned M4); no full-screen mode on the Amiga

### Add comments (15: ✅ 15 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Sticky note `comment.note` — `TL_NOTE`
- ✅ Highlight text `comment.highlight` — `TL_HIGHLIGHT`
- ✅ Underline `comment.underline` — `TL_UNDERLINE`
- ✅ Strikethrough `comment.strikeout` — `TL_STRIKE`
- ✅ Squiggly underline `comment.squiggly` — `TL_SQUIGGLY` (before: read and listed, not made) (was 🟨)
- ✅ Text box `comment.freetext` — `TL_TEXTBOX`
- ✅ Draw freehand `comment.ink` — `TL_INK`
- ✅ Rectangle `comment.square` — `TL_RECT`
- ✅ Oval `comment.circle` — `TL_OVAL`
- ✅ Line `comment.line` — `TL_LINE`
- ✅ Arrow `comment.arrow` — `TL_ARROW`: `annot_arrow` (line with an open arrowhead) (was ⬜)
- ✅ Cloud `comment.cloud` — `TL_CLOUD`: `annot_cloud` (PDFCraft plans it for M5; Folio has it) (was ➖)
- ✅ Stamp `comment.stamp` — `TL_STAMP` / Tools › Stamp: `annot_stamp` (planned in PDFCraft's comment group, Ready as its stamp palette) (was ➖)
- ✅ Comment list `comment.list` — the comments pane (panes.c)
- ✅ Flatten comments `comment.flatten` — Tools › Flatten Comments (`ID_FLATTENCOMMENTS`): `annots_flatten` (was ⬜)

### Scan & OCR (5: ✅ 0 · 🟨 0 · ⬜ 5 · ➖ 0)

- ⬜ In this file `ocr.recognize` — no text recognition: needs recognition models far beyond a 68040's memory and speed; not done
- ⬜ In multiple files `ocr.recognize_batch` — as above
- ⬜ Enhance scanned file `ocr.enhance` — not done (background and speckle removal of scans)
- ⬜ Enhance camera image `ocr.enhance_camera` — not done
- ⬜ Correct recognized text `ocr.correct` — not done (needs OCR)

### Protect a PDF (5: ✅ 4 · 🟨 0 · ⬜ 0 · ➖ 1)

- ✅ Protect with password `protect.password` — File/Protect › Protect with Password (`ID_SAVEPW`): pdfwrite.c `pdf_save_opts` AES-256 (R6) or AES-128 (R4), open and permissions passwords, print/copy/change/comment/fill permissions (tests/savetest.c, qpdf --check) (was ⬜)
- ✅ Remove hidden information `protect.remove_hidden` — Protect › Remove Hidden Information (`ID_HIDDEN`): core/redact.c `hidden_survey`/`hidden_remove` — metadata, scripts, attachments, comments, forms, hidden layers, invisible text, thumbnails, private data, links, bookmarks (was ⬜)
- ➖ Encrypt with certificate `protect.certificate` — PDFCraft hasn't built it (Planned M8); certificates left out
- ✅ Security properties `protect.properties` — Protect › Security Properties (`ID_SECPROPS`): method and every permission (before: Properties showed the method only) (was 🟨)
- ✅ Remove security `protect.remove` — saving writes without protection; Protect › Remove Security (`ID_REMSEC`)

### Redact a PDF (5: ✅ 5 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Redact text and images `redact.mark` — Protect › Redact Area (`TL_REDACT`), applied by Apply Redactions (`ID_REDACTAPPLY`): core/redact.c `redact_apply` removes the glyphs under the marks from the content (core/contentedit.c), clears covered image pixels (images Folio can't decode are removed), removes annotations there, paints the areas (tests/redacttest.c, pdftotext confirms) (was ⬜)
- ✅ Redact pages `redact.pages` — `ID_REDACTPAGE`: `redact_mark_page` (was ⬜)
- ✅ Find text and redact `redact.search` — `ID_REDACTTEXT`: every match marked (`redact_mark_text`) (was ⬜)
- ✅ Set properties `redact.properties` — `ID_REDACTPROPS`: fill colour and overlay text (was ⬜)
- ✅ Sanitize document `redact.sanitize` — `ID_SANITIZE`: `hidden_remove(HID_SANITIZE)` (was ⬜)

### Compress a PDF (3: ✅ 3 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Reduce file size `optimize.reduce` — File › Reduce File Size (`ID_REDUCE`): pictures at 150/100/300 dpi (core/optimize.c `optimize_images`, JPEG via core/jpegout.c), identical streams once, Flate, object streams (was ⬜)
- ✅ Advanced optimization `optimize.advanced` — `ID_ADVOPT`: resolution, JPEG quality, deduplication, object streams, metadata/thumbnails removal (was ⬜)
- ✅ Audit space usage `optimize.audit` — `ID_AUDIT`: `optimize_audit` (PDFCraft plans it for M11; Folio has it) (was ➖)

### Prepare a form (20: ✅ 17 · 🟨 3 · ⬜ 0 · ➖ 0)

- ✅ Field list `form.fields` — Forms › Field List (`ID_FIELDLIST`): every field with its kind, page and value; double-click goes there (core/forms.c `form_load`) (was ⬜)
- ✅ Detect form fields `form.detect` — `ID_DETECT`: underscores become text fields, [ ] and ☐ check boxes (`form_detect`) (was ⬜)
- ✅ Clear form `form.clear` — `ID_CLEARFORM`: `form_reset` (back to /DV) (was ⬜)
- ✅ Field properties `form.field.properties` — `ID_FIELDPROPS`: name, tooltip, read-only, required, max length, alignment, text size (`form_set_props`) (was ⬜)
- ✅ Tab order by rows `form.tab_order.row` — `ID_TABROWS`: `form_tab_order`, /Tabs /R (was ⬜)
- ✅ Tab order by columns `form.tab_order.column` — `ID_TABCOLS` (was ⬜)
- 🟨 Calculation order `form.calculation_order` — `ID_CALCORDER` sets /CO (`form_calc_order`); calculations themselves need JavaScript, left out (was ⬜)
- ✅ Flatten form fields `form.flatten` — `ID_FLATTENFORM`: `form_flatten` (appearances into the page) (was ⬜)
- ✅ Import data `form.import_data` — `ID_IMPORTDATA`: XFDF or FDF (`form_import`) (was ⬜)
- ✅ Export data `form.export_data` — `ID_EXPORTDATA`: XFDF or FDF (`form_export`) (was ⬜)
- ✅ Text field `form.add.text` — Forms › Add Field (`ID_FIELDTOOL`, `TL_FIELD`): `form_add_field(FF_TEXT)`, several lines when tall (was ⬜)
- ✅ Text field table `form.add.text_table` — `form_add_table` (rows x columns, named name.r.c) (was ⬜)
- ✅ Checkbox `form.add.checkbox` — `FF_CHECK` with its own on/off appearances (was ⬜)
- ✅ Radio button `form.add.radio` — `FF_RADIO`: buttons of one name form a group (was ⬜)
- ✅ Drop-down list `form.add.combo` — `FF_COMBO` with choices (was ⬜)
- ✅ List box `form.add.list` — `FF_LIST` (was ⬜)
- ✅ Button `form.add.button` — `FF_PUSH` with its caption (no actions: JavaScript left out) (was ⬜)
- 🟨 Image field `form.add.image` — a button field named Picture; choosing a picture into it isn't done (was ⬜)
- 🟨 Date field `form.add.date` — a text field named Date; its date format needs JavaScript (left out) (was ⬜)
- ✅ Digital signature `form.add.signature` — `FF_SIG` (an empty signature field) (was ⬜)

### Add a stamp (1: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Stamp palette `comment.stamp` — Tools › Stamp (`ID_STAMPPICK`): twelve standard stamps or your own text, then click (`TL_STAMP`) (was ⬜)

### Use a certificate (5: ✅ 0 · 🟨 1 · ⬜ 3 · ➖ 1)

- ⬜ Digitally sign `sign.digital` — signing needs a private key and certificate handling (PKCS#12, RSA): not done
- ➖ Timestamp `sign.timestamp` — PDFCraft hasn't built it (Planned M9); needs a timestamp server (network): left out
- 🟨 Validate all signatures `sign.validate` — Document › Signatures (`ID_SIGNATURES`), and clicking a signature field: core/review.c `sig_list` checks the signed byte ranges against the CMS messageDigest (SHA-1/256/384/512) and whether anything was appended; the certificate and its chain aren't checked (was ⬜)
- ⬜ Certify (visible signature) `sign.certify` — as Digitally sign
- ⬜ Certify (invisible signature) `sign.certify_invisible` — as Digitally sign

### Use print production (6: ✅ 3 · 🟨 1 · ⬜ 0 · ➖ 2)

- ➖ Output preview `prepress.output_preview` — PDFCraft hasn't built it (Planned M11)
- ➖ Preflight `preflight.run` — PDFCraft hasn't built it (Planned M11)
- 🟨 Convert colours `prepress.convert_colors` — Document › Print Production › Convert Colours to Grey (`ID_TOGRAY`, core/optimize.c `convert_to_gray`): RGB/CMYK operators and 8-bit pictures; shadings, ICC and spot colours not converted, no other target spaces (was ⬜)
- ✅ Set page boxes `page.boxes` — `ID_BOXES` (was ⬜)
- ✅ Add printer marks `prepress.marks` — `ID_MARKS`: crop marks, bleed and trim boxes (`printer_marks`); no registration marks or colour bars (was ⬜)
- ✅ Fix hairlines `prepress.hairlines` — `ID_HAIRLINES`: `fix_hairlines` (was ⬜)

### Measure objects (8: ✅ 4 · 🟨 2 · ⬜ 1 · ➖ 1)

- ✅ Distance `measure.distance` — Tools › Measure › Distance (`TL_MEASURE`): `annot_measure`, a dimension line with its length and a /Measure dictionary (was ⬜)
- 🟨 Perimeter `measure.perimeter` — `TL_PERIM`: of a dragged rectangle only (`annot_measure_area` takes any polygon; drawing one point by point isn't there) (was ⬜)
- 🟨 Area `measure.area` — `TL_AREA`: as Perimeter (was ⬜)
- ✅ Drawing scale `measure.scale` — `ID_SCALE` (units per inch, kept in Folio.prefs) (was ⬜)
- ✅ Measurement information `measure.info` — `ID_MEASINFO`: the document's measurements listed (was ⬜)
- ⬜ Snapping `measure.snap` — snapping to the page's paths and ends: not done
- ✅ Export measurements `measure.export` — `ID_MEASEXPORT`: CSV (was ⬜)
- ➖ Geospatial location `measure.geo` — PDFCraft hasn't built it (Planned M12)

### Compare files (1: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Select files to compare `doc.compare` — Tools › Compare Files (`ID_COMPARE`): core/review.c `compare_docs`, text compared line by line, the report, insertions highlighted and deletions noted in this document (no visual compare) (was ⬜)

### Use guided actions (5: ✅ 0 · 🟨 1 · ⬜ 2 · ➖ 2)

- ⬜ Action Wizard `actions.wizard` — recorded sequences of commands: not done (scripting is left out; the tools run one by one)
- ➖ Make accessible `actions.make_accessible` — PDFCraft hasn't built it (Planned M13)
- 🟨 Prepare for distribution `actions.distribution` — the same steps by hand: Remove Hidden Information, then Reduce File Size (was ⬜)
- ⬜ Optimize scanned documents `actions.optimize_scans` — needs OCR (see Scan & OCR)
- ➖ Archive documents `actions.archive` — PDFCraft hasn't built it (Planned M13)

### Prepare for accessibility (9: ✅ 6 · 🟨 2 · ⬜ 0 · ➖ 1)

- 🟨 Automatically tag PDF `a11y.autotag` — Document › Accessibility › Automatically Tag (`ID_AUTOTAG`, `a11y_autotag`): a Document with a paragraph per page and form fields as Form elements; no headings, tables or reading-order analysis (was ⬜)
- ✅ Tag form fields `a11y.autotag_fields` — form fields get Form elements with object references (`a11y_autotag(fields)`) (was ⬜)
- ✅ Change reading options `a11y.reading_options` — `ID_READOPTS`: language, title in the title bar (was ⬜)
- ✅ Check for accessibility `a11y.check` — `ID_A11YCHECK`: `a11y_check` — tags, tagged content, language, title, image-only pages, fonts, figure alternate text, links, field descriptions, tab order, bookmarks, and what needs a person (was ⬜)
- ✅ Find untagged content `a11y.find_untagged` — `ID_UNTAGGED`: pages with content outside marked content (was ⬜)
- ✅ Open accessibility report `a11y.report` — the check's report (`a11y_report`) (was ⬜)
- ✅ Add alternate text `a11y.alt_text` — `ID_ALTTEXT`: the tagged figures, their /Alt set (was ⬜)
- 🟨 Tags panel `a11y.tags` — `ID_TAGTREE`: the structure tree as a list to read; no editing of tags (was ⬜)
- ➖ Fix reading order `a11y.reading_order` — PDFCraft hasn't built it (Planned M12)

### Apply PDF standards (4: ✅ 0 · 🟨 1 · ⬜ 0 · ➖ 3)

- 🟨 Save as PDF/A `standards.pdfa` — File › Save as PDF/A (`ID_PDFA`, `pdfa_convert`): XMP pdfaid 2B, an sRGB output intent (its ICC profile made by Folio), no scripts or attachments, annotation flags; fonts that aren't embedded are reported, not embedded (was ⬜)
- ➖ Save as PDF/X `standards.pdfx` — PDFCraft hasn't built it (Planned M11)
- ➖ Save as PDF/UA `standards.pdfua` — PDFCraft hasn't built it (Planned M11)
- ➖ Preflight `preflight.run` — PDFCraft hasn't built it (Planned M11)

### Add search index (1: ✅ 0 · 🟨 0 · ⬜ 0 · ➖ 1)

- ➖ Manage embedded index `search.index` — PDFCraft hasn't built it (Planned M12)

### Use JavaScript (3: ✅ 0 · 🟨 0 · ⬜ 0 · ➖ 3)

- ➖ Console `js.console` — JavaScript is left out on purpose (and PDFCraft plans this for M6)
- ➖ Document JavaScripts `js.document` — as above
- ➖ Document actions `js.actions` — as above

### AI assistant (3: ✅ 0 · 🟨 0 · ⬜ 0 · ➖ 3)

- ➖ Summarize `ai.summary` — AI and cloud services are left out on purpose (PDFCraft's are an optional provider too)
- ➖ Ask about this document `ai.ask` — as above
- ➖ Translate `ai.translate` — as above

## 2. Beyond the catalogue

PDFCraft's menu bar and keys (the commands not in the catalogue), its side panels, and features that aren't one command.

### Menus and keys (51: ✅ 27 · 🟨 4 · ⬜ 18 · ➖ 2)

- ✅ Open `file.open` — `ID_OPEN` (Amiga-O)
- ✅ Open Recent `file.open_recent` — Home page and `ID_RECENT0`
- ⬜ Pin folder `file.pin_folder` — favourite folders on Home: not done
- ✅ Save `file.save` — `ID_SAVE`
- ✅ Save As `file.save_as` — `ID_SAVEAS`
- ✅ Revert `file.revert` — File › Revert to Saved (`ID_REVERT`) (was ⬜)
- ✅ Close `file.close` — `ID_CLOSE`
- ✅ Close All `file.close_all` — `ID_CLOSEALL`
- ✅ Properties `file.properties` — `ID_PROPS`; XMP kept in step on request (`xmp_update`, used by PDF/A)
- ✅ Word count `file.word_count` — File › Word Count (`ID_WORDCOUNT`) (was ⬜)
- ➖ Print `print.dialog` — printing is left out on purpose
- ✅ Unlock (password) `protect.unlock` — asked when opening (`dlg_password`)
- ✅ Undo `edit.undo` — `ID_UNDO`; document-wide changes undone by snapshot (doctools.c `doc_snapshot`, last three)
- ⬜ Redo `edit.redo` — not done
- ⬜ Cut / Paste (pages, comments) `edit.cut` — copying and pasting pages and comments between documents: not done
- ✅ Copy `edit.copy` — `ID_COPY` (text)
- ⬜ Copy / paste comment `comment.copy` — not done
- ✅ Delete `edit.delete` — `ID_DELETE` (comments), Del key
- ✅ Select all `edit.select_all` — `ID_SELALL`
- ✅ Find `edit.find` — `ID_FIND`, Find Next/Previous
- ⬜ Replace `edit.replace` — replacing page text needs Edit text: not done
- 🟨 Advanced search `edit.advanced_search` — Find Text and Redact searches every page; no search panel with results list (was ⬜)
- ⬜ Column select `edit.column_select` — not done
- ⬜ Take a snapshot `edit.snapshot` — an area copied as a picture: not done (Export Page as PNG instead)
- ✅ Attach (Edit menu) `edit.attach` — as Attach file (was ⬜)
- ✅ Clear form (Edit menu) `form.clear` — as Clear form (was ⬜)
- ⬜ Preferences `app.preferences` — Folio's few settings are menu items and Folio.prefs; no Preferences window
- ✅ Actual size `view.zoom.actual_size` — `ID_ZOOM100`
- ⬜ Zoom to page level `view.zoom.page_level` — not done
- ✅ Fit to width `view.zoom.fit_width` — `ID_FITWIDTH`
- ✅ Fit page / height `view.zoom.fit_height` — `ID_FITPAGE`
- ⬜ Fit visible `view.fit_visible` — not done
- ✅ Zoom in / out `view.zoom.in` — `ID_ZOOMIN`/`ID_ZOOMOUT`
- ⬜ Marquee zoom `view.marquee_zoom` — not done
- ✅ Page number field `view.focus_page_input` — the toolbar's page field, `ID_GOTO`
- ⬜ Previous / next view `view.previous_view` — not done
- ⬜ Single page / two-up layouts `view.layout.single` — Folio shows pages continuously one under another only
- ✅ Continuous `view.continuous` — always
- ⬜ Cover page, page gaps `view.layout.cover` — not done
- ✅ Rotate view `view.rotate.cw` — `ID_ROTVIEWR`/`ID_ROTVIEWL`
- 🟨 Display theme `view.theme.dark` — View › Dark Interface (`ID_DARKUI`); no follow-the-system setting
- ✅ Side panel / tools pane `view.tools_pane` — `ID_TOOLSPANE`, `ID_THUMBS`, `ID_BOOKMARKS`
- ⬜ Split view `view.split` — not done
- ⬜ Full screen / read mode `view.full_screen` — not done
- 🟨 Bookmarks: add, rename, delete `bookmark.add` — Document › Bookmarks (`ID_BMADD`, `ID_BMRENAME`, `ID_BMDELETE`, `outline_write`); no dragging to reorder (was ⬜)
- ⬜ Bookmarks from structure `bookmark.from_structure` — from the tags: not done
- ✅ Bookmarks from text styles `bookmark.from_text_styles` — `ID_BMSTYLES`: `outline_from_styles` (was ⬜)
- ⬜ Export / import bookmark trees `bookmark.export` — not done
- ✅ Next / previous tab `view.next_tab` — `ID_NEXTDOC`/`ID_PREVDOC`
- 🟨 Help, about `help.about` — About Folio; README; no shortcuts window
- ➖ Check for updates, web pages, Discord `help.check_updates` — network features are left out

### Side panels (11: ✅ 4 · 🟨 6 · ⬜ 1 · ➖ 0)

- ✅ Comments `panel.comments` — the comments pane
- ✅ Bookmarks `panel.bookmarks` — the bookmarks pane
- ✅ Pages `panel.pages` — the thumbnails pane
- 🟨 Fields `panel.fields` — Forms › Field List (a window, not a pane) (was ⬜)
- ⬜ Layers `panel.layers` — showing and hiding optional content: not done (hidden layers can be removed)
- 🟨 Attachments `panel.attachments` — Document › Attachments (a window) (was ⬜)
- 🟨 Signatures `panel.signatures` — Document › Signatures (a window) (was ⬜)
- 🟨 Accessibility checker `panel.accessibility` — the check's report (a window) (was ⬜)
- 🟨 Tags `panel.tags` — the tag tree (a window, read only) (was ⬜)
- ✅ Search `panel.search` — the find bar
- 🟨 Compare `panel.compare` — the comparison report (a window) (was ⬜)

### Beyond the tools: features (22: ✅ 8 · 🟨 2 · ⬜ 7 · ➖ 5)

- ✅ Fill in forms `feature.fill` — click a field with Select: text (several lines, comb, password, length), check boxes, radio buttons, drop-downs, lists; appearances made for every viewer (core/forms.c, tests/formtest.c). Was left out before (was ⬜)
- ⬜ Form JavaScript (format, validate, calculate) `feature.formjs` — JavaScript left out on purpose
- ⬜ XFA forms `feature.xfa` — XFA (XML forms) aren't read
- ✅ Comments with appearances `feature.annots` — every annotation Folio makes carries an appearance stream
- ✅ Encrypted documents `feature.decrypt` — RC4 40/128, AES-128, AES-256 opened with passwords
- ✅ Saving with a password and permissions `feature.encrypt` — `pdf_save_opts` (AES-128 R4, AES-256 R6); qpdf and poppler open the files (was ⬜)
- ✅ Object streams and compressed saving `feature.objstm` — `SaveOpts.objstm`, `compress` (was ⬜)
- 🟨 PDF 2.0 `feature.pdf20` — read; written as 1.7 (with 2.0's AES-256)
- ⬜ Linearized (fast web view) saving `feature.linearize` — not done
- ⬜ Incremental saving (keeps signatures valid) `feature.incremental` — Folio rewrites the file: a signed document's signatures break when it is saved
- ✅ Repairing damaged files `feature.repair` — the cross-reference table rebuilt; fuzzed (tests/robusttest.c)
- ⬜ JBIG2 and JPEG 2000 pictures `feature.jbig2` — shown blank
- ✅ Optional content `feature.oc` — hidden layers stay hidden; layers can't be switched
- ✅ XMP metadata `feature.xmp` — written from the information (`xmp_update`) (was ⬜)
- ⬜ OCR `feature.ocr` — see Scan & OCR
- ⬜ Office import (Word, Excel, PowerPoint to PDF) `feature.officein` — Quill and Ledger export PDF; Folio doesn't convert
- ➖ Printing `feature.print` — left out on purpose
- ➖ ARexx, scripting, MCP and automation `feature.script` — left out on purpose (PDFCraft's automation crate and MCP server)
- ➖ Sound, video, 3D (PRC/U3D) `feature.media` — left out on purpose
- ➖ Network and cloud services, AI `feature.cloud` — left out on purpose
- ➖ E-signing with certificates over the network `feature.esign` — left out on purpose
- 🟨 Interface languages `feature.lang` — strings go through tr() (amiga/lang.c, lang_table.h); Folio runs in English (lang_set(LANG_EN)); PDFCraft has many catalogs

## 3. What could come next

1. Drawn signatures (an ink signature kept in Folio.prefs and placed as a stamp), polygon measuring point by point.
2. Embedding the stand-in TrueType fonts for unembedded standard fonts (PDF/A, and documents that look the same everywhere).
3. A layers pane (optional content on and off), two-up and single-page layouts, redo.
4. Incremental saving, so signed documents stay valid when comments are added.
5. Editing existing page text in simple cases (a line in a simple font), using core/contentedit.c.
6. Signature certificate checks (RSA verification of the CMS signature) and signing with a key file.

Recount with `python3 tools/gapcount.py docs/pdfcraft-gap.md` (`--write` updates the headers and tables).
