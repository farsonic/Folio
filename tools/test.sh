#!/bin/sh
# Folio's native tests (no Amiga toolchain needed): the filters, fonts, editing and annotation tests with
# ASan/UBSan, the same steps build.sh runs before the Amiga build. TMP=<dir> to build elsewhere than /tmp.
set -e
cd "$(dirname "$0")/.."
T=${TMP:-/tmp}
CORE="core/util.c core/geom.c core/zip.c core/image.c core/filters.c core/crypto.c core/pdf.c core/func.c core/cmap.c core/pfont.c core/pdfenc.c core/font.c core/raster.c core/render.c core/pimage.c core/shade.c core/textpage.c core/pdfwrite.c core/pdfedit.c core/pngout.c"
  SAN="cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I. -Icore"
  $SAN core/util.c core/filters.c core/crypto.c tests/filtertest.c -o $T/folio_filtertest -lm && $T/folio_filtertest | tail -1
  $SAN core/util.c core/geom.c core/pfont.c core/pdfenc.c tests/fonttest.c -o $T/folio_fonttest -lm && $T/folio_fonttest tests/fonts 20 1 | tail -1
  $SAN $CORE tests/edittest.c -o $T/folio_edittest -lm
  $T/folio_edittest tests/pdfs/quill_tables.pdf tests/gen/graphics.pdf $T/folio_e1.pdf | tail -1
  $T/folio_edittest tests/gen/enc_aes256.pdf tests/gen/enc_rc4_128.pdf $T/folio_e2.pdf | tail -1
  # 0.1.1: a comment drawn over the page drawn before comes out as drawing the page again
  $SAN $CORE tests/annotdrawtest.c -o $T/folio_annotdraw -lm && $T/folio_annotdraw tests/gen/graphics.pdf | tail -1 && $T/folio_annotdraw tests/pdfs/quill_tables.pdf | tail -1
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
