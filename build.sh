#!/usr/bin/env sh
# Builds the Sokol cube sample to WebAssembly using Emscripten.
# Requires emsdk activated in the current shell (so `emcc` is on PATH).
set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$ROOT_DIR/build"
mkdir -p "$OUT_DIR"

emcc "$ROOT_DIR/src/main.c" \
  -I"$ROOT_DIR/third_party/sokol" \
  -DSOKOL_GLES3 \
  -O2 \
  -std=c99 \
  -s USE_WEBGL2=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s WASM=1 \
  -s ENVIRONMENT=web \
  --shell-file "$ROOT_DIR/src/shell.html" \
  -o "$OUT_DIR/index.html"

echo "Built:"
ls -lh "$OUT_DIR"
