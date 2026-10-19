#!/bin/bash

BUILD=./bin
mkdir -p "$BUILD"
SRC="image_sharpener.cpp libppm.cpp"

OPT_LEVELS=("O0" "O1" "O2" "O3")

for opt in "${OPT_LEVELS[@]}"; do
    g++ "-$opt" -o "$BUILD/a_${opt}.out" $SRC
done