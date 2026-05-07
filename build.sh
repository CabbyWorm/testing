#!/usr/bin/env sh
# Builds the Sokol cube sample to WebAssembly using Emscripten.
# Requires emsdk activated in the current shell (so `emcc` is on PATH).
set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$ROOT_DIR/build"
mkdir -p "$OUT_DIR"

emcc \
  "$ROOT_DIR/src/main.c" \
  "$ROOT_DIR/src/viz_registry.c" \
  "$ROOT_DIR/src/viz_fractal.c" \
  "$ROOT_DIR/src/viz_volcano.c" \
  -I"$ROOT_DIR/third_party/sokol" \
  -I"$ROOT_DIR/third_party/sokol/util" \
  -I"$ROOT_DIR/src" \
  -DSOKOL_GLES3 \
  -O2 \
  -std=c99 \
  -s USE_WEBGL2=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s WASM=1 \
  -s ENVIRONMENT=web \
  -s EXPORTED_FUNCTIONS='["_main","_next_fractal"]' \
  --shell-file "$ROOT_DIR/src/shell.html" \
  -o "$OUT_DIR/index.html"

echo "Built:"
ls -lh "$OUT_DIR"
