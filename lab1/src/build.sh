mkdir -p "$OUT_DIR"

for opt in "${OPT_LEVELS[@]}"; do
    g++ "-$opt" -o "$BUILD/a_${opt}.out" $SRC
done