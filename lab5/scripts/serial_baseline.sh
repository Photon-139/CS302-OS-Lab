#!/bin/bash
# Serial baseline measurements for the speedup calculations.
#
# Runs ./serial_baseline.out for every (image, variant) REPEATS times, pinned to one core,
# checks every output against images/reference/<n>_ref.ppm, and appends one key=value line
# per run to results/serial_baseline.txt (plus image= and run= fields).
#
# Usage (from the lab5 directory):
#   scripts/serial_baseline.sh [repeats] [image numbers...]
# Examples:
#   scripts/serial_baseline.sh            # 3 repeats, images 1 2 3 4
#   scripts/serial_baseline.sh 5 1 2      # 5 repeats, images 1 and 2
#
# Note: with ITERATIONS=1000, one run on a large image takes minutes (image 7: well over
# ten minutes per run), so the default leaves out images 5-7.

set -euo pipefail

REPEATS=${1:-3}
shift || true
IMAGES=("$@")
if [ ${#IMAGES[@]} -eq 0 ]; then
    IMAGES=(1 2 3 4)
fi

CPU=0                                   # serial baseline runs on one fixed core
BIN=./serial_baseline.out
OUT_DIR=results
OUT_FILE=$OUT_DIR/serial_baseline.txt
TMP_IMG=$(mktemp --suffix=.ppm)
trap 'rm -f "$TMP_IMG"' EXIT

make -s "$BIN"
mkdir -p "$OUT_DIR"

echo "# serial baseline, $(date '+%F %T'), host=$(hostname), repeats=$REPEATS, cpu=$CPU" >> "$OUT_FILE"

for img in "${IMAGES[@]}"; do
    for variant in 1 2; do
        for run in $(seq 1 "$REPEATS"); do
            line=$(taskset -c "$CPU" "$BIN" "$variant" "images/$img.ppm" "$TMP_IMG")

            if ! cmp -s "$TMP_IMG" "images/reference/${img}_ref.ppm"; then
                echo "ERROR: image $img variant $variant run $run: output differs from reference" >&2
                exit 1
            fi

            echo "image=$img run=$run $line" | tee -a "$OUT_FILE"
        done
    done
done

echo "results appended to $OUT_FILE"
