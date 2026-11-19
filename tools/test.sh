#!/bin/sh
# Folio's native tests (no Amiga toolchain needed): the interface strings, the filters, fonts, editing and annotation tests with
# ASan/UBSan, the same steps build.sh runs before the Amiga build. TMP=<dir> to build elsewhere than /tmp.
set -e
# Each test's output goes through a pipe into tail, which would hide a failing test's exit status: run
# under bash with pipefail so a failure stops the script (and CI). bash is on every Linux and macOS.
if [ -z "$BASH_VERSION" ] && command -v bash >/dev/null 2>&1; then exec bash "$0" "$@"; fi
if [ -n "$BASH_VERSION" ]; then set -o pipefail; fi
cd "$(dirname "$0")/.."
T=${TMP:-/tmp}
CORE="core/util.c core/geom.c core/zip.c core/image.c core/filters.c core/crypto.c core/pdf.c core/func.c core/cmap.c core/pfont.c core/pdfenc.c core/font.c core/raster.c core/render.c core/pimage.c core/shade.c core/textpage.c core/pdfwrite.c core/pdfedit.c core/pngout.c core/xml.c core/pagecontent.c core/forms.c core/pagetools.c core/contentedit.c core/jpegout.c core/redact.c core/optimize.c core/export.c core/review.c core/pki.c core/sign.c core/editpage.c core/scan.c core/respath.c"
  # the interface's words: amiga/lang_table.h made from lang/strings.txt, and every string in amiga/
  # translated into German, French, Spanish and Italian and used (tools/langcheck.py)
  python3 tools/mklang.py --check
  python3 tools/langcheck.py --unused > $T/folio_langcheck.log || { cat $T/folio_langcheck.log; exit 1; }
  if grep -q 'unused:' $T/folio_langcheck.log; then cat $T/folio_langcheck.log; exit 1; fi
  tail -1 $T/folio_langcheck.log
  SAN="cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I. -Icore"
  # (Bureau) where the shared Fonts, TrueType and Dict drawers are found: beside Folio, in Bureau:, FONTS:
  $SAN core/respath.c tests/restest.c -o $T/folio_restest && $T/folio_restest | tail -1
  $SAN core/util.c core/filters.c core/crypto.c tests/filtertest.c -o $T/folio_filtertest -lm && $T/folio_filtertest | tail -1
  $SAN core/util.c core/geom.c core/pfont.c core/pdfenc.c tests/fonttest.c -o $T/folio_fonttest -lm && $T/folio_fonttest tests/fonts 20 1 | tail -1
  $SAN $CORE tests/edittest.c -o $T/folio_edittest -lm
  $T/folio_edittest tests/pdfs/quill_tables.pdf tests/gen/graphics.pdf $T/folio_e1.pdf | tail -1
  $T/folio_edittest tests/gen/enc_aes256.pdf tests/gen/enc_rc4_128.pdf $T/folio_e2.pdf | tail -1
  # 0.1.1: a comment drawn over the page drawn before comes out as drawing the page again
  $SAN $CORE tests/annotdrawtest.c -o $T/folio_annotdraw -lm && $T/folio_annotdraw tests/gen/graphics.pdf | tail -1 && $T/folio_annotdraw tests/pdfs/quill_tables.pdf | tail -1
  # saving with options: compression, object streams, password protection (checked with qpdf where installed)
  $SAN $CORE tests/savetest.c -o $T/folio_savetest -lm && $T/folio_savetest tests/gen/fonts.pdf $T | tail -1
  # forms: filling in, appearances, export/import, clearing, flattening, adding fields, detection
  $SAN $CORE tests/formtest.c -o $T/folio_formtest -lm && $T/folio_formtest tests/gen/forms.pdf $T 2>/dev/null | tail -1
  # page and document tools: boxes, labels, headers/footers, Bates, watermarks, backgrounds, text, pictures,
  # links, attachments, bookmarks, stamps/arrows/clouds/measurements, flattening, new documents, XMP
  $SAN $CORE tests/pagetest.c -o $T/folio_pagetest -lm && $T/folio_pagetest tests/gen/graphics.pdf tests/gen/fonts.pdf tests/gen/pic.jpg tests/gen/pic.png $T 2>/dev/null | tail -1
  # redaction, hidden information, sanitizing, optimizing, hairlines, grey, printer's marks
  $SAN $CORE tests/redacttest.c -o $T/folio_redacttest -lm && $T/folio_redacttest tests/gen/fonts.pdf tests/gen/pic.jpg tests/gen/pic.png $T 2>/dev/null | tail -1
  # export: HTML, RTF, Word, text, PowerPoint, pictures (the Office files opened by python-docx/-pptx where installed)
  $SAN $CORE tests/exporttest.c -o $T/folio_exporttest -lm && $T/folio_exporttest tests/gen/pic.jpg tests/gen/pic.png $T | tail -1
  if python3 -c "import docx, pptx" 2>/dev/null; then
    python3 -c "import docx, pptx, sys; d = docx.Document(sys.argv[1]); p = pptx.Presentation(sys.argv[2]); assert d.paragraphs[0].style.name == 'Heading 1' and len(p.slides) == 2" $T/export.docx $T/export.pptx && echo "python-docx and python-pptx read the exported files"
  fi
  # compare, accessibility (check, tagging, reading options), signatures (digest check), PDF/A
  $SAN $CORE tests/reviewtest.c -o $T/folio_reviewtest -lm && $T/folio_reviewtest $T | tail -1
  # (round 2) signatures: RSA/ECDSA, X.509, CMS against OpenSSL, PKCS #12/PEM, signing and checking PDFs,
  # incremental saving (the keys are made at test time in $T by openssl; none are kept in the repository)
  $SAN $CORE tests/signtest.c -o $T/folio_signtest -lm && $T/folio_signtest $T 2>/dev/null | tail -1
  # (round 2) a page's own text and pictures edited, snapping, polygons, drawn signatures, colours, PDF/A fonts
  $SAN $CORE tests/edit2test.c -o $T/folio_edit2test -lm && $T/folio_edit2test tests/pdfs/quill_tables.pdf tests/gen/graphics.pdf tests/gen/pic.jpg tests/gen/pic.png $T tests/fonts tests/pdfs/tables.pdf 2>/dev/null | tail -1
  # (round 2) scans: CCITT G4 written, OCR of drawn and scanned text, straightening, cleaning
  $SAN $CORE tests/scantest.c -o $T/folio_scantest -lm && $T/folio_scantest $T tests/pdfs/tables.pdf tests/pdfs/angles.pdf tests/pdfs/quill_tables.pdf 2>/dev/null | tail -1
  if command -v qpdf >/dev/null 2>&1; then
    for f in $T/save_*.pdf $T/form_*.pdf $T/page_*.pdf $T/redact_*.pdf $T/opt_*.pdf $T/review_*.pdf $T/edit_*.pdf $T/signed*.pdf $T/scan_*.pdf; do qpdf --check --password=secret "$f" >/dev/null 2>&1 || qpdf --check "$f" >/dev/null || { echo "qpdf rejects $f"; exit 1; }; done
    echo "qpdf --check: the saved files pass"
  fi
  $SAN $CORE tools/pdfrender.c -o $T/pdfrender -lm
  # damaged and hostile PDFs that crashed the core, hung it or ran into undefined behaviour (each in
  # a child with a time limit, every sanitizer report fatal), also with a 32-bit long where the
  # compiler can build for it, as on the Amiga; and running out of memory at each allocation in turn
  RSAN="$SAN -fsanitize=float-cast-overflow -fno-sanitize-recover=all"
  $RSAN $CORE tests/robusttest.c -o $T/folio_robust -lm
  $T/folio_robust > $T/folio_robust.log 2>&1 || { cat $T/folio_robust.log; exit 1; }; tail -1 $T/folio_robust.log
  if echo 'int main(void){return 0;}' > $T/folio_m32.c && cc -m32 -fsanitize=address,undefined $T/folio_m32.c -o $T/folio_m32 2>/dev/null; then
    $RSAN -m32 $CORE tests/robusttest.c -o $T/folio_robust32 -lm
    $T/folio_robust32 > $T/folio_robust.log 2>&1 || { cat $T/folio_robust.log; exit 1; }; tail -1 $T/folio_robust.log
  else echo "robusttest: no 32-bit build here (cc -m32), skipped"; fi
  # (memory read before it was written, which the sanitizers above don't see: under valgrind if installed)
  if command -v valgrind > /dev/null; then
    cc -std=c99 -O1 -g -w -I. -Icore $CORE tests/robusttest.c -o $T/folio_robust_vg -lm
    valgrind -q --error-exitcode=9 --trace-children=yes $T/folio_robust_vg xref_trailer_at_end parse_wrong_offset pattern_name_no_resources > $T/folio_robust.log 2>&1 || { cat $T/folio_robust.log; exit 1; }
    echo "valgrind: $(tail -1 $T/folio_robust.log)"
  fi
  $RSAN -DFOLIO_ALLOC_TEST $CORE tests/allocfail.c -o $T/folio_allocfail -lm
  $T/folio_allocfail tests/gen/graphics.pdf tests/gen/fonts.pdf tests/gen/images.pdf tests/gen/enc_aes256.pdf > $T/folio_allocfail.log 2>&1 || { cat $T/folio_allocfail.log; exit 1; }; tail -1 $T/folio_allocfail.log
  # the fuzzer, only when asked: FUZZ=1 (FUZZ_SECS seconds, default 60; see tools/fuzz.sh)
  if [ "$FUZZ" = 1 ]; then TMP=$T sh tools/fuzz.sh ${FUZZ_SECS:-60}; fi
