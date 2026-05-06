# your sokol fractal — WebAssembly + GitHub Pages

A per-client fractal generator written in C with [Sokol](https://github.com/floooh/sokol),
compiled to WebAssembly with Emscripten, and deployed to GitHub Pages.

The browser fingerprint (user agent, screen size, GPU renderer, timezone,
language) is hashed in JavaScript before the wasm starts, then written into
the page URL as `?seed=...&gpu=...`. The C code reads that via `sokol_args`,
and the seed deterministically picks one of four escape-time fractals
(Mandelbrot, Julia, Burning Ship, Tricorn), a palette, and a starting view —
so the same browser always lands on the same fractal, and copying the URL
shares it with someone else.

The site runs entirely in the browser via WebGL2.

## Live site

```
https://cabbyworm.github.io/testing/
```

URL parameters that override the fingerprint-derived defaults:

| param   | meaning                                                      |
| ------- | ------------------------------------------------------------ |
| seed    | 8 hex digits, e.g. `?seed=deadbeef`                          |
| type    | `mandelbrot` / `julia` / `burningship` / `tricorn`           |
| palette | integer 0–5                                                  |
| zoom    | float; smaller = deeper zoom                                 |
| cx, cy  | floats, fractal-space center                                 |
| jx, jy  | floats, Julia c-constant (only for `type=julia`)             |

In-page controls: **drag** pans, **scroll wheel / pinch** zooms about the
cursor or pinch midpoint, **R** resets to the seed-derived view, **H**
toggles the overlay.

## Layout

```
src/main.c                 # fractal renderer (sokol_app/gfx/glue/log/args/debugtext)
src/shell.html             # custom Emscripten shell + fingerprint JS
build.sh                   # emcc invocation, also used by CI
dev.sh                     # local dev wrapper: build + serve on :8765
third_party/sokol          # vendored as a git submodule (pinned)
.github/workflows/pages.yml  # CI: emsdk -> emcc -> deploy-pages
```

## One-time GitHub setup

1. **Settings → Pages → Source = "GitHub Actions"**.
2. **Settings → Environments → github-pages → Deployment branches and tags**:
   allow `main` (or "All branches").

Pushes to `main` then build and deploy automatically.

## Local build

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).
The `dev.sh` wrapper sources emsdk from `$EMSDK`, `~/emsdk`, or
`/home/user/emsdk` (whichever it finds first) and serves on port 8765:

```sh
git submodule update --init --recursive
./dev.sh
# open http://localhost:8765
```

If you'd rather drive it manually:

```sh
source /path/to/emsdk/emsdk_env.sh
./build.sh
python3 -m http.server -d build 8765
```

The build outputs `build/index.html`, `build/index.js`, and `build/index.wasm`.
