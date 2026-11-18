#!/bin/sh
# Folio's fuzzer (tools/fuzz.c) built with ASan/UBSan and run for a while over tests/gen/*.pdf:
#   sh tools/fuzz.sh [SECONDS [FIRST-SEED]]       (default 60 s; TMP=<dir> to build and work elsewhere
#                                                 than /tmp; M32=1 for a 32-bit build, a long as on the Amiga)
# Runs of 500 inputs follow each other, each from its own random seed, until the time is up. An input
# that crashes the core or hangs it is kept as $TMP/folio_fuzz/crash-<seed>.pdf and the fuzzing goes
# on; tools/fuzz -r replays such files. The exit status is 1 if anything was found.
set -e
cd "$(dirname "$0")/.."
T=${TMP:-/tmp}
SECS=${1:-60}
SEED=${2:-1}
OUT=$T/folio_fuzz
mkdir -p "$OUT"
CORE="core/util.c core/geom.c core/zip.c core/image.c core/filters.c core/crypto.c core/pdf.c core/func.c core/cmap.c core/pfont.c core/pdfenc.c core/font.c core/raster.c core/render.c core/pimage.c core/shade.c core/textpage.c core/pdfwrite.c core/pdfedit.c core/pngout.c"
BITS=""
[ "$M32" = 1 ] && BITS=-m32
cc -std=c99 -O1 -g $BITS -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
   -I. -Icore $CORE tools/fuzz.c -o $T/folio_fuzzer -lm
END=$(( $(date +%s) + SECS ))
found=0
runs=0
while [ "$(date +%s)" -lt $END ]; do
  if ! $T/folio_fuzzer "$OUT" 500 $SEED tests/gen/*.pdf > "$OUT/log" 2>&1; then
    found=$((found + 1))
    mv "$OUT/fuzz-current.pdf" "$OUT/crash-$SEED.pdf"
    mv "$OUT/log" "$OUT/crash-$SEED.log"
    echo "fuzz: seed $SEED: $(grep -m1 -E 'ERROR|runtime error|HANG' "$OUT/crash-$SEED.log" || tail -1 "$OUT/crash-$SEED.log")"
  fi
  runs=$((runs + 500))
  SEED=$((SEED + 1))
done
echo "fuzz: about $runs inputs in $SECS s (seeds up to $((SEED - 1))), $found found (kept in $OUT)"
[ $found = 0 ]
