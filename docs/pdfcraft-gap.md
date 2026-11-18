# Folio against PDFCraft: what is missing

PDFCraft (github.com/storytold/pdfcraft, the Rust PDF editor, MIT OR Apache-2.0) is the program Folio follows. This list was made on 2026-11-18 from PDFCraft's `main` at commit `2251a75` (2026-11-17 22:47 -0600; the artcraft-sync report folio-2026-11-18 covers the 144 pull requests merged since 2026-10-27, up to bd0a21c): its tool catalogue (crates/engine/src/catalog.rs, the "All tools" groups every PDFCraft interface is built from: 152 entries in 23 groups), its menu bar (crates/ui-egui/src/menus.rs), its side panels, and its docs (docs/gaps.md, ROADMAP.md). Folio was checked command by command in its source: every ✅ cites the id (amiga/ids.h), tool (`TL_` in amiga/folio.h) or core function that does it; a ⬜ was looked for and not found.

**Branch `pcsync`** (from main 511adc5, Folio 0.1.2) added most of what was missing; each line says what Folio had at 511adc5 where that differs ("was ⬜"). The rule that Folio leaves out forms and editing page content was lifted for this work: filling and saving AcroForm forms (no JavaScript) and the page editing PDFCraft offers are now in.

Marks: ✅ Folio has it · 🟨 partly (what is missing is said) · ⬜ missing · ➖ left out on purpose (printing; ARexx, scripting and MCP; JavaScript; sound and video; network, cloud and AI services; signing with certificates over the network), or PDFCraft hasn't built it either (its catalogue marks those Planned; they are ➖ here unless Folio has them anyway).

## Summary

