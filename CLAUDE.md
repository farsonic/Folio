# CLAUDE.md - working on Folio

Folio is a PDF viewer and editor for AmigaOS 3.x (68040/68060 with FPU, MUI 3.8, AROS or MorphOS),
written in C and laid out the way PDFCraft is: Home/Tools/document tabs, a toolbar, thumbnails or
bookmarks on the left, tools or comments on the right, pages in the middle. It reads PDF 1.0-2.0
(object and cross-reference streams, every standard filter, RC4/AES encryption), draws pages with its
own renderer and fonts (Type1, TrueType, CFF/OpenType, Type3), extracts and finds text, and edits:
comments (notes, highlights, ink, shapes), page organising (insert, delete, rotate, reorder) and
saving. GPL-2.0-only (see `LICENSE`). This file is for anyone, person or agent, picking the work up.
Read it first, then `README.md` (what Folio does).

## Layout

| Path | What |
|---|---|
| `core/` | Portable C99, no AmigaOS calls: `pdf.c` (objects, xref, page tree), `filters.c` (Flate, LZW, ASCII85/Hex, RunLength, CCITT, DCT), `crypto.c` (RC4, AES, MD5, SHA-2, the standard security handler), `font.c`/`pfont.c`/`cmap.c`/`pdfenc.c`/`fonts.c`/`ttraster.c` (fonts), `render.c`/`raster.c`/`shade.c`/`pimage.c`/`func.c` (content interpreter and rasteriser), `textpage.c` (text extraction and search), `pdfwrite.c` (writing: compression, object streams, AES-128/256 password protection)/`pdfedit.c` (comments, pages), `pngout.c`; after 0.1.2: `pagecontent.c` (adding to page content, object helpers), `forms.c` (AcroForm fill, appearances, FDF/XFDF, preparing forms), `pagetools.c` (boxes, labels, headers/footers, watermarks, backgrounds, Bates, text, pictures, links, attachments, bookmarks, stamps, measuring, new documents, XMP), `contentedit.c` (content rewritten operator by operator), `redact.c` (redaction, hidden information), `optimize.c` (smaller files, print production), `jpegout.c`, `export.c` (HTML, RTF, .docx, .pptx, pictures), `review.c` (compare, accessibility, signatures, PDF/A); round 2: `pki.c` (DER, big numbers, RSA/ECDSA, X.509, CMS, PKCS #12/PEM), `sign.c` (signing as an incremental update; `pdf_save_incremental` is in `pdfwrite.c`), `editpage.c` (retyping page text, pictures replaced/moved, snap points), `scan.c` (OCR, deskew, despeckle, levels, CCITT G4 via `filters.c`). |
| `amiga/` | The MUI front end: `main.c`, `view.c` (the page view: off-screen buffer, blitter scrolling, background rendering of the next page), `bars.c`, `panes.c`, `edits.c` (comments and page editing), `dialogs.c`, `doctools.c` (every tool added after 0.1.2: menus Document, Forms, Protect, the Tools page's groups, form filling by clicking, undo by snapshot; round 2 at its end: edit text & images, certificates, Scan & OCR, polygons and snapping), `signpad.c` (drawing a signature), `lang.c`/`lang_table.h` (interface strings, the table made from `lang/strings.txt`). |
| `tests/` | Native tests (`filtertest.c`, `fonttest.c`, `edittest.c`, `annotdrawtest.c`, `robusttest.c` (hostile PDFs), `allocfail.c` (out of memory everywhere), `savetest.c`, `formtest.c`, `pagetest.c`, `redacttest.c`, `exporttest.c`, `reviewtest.c` (the tools after 0.1.2), `signtest.c`, `edit2test.c`, `scantest.c` (round 2)), generated PDFs (`gen/`, made by `make_pdfs.py`), PDFs made by Quill (`pdfs/`), test fonts (`fonts/`), `compare.py` (compares rendered pages with poppler's). |
| `lang/` | `strings.txt`: every interface string with German, French, Spanish and Italian (Acrobat's terms); `ignore.txt`: literals that aren't words. `tools/mklang.py` makes `amiga/lang_table.h`, `tools/langcheck.py` checks the source (both in `tools/test.sh`). |
| `docs/` | `pdfcraft-gap.md`: every PDFCraft tool, menu command and panel with Folio's status and evidence (recount with `tools/gapcount.py`). |
| `tools/` | `test.sh` (the native tests; what CI runs), `gapcount.py`, `fuzz.c`/`fuzz.sh` (the fuzzer), `pdfrender.c` (renders a page to PNG on the host), `Install-Folio` (Installer script), `mkicons.py`, `vbcc-aos68k-fpu` (the vbcc config for the 68040+FPU build). |
| `sample/` | `Welcome.pdf`, shipped in the package. |

## Build and test

Native tests (need only a C compiler; CI runs them and they must stay green):

    sh tools/test.sh            # TMP=<dir> to build somewhere other than /tmp; FUZZ=1 to fuzz a minute too

Amiga binary: `./build.sh` (`./build.sh fast` skips the tests). It needs, at these default paths (or set
`VBCC`, `NDK`, `MUI`):

- **vbcc** 0.9h with vasm and vlink and the m68k-amigaos target, in `/opt/amiga/vbcc`
  (http://www.compilers.de/vbcc.html), with the 68040 FPU maths library (`-lm040`); Folio uses the
  config in `tools/vbcc-aos68k-fpu`.
- **NDK 3.2** C headers in `/opt/amiga/ndk/Include_H` (Aminet `dev/misc/NDK3.2.lha`).
- **MUI 3.8 developer headers** in `/opt/amiga/mui/include` (Aminet `dev/mui/mui38dev.lha`, its
  `C/Include`; not in this repository because it may only be redistributed whole).

Rendering a page on the host to look at it: build `tools/pdfrender.c` with the core (see `tools/test.sh`)
and run it on a PDF; `tests/compare.py` compares against poppler's `pdftoppm` where installed.

Release package: the `Folio` drawer holds Folio, README.md, Install-Folio, `sample/Welcome.pdf`, the
Fonts drawer (Amiga screen fonts, shared with Quill) and the TrueType drawer (Liberation, Carlito,
Caladea, SIL OFL, with `LICENSE-fonts.txt`), Source/ and LICENSE, each with an icon (`tools/mkicons.py`).
Packages are `.lha`, not committed (attach them to GitHub releases). Before a package ships, search it
for certificate or key material (`BEGIN CERTIFICATE`, `PRIVATE KEY`, `*.crt`, `*.pem`, `*.key`).

## Rules of the project

- C99 that vbcc compiles (watch for: `memchr` with bytes > 0x7F passed as int, `%.*s` with precision 0
  printing everything, rare `-O2` miscompiles). 68040 + FPU code.
- The window fits 640x480. Rendering never blocks the interface: the view renders in steps and keeps
  what was drawn on screen until a later try gets further.
- Every interface string goes through `tr()` (`amiga/lang.c`) and has a row in `lang/strings.txt` with all four
  translations; run `python3 tools/mklang.py` and `python3 tools/langcheck.py` after changing strings.
  Keep sentences for requesters short; `ask()` wraps them at 66 characters.
- No certificate or key material in the repository: tests make keys at test time in `$TMP` (openssl).
- Folio leaves out on purpose: printing to printers, ARexx/scripting/MCP and the Action Wizard,
  JavaScript (so form formats, validation and calculations aren't run), sound/video/3D, network,
  cloud and AI services, timestamps and revocation checks (CRL/OCSP). Filling and saving AcroForm forms (appearance
  streams generated) and the page editing PDFCraft offers (text, pictures, headers and footers,
  watermarks, redaction, cropping, labels, bookmarks, attachments, metadata, optimizing, splitting,
  password protection) are in scope since the `pcsync` branch. JBIG2 and JPEG 2000 images show blank.
- A new feature gets a native test and its line in `docs/pdfcraft-gap.md` (then
  `python3 tools/gapcount.py docs/pdfcraft-gap.md --write`), and a README note.
- Test documents must be ours or generated: don't commit PDFs made by others. (Two third-party PDFs
  used locally are listed in `.gitignore`.)
- Don't use `rm -rf` in scripts or by hand on shared machines; move things aside.
- Never put a personal email address in commits, code or docs.

## Where work stands

Folio 0.1.2 is released: viewing, finding, comments, page organising and saving, with smooth scrolling
and comment drawing (0.1.1/0.1.2). Its PDF engine is also used, as a patched copy, by pdf.datatype
(`farsonic/pdfdt`, where it was fuzzed). The bugs that work found are fixed here too (after 0.1.2, not
released yet), each with a test in `tests/robusttest.c` that failed before its fix:

- heap overread while rebuilding the cross-reference table (`xref_trailer_at_end`);
- tokens freed uninitialised in `parse_at` (`parse_wrong_offset`; seen under valgrind);
- a dangling pattern pointer after `sc`/`scn` with a name and no resources (`pattern_name_*`);
- dash patterns with a negative entry or far finer than the line (`dash_negative`, `dash_too_fine`);
- the number lexer's overflow with a 32-bit `long` (`lexer_big_numbers`, in the `-m32` build);
- a shift by 32 in the mesh shadings' bit reader (`mesh_shift32`);
- an over-full JPEG Huffman table (`jpeg_overfull_huffman`);
- names lost when memory runs out (`tests/allocfail.c`);
- out-of-range double-to-integer conversions (`huge_numbers*`; `dbl_int`/`dbl_long` in `core/util.h`);
- found by Folio's own fuzzer (or while reviewing what it found): a long real overrunning the writer's
  buffer (`save_huge_real`), a page's pixel count overflowing a 32-bit long (`huge_page`), JPEG
  coefficients overflowing the inverse DCT (`jpeg_huge_coefficients`), xref fields too big for a
  32-bit long (`xref_out_of_range`), `idiv`/`mod` by -1 and `bitshift` in calculator functions
  (`huge_numbers`). About 500,000 fuzzed inputs since (64- and 32-bit builds) found nothing more.

Robustness tooling: `tests/robusttest.c` (hostile PDFs made in memory, each in a child with a time
limit; every sanitizer report fatal, `float-cast-overflow` on; also built `-m32` where possible, and
partly run under valgrind), `tests/allocfail.c` (the core built with `-DFOLIO_ALLOC_TEST`, each
allocation failing in turn; `ALLOCFAIL_ALL=1` for every allocation of every file given), and the fuzzer
`tools/fuzz.c` with `tools/fuzz.sh SECONDS` (`FUZZ=1 sh tools/test.sh` runs it for a minute; `M32=1`
for a 32-bit build; crashes are kept as `$TMP/folio_fuzz/crash-<seed>.pdf`, replayed with
`folio_fuzzer -r FILE`). Its work bound is the renderer's abort callback (a page stops after 1.5 s of
CPU); Folio has no other per-document work counter. Each new fuzzer finding gets a robusttest case.
Next: fuzz for longer (also with fonts given through `FUZZ_FONTS`, and seeds from `tests/pdfs/`).

PDFCraft parity (branches `pcsync` and `fr2`, not released yet): `docs/pdfcraft-gap.md` compares Folio
with PDFCraft's tool catalogue at `2251a75` (2026-11-17): 121 of 130 applicable tools done, 9 partly,
none missing, 22 left out (it was 105/14/12/21 after `pcsync`). Round 2 (`fr2`) added: the interface
in German, French, Spanish and Italian (Locale prefs or View › Language); editing a page's own text
and pictures; signing and certifying with a PKCS #12/PEM certificate (RSA, ECDSA P-256/P-384) and
checking signatures' certificates and chains offline; incremental saving; OCR and scan clean-up
(G4); drawn signatures; polygons and snapping; colours to RGB/CMYK; PDF/A font embedding; picture
fields; Prepare for Distribution. The core has native tests for all of it (signtest checks Folio's
signatures with OpenSSL and pdfsig, keys made at test time).

Emulator check (branch `femu`, 2026-11-18): the Amiga side was run for the first time, on FS-UAE
(A4000/68040 without JIT, uaegfx, AROS ROM with its Zune) at 640x480 and on a 1024x768 8-bit public
screen, in English and German (French, Spanish, Italian spot-checked). Driven with the start arguments
`CMD=<id>` (an `ids.h` command once the documents are open), `LANG=<code>` and `DEBUG=<log>` (logs each
dialog's size, or that it couldn't be made, and each requester's text), plus the mouse and keys. Checked:
the Tools page and pane, the menus (English and German menu bars fit 640), about 45 dialogs (headers and
footers, watermarks, backgrounds, Bates, labels, crop, boxes, split, password protection, hidden
information, sanitize, reduce/advanced optimization, audit, PDF/A, Compare's requester, accessibility
check, reading options, tags, signatures, field list, the Tools page's groups, stamps, drawing scale,
hairlines, printer marks, redaction, enhance scans, trusted certificates, the sign pad), filling a form by
clicking (text, check box, radio button, drop-down), Edit Text & Images on a heading, a drawn signature
placed with Fill & Sign, signing with a PKCS #12 certificate from start to end (pdfsig: valid; about 12 s
in the emulator) and checking it, OCR of a page. No dialog is taller than 210 pixels; the German ones fit.
Fixed then: text dialogs that never opened on Zune (no Floattext.mui there), the colour picker without
Coloradjust.mui, the Tools pane cutting off seven tools at 480 lines, choice lists 130 pixels wide, Edit
Text & Images opening the comment bar, a grey sign pad, one-colour boxes losing their pen on palette
screens, repeated icons (see the commit). Still to look at: on the 8-bit 1024x768 screen the board around
the pages stayed dark blue (not on the 640x480 Workbench; maybe AROS's pens on an RTG screen opened by a
test program); Zune's lists use topaz, so their widths are estimates. Needs real AmigaOS 3.x with MUI 3.8:
the menus' submenus near the right edge (Forms › Add Field in German; AROS moves them, Intuition may
not), MUI's own Floattext/Coloradjust (the fallbacks are what Zune showed), and speed on a real 68040:
OCR (templates made from the TrueType drawer at the first OCR), RSA signing and checking, rendering.
Next steps: section 3 of the gap document.

## Issues

Bugs and requests come in as GitHub issues (templates in `.github/ISSUE_TEMPLATE/`). To work on them:

1. `gh issue list --repo farsonic/Folio` and `gh issue view N --repo farsonic/Folio --comments`.
2. Reproduce first. Most PDF problems can be reproduced natively with the core (render the page with
   `tools/pdfrender.c`, or load it in a test), then write a failing test in `tests/` and fix it in
   `core/`. Interface bugs need the Amiga build; say what you could and couldn't verify.
3. Fix on a branch named `issue-N-short-name`, keep `tools/test.sh` green, and open a pull request whose
   description says `Fixes #N`, what changed and how it was tested. Ask on the issue when information is
   missing rather than guessing. Don't close issues by hand unless asked; merging the PR closes them.
4. Attached PDFs may be private: never commit them; reproduce with a small PDF of your own (extend
   `tests/make_pdfs.py`).

`.github/workflows/claude.yml` lets people mention `@claude` in an issue or pull request to have Claude
Code look at it (it needs the `ANTHROPIC_API_KEY` repository secret or the Claude GitHub App).
