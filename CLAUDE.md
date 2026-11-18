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
| `core/` | Portable C99, no AmigaOS calls: `pdf.c` (objects, xref, page tree), `filters.c` (Flate, LZW, ASCII85/Hex, RunLength, CCITT, DCT), `crypto.c` (RC4, AES, MD5, SHA-2, the standard security handler), `font.c`/`pfont.c`/`cmap.c`/`pdfenc.c`/`fonts.c`/`ttraster.c` (fonts), `render.c`/`raster.c`/`shade.c`/`pimage.c`/`func.c` (content interpreter and rasteriser), `textpage.c` (text extraction and search), `pdfwrite.c` (writing: compression, object streams, AES-128/256 password protection)/`pdfedit.c` (comments, pages), `pngout.c`; after 0.1.2: `pagecontent.c` (adding to page content, object helpers), `forms.c` (AcroForm fill, appearances, FDF/XFDF, preparing forms), `pagetools.c` (boxes, labels, headers/footers, watermarks, backgrounds, Bates, text, pictures, links, attachments, bookmarks, stamps, measuring, new documents, XMP), `contentedit.c` (content rewritten operator by operator), `redact.c` (redaction, hidden information), `optimize.c` (smaller files, print production), `jpegout.c`, `export.c` (HTML, RTF, .docx, .pptx, pictures), `review.c` (compare, accessibility, signatures, PDF/A). |
| `amiga/` | The MUI front end: `main.c`, `view.c` (the page view: off-screen buffer, blitter scrolling, background rendering of the next page), `bars.c`, `panes.c`, `edits.c` (comments and page editing), `dialogs.c`, `doctools.c` (every tool added after 0.1.2: menus Document, Forms, Protect, the Tools page's groups, form filling by clicking, undo by snapshot), `lang.c`/`lang_table.h` (interface strings). |
| `tests/` | Native tests (`filtertest.c`, `fonttest.c`, `edittest.c`, `annotdrawtest.c`, `robusttest.c` (hostile PDFs), `allocfail.c` (out of memory everywhere), `savetest.c`, `formtest.c`, `pagetest.c`, `redacttest.c`, `exporttest.c`, `reviewtest.c` (the tools after 0.1.2)), generated PDFs (`gen/`, made by `make_pdfs.py`), PDFs made by Quill (`pdfs/`), test fonts (`fonts/`), `compare.py` (compares rendered pages with poppler's). |
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
- Every interface string goes through `amiga/lang.c`/`lang_table.h`.
- Folio leaves out on purpose: printing to printers, ARexx/scripting/MCP, JavaScript (so form
  formats, validation and calculations aren't run), sound/video/3D, network, cloud and AI services,
  and signing with certificates over the network. Filling and saving AcroForm forms (appearance
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

PDFCraft parity (branch `pcsync`, not released yet): `docs/pdfcraft-gap.md` compares Folio with
PDFCraft's tool catalogue at `2251a75` (2026-11-17): 105 of 131 applicable tools done, 14 partly, 12
missing (OCR, signing with a certificate, editing page text in place, Action Wizard), 21 left out. New
core files (forms, pagetools, contentedit, redact, optimize, export, review) have native tests; the
Amiga side (`amiga/doctools.c`: menus Document, Forms, Protect, more in File/Tools/Pages, the Tools
page's group cards, click-to-fill fields) builds with vbcc but has **not been tried on an Amiga or
in an emulator yet**: that check comes first. The fuzzer doesn't cover the new editing code yet
(`tools/fuzz.c` could run the redaction and form paths on fuzzed files). Next steps: section 3 of
the gap document.

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