- **Tool catalogue:** 152 entries in PDFCraft, 130 of them applicable to Folio. Folio has **123** (✅), **7** partly (🟨) and lacks **none** (⬜); 22 are ➖. So Folio covers 95% of the applicable entries fully and all of them at least in part.
- **After branch `pcsync` (613d70e):** ✅ 105, 🟨 14, ⬜ 12, ➖ 21. **Before it (511adc5):** ✅ 24, 🟨 7, ⬜ 97, ➖ 24 (128 applicable: 19% fully).
- **Menus and keys:** 51: ✅ 28, 🟨 4, ⬜ 17, ➖ 2 (after `pcsync`: ✅ 27, 🟨 4, ⬜ 18, ➖ 2; before: ✅ 22, 🟨 2, ⬜ 25, ➖ 2).
- **Side panels:** 11: ✅ 4, 🟨 6, ⬜ 1, ➖ 0 (before: ✅ 4, 🟨 0, ⬜ 7, ➖ 0).
- **Beyond the tools: features:** 22: ✅ 11, 🟨 1, ⬜ 5, ➖ 5 (after `pcsync`: ✅ 8, 🟨 2, ⬜ 7, ➖ 5).
- **Round 2 (branch `fr2`)** closed the catalogue's last ⬜ and most 🟨: editing the page's own text and pictures, signing and certifying with a certificate and checking certificates, OCR and scan clean-up, drawn signatures, polygons and snapping, PDF/A font embedding, picture fields, Prepare for Distribution; and, beyond the catalogue, incremental saving, Replace, and the interface in five languages. The Action Wizard is now ➖ (scripting).
- **Still missing:** in the views: two-up and single-page layouts, split view, full screen, the layers panel, redo; linearized saving; JBIG2 and JPEG 2000 pictures; Office documents to PDF.
- **Partly:** Create a PDF from Office files (no converter); calculation order and date fields (their scripts aren't run); camera pictures (no perspective correction); Convert Colours (device colours only, no shadings, ICC or spot colours); simple automatic tagging and a read-only tags list.

| Group | Entries | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|---|
| Export a PDF | 9 | 7 | 0 | 0 | 2 |
| Fill & Sign | 10 | 10 | 0 | 0 | 0 |
| Edit a PDF | 14 | 14 | 0 | 0 | 0 |
| Create a PDF | 6 | 5 | 1 | 0 | 0 |
| Combine files | 1 | 1 | 0 | 0 | 0 |
| Organize pages | 13 | 12 | 0 | 0 | 1 |
| Add comments | 15 | 15 | 0 | 0 | 0 |
| Scan & OCR | 5 | 4 | 1 | 0 | 0 |
| Protect a PDF | 5 | 4 | 0 | 0 | 1 |
| Redact a PDF | 5 | 5 | 0 | 0 | 0 |
| Compress a PDF | 3 | 3 | 0 | 0 | 0 |
| Prepare a form | 20 | 18 | 2 | 0 | 0 |
| Add a stamp | 1 | 1 | 0 | 0 | 0 |
| Use a certificate | 5 | 4 | 0 | 0 | 1 |
| Use print production | 6 | 3 | 1 | 0 | 2 |
| Measure objects | 8 | 7 | 0 | 0 | 1 |
| Compare files | 1 | 1 | 0 | 0 | 0 |
| Use guided actions | 5 | 2 | 0 | 0 | 3 |
| Prepare for accessibility | 9 | 6 | 2 | 0 | 1 |
| Apply PDF standards | 4 | 1 | 0 | 0 | 3 |
| Add search index | 1 | 0 | 0 | 0 | 1 |
| Use JavaScript | 3 | 0 | 0 | 0 | 3 |
| AI assistant | 3 | 0 | 0 | 0 | 3 |
| **All** | **152** | **123** | **7** | **0** | **22** |

| Beyond the catalogue | Entries | ✅ | 🟨 | ⬜ | ➖ |
|---|---|---|---|---|---|
| Menus and keys | 51 | 28 | 4 | 17 | 2 |
| Side panels | 11 | 4 | 6 | 1 | 0 |
| Beyond the tools: features | 22 | 11 | 1 | 5 | 5 |

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

### Fill & Sign (10: ✅ 10 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Add text `sign.fill.text` — Tools › Fill & Sign › Add Text (`TL_FTEXT`, doctools.c `tool_click`): borderless text box (was: only the comment Text Box) (was 🟨)
- ✅ Checkmark `sign.fill.check` — `TL_CHECK`: an ink tick where clicked (was ⬜)
- ✅ Cross `sign.fill.cross` — `TL_CROSS` (was ⬜)
- ✅ Dot `sign.fill.dot` — `TL_DOT` (was ⬜)
- ✅ Line `sign.fill.line` — the Line tool (`TL_LINE`), also in the Fill & Sign menu (was 🟨)
- ✅ Date `sign.fill.date` — `TL_DATE`: today's date as text (was ⬜)
- ✅ Signature `sign.fill.signature` — `TL_SIGN`: typed (the name in Folio.prefs) or drawn: Change Signature › Draw, or Tools › Fill & Sign › Draw Signature (`ID_SIGDRAW`, amiga/signpad.c, a pad to write on with the mouse; kept in PROGDIR:Folio.signature), placed as an ink annotation fitted into the spot clicked (core/pagetools.c `annot_ink_fit`; tests/edit2test.c) (round 2: was 🟨)
- ✅ Initials `sign.fill.initials` — `TL_INITIALS`: typed or drawn (`ID_INITDRAW`), as Signature (round 2: was 🟨)
- ✅ Change signature `sign.fill.signature.change` — `ID_SIGCHANGE` (was ⬜)
- ✅ Change initials `sign.fill.initials.change` — `ID_INITCHANGE` (was ⬜)

### Edit a PDF (14: ✅ 14 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Edit text & images `edit.edit_text` — Edit › Edit Text & Images (`TL_EDIT`): click a line (or the selection) and retype it: core/editpage.c `text_replace` takes the run's glyphs out (core/contentedit.c) and sets the new text in the run's own font when it has the glyphs (simple fonts by their encoding, CID fonts through their ToUnicode), else in the nearest standard font; the rest of the line moves by the difference. Text set at an angle isn't edited. Click a picture to replace it (JPEG/PNG), move it, resize it or delete it (`image_replace`, `image_set_matrix`, `image_delete`) (tests/edit2test.c) (round 2: was ⬜)
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

### Scan & OCR (5: ✅ 4 · 🟨 1 · ⬜ 0 · ➖ 0)

- ✅ In this file `ocr.recognize` — Document › Scan & OCR › Recognise Text on This Page / on Every Page (`ID_OCRPAGE`, `ID_OCRALL`): core/scan.c `ocr_page`, Folio's own OCR (the page drawn at 200 dpi, ink into lines, glyphs and words, each glyph matched against templates Folio draws from its own fonts at three sizes: Liberation Sans/Serif, Carlito, Caladea), laid over the page as invisible text (`ocr_add_text_layer`). Clean printed text in those kinds of type: 98% of the characters of a drawn page and 95% of a crooked, speckled 200 dpi scan of it right in tests/scantest.c; handwriting and unusual type don't come out (round 2: was ⬜)
- ✅ In multiple files `ocr.recognize_batch` — Scan & OCR › Recognise Text in Files (`ID_OCRFILES`): each file saved beside it with -ocr (round 2: was ⬜)
- ✅ Enhance scanned file `ocr.enhance` — Scan & OCR › Enhance Scanned Pages (`ID_ENHANCE`, `scan_clean_page`): straightened (skew found from the rows of glyph bottoms, ±10°), specks removed, the background levelled to white, optionally black and white as CCITT G4 (`flt_ccitt_g4_encode`) (round 2: was ⬜)
- 🟨 Enhance camera image `ocr.enhance_camera` — Scan & OCR › Enhance Camera Image (`ID_ENHANCECAM`): uneven light divided out, levels, straightened; no perspective correction or cropping to the page's edges (round 2: was ⬜)
- ✅ Correct recognized text `ocr.correct` — Scan & OCR › Correct Recognised Text (`ID_OCRCORRECT`): the page's recognised words, least certain first; one retyped in the invisible text (`text_replace` keeps render mode 3) (round 2: was ⬜)

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

### Prepare a form (20: ✅ 18 · 🟨 2 · ⬜ 0 · ➖ 0)

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
- ✅ Image field `form.add.image` — a button field named Picture; clicking it chooses a JPEG or PNG shown fitted in it (core/forms.c `form_set_picture`, its appearance and /MK /I icon; tests/edit2test.c) (round 2: was 🟨)
- 🟨 Date field `form.add.date` — a text field named Date; its date format needs JavaScript (left out) (was ⬜)
- ✅ Digital signature `form.add.signature` — `FF_SIG` (an empty signature field) (was ⬜)

### Add a stamp (1: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Stamp palette `comment.stamp` — Tools › Stamp (`ID_STAMPPICK`): twelve standard stamps or your own text, then click (`TL_STAMP`) (was ⬜)

### Use a certificate (5: ✅ 4 · 🟨 0 · ⬜ 0 · ➖ 1)

- ✅ Digitally sign `sign.digital` — Protect › Sign with a Certificate (`ID_SIGNTOOL`, drag the box) or Sign Invisibly (`ID_SIGNINVIS`): the key and certificate chain from a PKCS #12 file (PBES2/AES, as OpenSSL 3 makes them) or PEM (unencrypted or PBES2), RSA or ECDSA P-256/P-384 (core/pki.c); core/sign.c `pdf_sign` writes adbe.pkcs7.detached CMS with SHA-256 as an incremental update; poppler's pdfsig and OpenSSL accept the signatures (tests/signtest.c). Offline: no timestamp server (round 2: was ⬜)
- ➖ Timestamp `sign.timestamp` — PDFCraft hasn't built it (Planned M9); needs a timestamp server (network): left out
- ✅ Validate all signatures `sign.validate` — Protect › Validate All Signatures (`ID_SIGNATURES`): the byte ranges' digest, the CMS signature checked with the signer's certificate (RSA PKCS #1 v1.5, ECDSA; SHA-1/256/384/512), the signing time against the certificate's validity, the chain to a certificate you trust (PROGDIR:Certificates, filled by Trust a Signer's Certificate, `ID_TRUSTSIGNER`), and whether anything was appended. No revocation (CRL/OCSP: network) (round 2: was 🟨)
- ✅ Certify (visible signature) `sign.certify` — Protect › Certify with a Visible Signature (`ID_CERTIFYTOOL`): DocMDP (P 2) in the signature and the catalog's /Perms (round 2: was ⬜)
- ✅ Certify (invisible signature) `sign.certify_invisible` — `ID_CERTIFYINVIS` (round 2: was ⬜)

### Use print production (6: ✅ 3 · 🟨 1 · ⬜ 0 · ➖ 2)

- ➖ Output preview `prepress.output_preview` — PDFCraft hasn't built it (Planned M11)
- ➖ Preflight `preflight.run` — PDFCraft hasn't built it (Planned M11)
- 🟨 Convert colours `prepress.convert_colors` — Print Production › Convert Colours to Grey, RGB or CMYK (`ID_TOGRAY`, `ID_TORGB`, `ID_TOCMYK`; core/optimize.c `convert_colors`): device colours of paths and text and 8-bit pictures; shadings, ICC-based and spot colours aren't converted, and there is no colour management (profiles) (round 2: was 🟨, grey only)
- ✅ Set page boxes `page.boxes` — `ID_BOXES` (was ⬜)
- ✅ Add printer marks `prepress.marks` — `ID_MARKS`: crop marks, bleed and trim boxes (`printer_marks`); no registration marks or colour bars (was ⬜)
- ✅ Fix hairlines `prepress.hairlines` — `ID_HAIRLINES`: `fix_hairlines` (was ⬜)

### Measure objects (8: ✅ 7 · 🟨 0 · ⬜ 0 · ➖ 1)

- ✅ Distance `measure.distance` — Tools › Measure › Distance (`TL_MEASURE`): `annot_measure`, a dimension line with its length and a /Measure dictionary (was ⬜)
- ✅ Perimeter `measure.perimeter` — `TL_PERIM`: click point by point; clicking the last point again (or the first) ends it (`annot_measure_area`) (round 2: was 🟨)
- ✅ Area `measure.area` — `TL_AREA`: a polygon clicked point by point, closed on its first point (round 2: was 🟨)
- ✅ Drawing scale `measure.scale` — `ID_SCALE` (units per inch, kept in Folio.prefs) (was ⬜)
- ✅ Measurement information `measure.info` — `ID_MEASINFO`: the document's measurements listed (was ⬜)
- ✅ Snapping `measure.snap` — View › Snap to Page Objects (`ID_SNAP`): the measuring tools' points snap to the ends, corners and edge midpoints of the page's paths within 8 pixels (core/editpage.c `page_snap_points`, `snap_nearest`) (round 2: was ⬜)
- ✅ Export measurements `measure.export` — `ID_MEASEXPORT`: CSV (was ⬜)
- ➖ Geospatial location `measure.geo` — PDFCraft hasn't built it (Planned M12)

### Compare files (1: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 0)

- ✅ Select files to compare `doc.compare` — Tools › Compare Files (`ID_COMPARE`): core/review.c `compare_docs`, text compared line by line, the report, insertions highlighted and deletions noted in this document (no visual compare) (was ⬜)

### Use guided actions (5: ✅ 2 · 🟨 0 · ⬜ 0 · ➖ 3)

- ➖ Action Wizard `actions.wizard` — recorded sequences of commands are scripting, left out on purpose (the tools run one by one) (round 2: was ⬜)
- ➖ Make accessible `actions.make_accessible` — PDFCraft hasn't built it (Planned M13)
- ✅ Prepare for distribution `actions.distribution` — Document › Optimize › Prepare for Distribution (`ID_DISTRIB`, and the Compress card): Remove Hidden Information, then Reduce File Size, in one go (round 2: was 🟨)
- ✅ Optimize scanned documents `actions.optimize_scans` — Scan & OCR › Optimize Scanned Document (`ID_OPTSCAN`): every page's scan straightened and despeckled, stored as CCITT G4 when it is nearly black and white, and its text recognised (round 2: was ⬜)
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

### Apply PDF standards (4: ✅ 1 · 🟨 0 · ⬜ 0 · ➖ 3)

- ✅ Save as PDF/A `standards.pdfa` — File › Save as PDF/A (`ID_PDFA`, `pdfa_convert`): XMP pdfaid 2B, an sRGB output intent, no scripts or attachments, annotation flags, and every simple font that isn't embedded given the TrueType stand-in Folio draws it with (Liberation, Carlito, Caladea: metric-compatible), widths from the program; CID fonts without a program and Symbol/Dingbats are reported (tests/edit2test.c, pdffonts) (round 2: was 🟨)
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

### Menus and keys (51: ✅ 28 · 🟨 4 · ⬜ 17 · ➖ 2)

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
- ✅ Replace `edit.replace` — Edit › Replace Text (`ID_REPLACETEXT`): every match on every page retyped in place (`text_replace`, the last on a page first) (round 2: was ⬜)
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
- ✅ Next / previous tab `view.next_tab` — View › Next/Previous Document (`ID_NEXTDOC`/`ID_PREVDOC`; the Window menu folded into View so the German menu bar fits 640 pixels)
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

### Beyond the tools: features (22: ✅ 11 · 🟨 1 · ⬜ 5 · ➖ 5)

- ✅ Fill in forms `feature.fill` — click a field with Select: text (several lines, comb, password, length), check boxes, radio buttons, drop-downs, lists; appearances made for every viewer (core/forms.c, tests/formtest.c). Was left out before (was ⬜)
- ⬜ Form JavaScript (format, validate, calculate) `feature.formjs` — JavaScript left out on purpose
- ⬜ XFA forms `feature.xfa` — XFA (XML forms) aren't read
- ✅ Comments with appearances `feature.annots` — every annotation Folio makes carries an appearance stream
- ✅ Encrypted documents `feature.decrypt` — RC4 40/128, AES-128, AES-256 opened with passwords
- ✅ Saving with a password and permissions `feature.encrypt` — `pdf_save_opts` (AES-128 R4, AES-256 R6); qpdf and poppler open the files (was ⬜)
- ✅ Object streams and compressed saving `feature.objstm` — `SaveOpts.objstm`, `compress` (was ⬜)
- 🟨 PDF 2.0 `feature.pdf20` — read; written as 1.7 (with 2.0's AES-256)
- ⬜ Linearized (fast web view) saving `feature.linearize` — not done
- ✅ Incremental saving (keeps signatures valid) `feature.incremental` — core/pdfwrite.c `pdf_save_incremental`: only new and changed objects appended (a new page tree when pages moved), with a cross-reference section or stream and /Prev; Save uses it for signed documents (tests/signtest.c: a comment added to a signed file, the signature still valid) (round 2: was ⬜)
- ✅ Repairing damaged files `feature.repair` — the cross-reference table rebuilt; fuzzed (tests/robusttest.c)
- ⬜ JBIG2 and JPEG 2000 pictures `feature.jbig2` — shown blank
- ✅ Optional content `feature.oc` — hidden layers stay hidden; layers can't be switched
- ✅ XMP metadata `feature.xmp` — written from the information (`xmp_update`) (was ⬜)
- ✅ OCR `feature.ocr` — see Scan & OCR (round 2: was ⬜)
- ⬜ Office import (Word, Excel, PowerPoint to PDF) `feature.officein` — Quill and Ledger export PDF; Folio doesn't convert
- ➖ Printing `feature.print` — left out on purpose
- ➖ ARexx, scripting, MCP and automation `feature.script` — left out on purpose (PDFCraft's automation crate and MCP server)
- ➖ Sound, video, 3D (PRC/U3D) `feature.media` — left out on purpose
- ➖ Network and cloud services, AI `feature.cloud` — left out on purpose
- ➖ E-signing with certificates over the network `feature.esign` — left out on purpose
- ✅ Interface languages `feature.lang` — English, German, French, Spanish and Italian (Acrobat's terms), from lang/strings.txt (tools/mklang.py, tools/langcheck.py in tools/test.sh: every string translated and used); the language Locale prefs prefer, or View › Language (round 2: was 🟨)

## 3. What could come next

Round 2 (branch `fr2`) did the list that stood here: drawn signatures, polygons point by point, embedded stand-in fonts for PDF/A, incremental saving, editing the page's own text and pictures, certificate checks and signing, and added OCR and scan clean-up and the interface in four more languages. Next:

1. A layers pane (optional content on and off), two-up and single-page layouts, redo.
2. Picture fields filled by choosing a picture; perspective correction for camera pictures.
3. OCR: more training fonts (italics, Courier's stand-in), a word list to choose between close readings, columns and tables.
4. Signing: PKCS #12 files with the older 3DES/RC2 encryption; RSA-PSS; timestamps stay out (network).
5. Linearized saving; JBIG2 and JPEG 2000 pictures.

Recount with `python3 tools/gapcount.py docs/pdfcraft-gap.md` (`--write` updates the headers and tables).
