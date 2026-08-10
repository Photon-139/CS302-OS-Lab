#!/bin/bash

BUILD=./bin
SRC="image_sharpener.cpp libppm.cpp"
OPT_LEVELS=("O0" "O1" "O2" "O3")
REPEATS=5
IMGS_DIR=../images
OUT_DIR=../images/output

if [ $# -eq 0 ]; then
    echo "usage: $0 <image_number> [image_number ...]"
    exit 1
fi

for opt in "${OPT_LEVELS[@]}"; do
    if [ ! -x "$BUILD/a_${opt}.out" ]; then
        echo "error: $BUILD/a_${opt}.out missing, run ./build.sh first" >&2
        exit 1
    fi
done

for img_num in "$@"; do
    img="$IMGS_DIR/${img_num}.ppm"
    out="$OUT_DIR/${img_num}_out.ppm"

    if [ ! -f "$img" ]; then
        echo "warning: $img not found, skipping" >&2
        continue
    fi
    echo "=== Image $img_num ==="
    for opt in "${OPT_LEVELS[@]}"; do
        binary="$BUILD/a_${opt}.out"

        for ((run=1; run<=REPEATS; run++)); do
            "$binary" "$img" "$out"
        done | awk -v img="$img_num" -v opt="$opt" '
        {
            for (j = 1; j<=NF; j++){
                split($j, kv, "=")
                sum[kv[1]] += kv[2]
            }
            runs++
        }
        END{
            printf "opt=%s image=%s read=%.3f s1=%.3f s2=%.3f s3=%.3f write=%.3f\n",
                opt, img, sum["read"]/runs, sum["s1"]/runs, sum["s2"]/runs, sum["s3"]/runs, sum["write"]/runs
        }
        '
    done
done