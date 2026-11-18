#!/bin/sh
# Folio's native tests (no Amiga toolchain needed): the filters, fonts, editing and annotation tests with
# ASan/UBSan, the same steps build.sh runs before the Amiga build. TMP=<dir> to build elsewhere than /tmp.
set -e
cd "$(dirname "$0")/.."
T=${TMP:-/tmp}
CORE="core/util.c core/geom.c core/zip.c core/image.c core/filters.c core/crypto.c core/pdf.c core/func.c core/cmap.c core/pfont.c core/pdfenc.c core/font.c core/raster.c core/render.c core/pimage.c core/shade.c core/textpage.c core/pdfwrite.c core/pdfedit.c core/pngout.c core/xml.c core/pagecontent.c core/forms.c core/pagetools.c core/contentedit.c core/jpegout.c core/redact.c core/optimize.c"
  SAN="cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I. -Icore"
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
  if command -v qpdf >/dev/null 2>&1; then
    for f in $T/save_*.pdf $T/form_*.pdf $T/page_*.pdf $T/redact_*.pdf $T/opt_*.pdf; do qpdf --check --password=secret "$f" >/dev/null 2>&1 || qpdf --check "$f" >/dev/null || { echo "qpdf rejects $f"; exit 1; }; done
    echo "qpdf --check: the saved files pass"
  fi
  $SAN $CORE tools/pdfrender.c -o $T/pdfrender -lm
