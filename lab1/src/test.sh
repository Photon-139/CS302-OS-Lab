#!/bin/bash

BINARY=./a.out
REPEATS=5
OUT_DIR=../images/output
IMGS_DIR=../images

mkdir -p "$OUT_DIR"

for i in {1..7}
do
    img="$IMGS_DIR/${i}.ppm"
    out="$OUT_DIR/${i}_out.ppm"
    for ((run=1; run<=REPEATS; ++run)); do
        "$BINARY" "$img" "$out"
    done | awk -v img="$i" '
    {
        for (j = 1; j<=NF; j++){
            split($j, kv, "=")
            sum[kv[1]]+=kv[2]
        }
        runs++
    }
    END {
        printf "image=%s read=%.3f s1=%.3f s2=%.3f s3=%.3f write=%.3f\n",
            img, sum["read"]/runs, sum["s1"]/runs, sum["s2"]/runs, sum["s3"]/runs, sum["write"]/runs
    }
    '
done