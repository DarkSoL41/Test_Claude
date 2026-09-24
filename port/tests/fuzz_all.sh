#!/bin/sh
# Random-input comparison of the port against the original code on every level.
# usage: tests/fuzz_all.sh <build-dir> <adf> <out-dir> [frames] [seed] [jobs]
BUILD=${1:-build}; ADF=${2:-"../Supaplex (1991).adf"}; OUT=${3:-fuzz_out}
FRAMES=${4:-15000}; SEED=${5:-1}; JOBS=${6:-4}
mkdir -p "$OUT"
seq 1 111 | xargs -P "$JOBS" -I{} sh -c \
  "\"$BUILD/difftest\" --adf \"$ADF\" --frames $FRAMES --random \$(( $SEED * 1000 + {} )) --level {} \
     --coverage \"$OUT/cov_{}.txt\" > \"$OUT/level_{}.log\" 2>&1; echo \"level {}: \$(tail -n 1 \"$OUT/level_{}.log\")\""
