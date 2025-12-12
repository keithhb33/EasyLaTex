#!/usr/bin/env bash
set -euo pipefail

C_FILE="${1:-easylatex.c}"
IN_FILE="${2:-input.itex}"
OUT_BASENAME="${3:-output}"

BUILD_DIR=".itex_build"
mkdir -p "$BUILD_DIR"

gcc -O2 -Wall -Wextra -std=c11 "$C_FILE" -o easylatex
./easylatex "$IN_FILE" > "$BUILD_DIR/$OUT_BASENAME.tex"

pdflatex -interaction=nonstopmode -halt-on-error -output-directory="$BUILD_DIR" "$BUILD_DIR/$OUT_BASENAME.tex" >/dev/null
pdflatex -interaction=nonstopmode -halt-on-error -output-directory="$BUILD_DIR" "$BUILD_DIR/$OUT_BASENAME.tex" >/dev/null

rm -f "$BUILD_DIR/$OUT_BASENAME.aux" \
      "$BUILD_DIR/$OUT_BASENAME.out" \
      "$BUILD_DIR/$OUT_BASENAME.log" \
      "$BUILD_DIR/$OUT_BASENAME.toc" \
      "$BUILD_DIR/$OUT_BASENAME.synctex.gz"

echo "Done (build outputs in $BUILD_DIR):"
echo "  $BUILD_DIR/$OUT_BASENAME.tex"
echo "  $BUILD_DIR/$OUT_BASENAME.pdf"