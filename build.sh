#!/bin/sh
# Build Folio for AmigaOS (vbcc + NDK 3.2 + MUI 3.8 headers, 68040 with its FPU) after running the
# native core tests (with the address and undefined-behaviour sanitizers).
set -e
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
NDK=${NDK:-/opt/amiga/ndk/Include_H}
MUI=${MUI:-/opt/amiga/mui/include}
cd "$(dirname "$0")"
CORE="core/util.c core/geom.c core/zip.c core/image.c core/filters.c core/crypto.c core/pdf.c core/func.c core/cmap.c core/pfont.c core/pdfenc.c core/font.c core/raster.c core/render.c core/pimage.c core/shade.c core/textpage.c core/pdfwrite.c core/pdfedit.c core/pngout.c core/xml.c core/pagecontent.c core/forms.c"
if [ "$1" != "fast" ]; then
  SAN="cc -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I. -Icore"
  $SAN core/util.c core/filters.c core/crypto.c tests/filtertest.c -o /tmp/folio_filtertest -lm && /tmp/folio_filtertest | tail -1
  $SAN core/util.c core/geom.c core/pfont.c core/pdfenc.c tests/fonttest.c -o /tmp/folio_fonttest -lm && /tmp/folio_fonttest tests/fonts 20 1 | tail -1
  $SAN $CORE tests/edittest.c -o /tmp/folio_edittest -lm
  /tmp/folio_edittest tests/pdfs/quill_tables.pdf tests/gen/graphics.pdf /tmp/folio_e1.pdf | tail -1
  /tmp/folio_edittest tests/gen/enc_aes256.pdf tests/gen/enc_rc4_128.pdf /tmp/folio_e2.pdf | tail -1
  # 0.1.1: a comment drawn over the page drawn before comes out as drawing the page again
  $SAN $CORE tests/annotdrawtest.c -o /tmp/folio_annotdraw -lm && /tmp/folio_annotdraw tests/gen/graphics.pdf | tail -1 && /tmp/folio_annotdraw tests/pdfs/quill_tables.pdf | tail -1
  $SAN $CORE tools/pdfrender.c -o /tmp/pdfrender -lm
fi
vc +tools/vbcc-aos68k-fpu -c99 -O2 -cpu=68040 -fpu=68040 -I. -I"$MUI" -I"$NDK" amiga/*.c $CORE core/fonts.c core/ttraster.c -o Folio -lamiga -lm040
ls -l Folio
