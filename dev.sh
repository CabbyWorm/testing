#!/usr/bin/env bash
# Local dev helper: source emsdk if needed, build, then serve build/ on :8765.
# emcc must be on PATH or EMSDK pointing at an installed sdk root.
set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v emcc >/dev/null 2>&1; then
  if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
    # shellcheck disable=SC1090
    . "$EMSDK/emsdk_env.sh"
  elif [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    . "$HOME/emsdk/emsdk_env.sh"
  elif [ -f "/home/user/emsdk/emsdk_env.sh" ]; then
    . "/home/user/emsdk/emsdk_env.sh"
  else
    echo "error: emcc not found. Install emsdk (https://emscripten.org) and re-run, or set EMSDK." >&2
    exit 1
  fi
fi

bash "$ROOT_DIR/build.sh"

PORT="${PORT:-8765}"
echo
echo "serving on http://127.0.0.1:$PORT/"
exec python3 -m http.server "$PORT" --directory "$ROOT_DIR/build"
