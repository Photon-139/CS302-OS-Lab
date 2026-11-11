#!/bin/bash
# Measurement sweeps for Part II (single machine). One run per configuration; all numbers come
# from the programs' own output (no pinning or other changes from outside).
#
#   A: main table   serial, part2_1, part2_2, part2_3   default k and N
#   B: strip size   part2_1, part2_2, part2_3           k = 1, 2, 4, ..., 128   (N default)
#   C: ring size    part2_1, part2_3                    N = 1, 2, 4, ..., 64    (k default)
#
# Every sweep uses the same images (IMAGES) and both variants. Every output image is compared
# with images/reference/<n>_ref.ppm.
#
# Each output line of a run is written to results/sweep_<X>.txt, prefixed with the run's
# configuration, e.g.
#   sweep=B program=part2_1 image=2 variant=1 k=4 N=16 check=ok stage=S1 busy=... wait_in=...
# so every line can be parsed on its own. check=ok means exit status 0 and output == reference.
#
# Usage (from the lab5 directory):
#   scripts/sweep_part2.sh [A] [B] [C]        # no arguments: all three

set -uo pipefail

IMAGES=(1 2 4)
VARIANTS=(1 2)
K_DEFAULT=8                             # = config::K
N_DEFAULT=16                            # = config::RING_CAPACITY
K_VALUES=(1 2 4 8 16 32 64 128)
N_VALUES=(1 2 4 8 16 32 64)

SWEEPS=("$@")
if [ ${#SWEEPS[@]} -eq 0 ]; then
    SWEEPS=(A B C)
fi

OUT_DIR=results
TMP_IMG=$(mktemp --suffix=.ppm)
TMP_OUT=$(mktemp)
trap 'rm -f "$TMP_IMG" "$TMP_OUT"' EXIT

make -s ./serial_baseline.out ./part2_1.out ./part2_2.out ./part2_3.out || exit 1
mkdir -p "$OUT_DIR"
FAILURES=0

# run_one <sweep> <program> <image> <variant> <k> <N>
# k and N are "-" where the program does not take them.
run_one() {
    local sweep=$1 prog=$2 img=$3 variant=$4 k=$5 n=$6
    local out_file=$OUT_DIR/sweep_$sweep.txt
    local cmd

    case $prog in
        serial)  cmd=(./serial_baseline.out "$variant" "images/$img.ppm" "$TMP_IMG") ;;
        part2_2) cmd=(./part2_2.out "$variant" "images/$img.ppm" "$TMP_IMG" "$k") ;;
        *)       cmd=("./$prog.out" "$variant" "images/$img.ppm" "$TMP_IMG" "$k" "$n") ;;
    esac

    rm -f "$TMP_IMG"
    "${cmd[@]}" > "$TMP_OUT" 2>&1
    local status=$?

    local check=ok
    if [ $status -ne 0 ]; then
        check=exit$status
    elif ! cmp -s "$TMP_IMG" "images/reference/${img}_ref.ppm"; then
        check=differs
    fi
    if [ $check != ok ]; then
        FAILURES=$((FAILURES + 1))
        echo "ERROR: sweep $sweep $prog image $img variant $variant k=$k N=$n: $check" >&2
    fi

    local meta="sweep=$sweep program=$prog image=$img variant=$variant k=$k N=$n check=$check"
    sed "s/^/$meta /" "$TMP_OUT" >> "$out_file"
    echo "$meta $(tail -n 1 "$TMP_OUT")"
}

for sweep in "${SWEEPS[@]}"; do
    out_file=$OUT_DIR/sweep_$sweep.txt
    echo "# sweep $sweep, $(date '+%F %T'), host=$(hostname)" >> "$out_file"

    for img in "${IMAGES[@]}"; do
        for variant in "${VARIANTS[@]}"; do
            case $sweep in
                A)
                    run_one A serial  "$img" "$variant" - -
                    run_one A part2_1 "$img" "$variant" "$K_DEFAULT" "$N_DEFAULT"
                    run_one A part2_2 "$img" "$variant" "$K_DEFAULT" -
                    run_one A part2_3 "$img" "$variant" "$K_DEFAULT" "$N_DEFAULT"
                    ;;
                B)
                    for k in "${K_VALUES[@]}"; do
                        run_one B part2_1 "$img" "$variant" "$k" "$N_DEFAULT"
                        run_one B part2_2 "$img" "$variant" "$k" -
                        run_one B part2_3 "$img" "$variant" "$k" "$N_DEFAULT"
                    done
                    ;;
                C)
                    for n in "${N_VALUES[@]}"; do
                        run_one C part2_1 "$img" "$variant" "$K_DEFAULT" "$n"
                        run_one C part2_3 "$img" "$variant" "$K_DEFAULT" "$n"
                    done
                    ;;
                *)
                    echo "unknown sweep: $sweep" >&2
                    exit 1
                    ;;
            esac
        done
    done
done

echo "done, $FAILURES failed run(s); results in $OUT_DIR/sweep_*.txt"
[ $FAILURES -eq 0 ]
