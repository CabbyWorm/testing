# your sokol viz — WebAssembly + GitHub Pages

A multi-visualization renderer written in C with [Sokol](https://github.com/floooh/sokol),
compiled to WebAssembly with Emscripten, and deployed to GitHub Pages.

Two visualizations ship today:

- **fractals** — raymarched 3D distance-estimated fractal (Mandelbulb,
  Mandelbox, Sierpinski tetrahedron, or Menger sponge). The browser
  fingerprint (user agent, screen size, GPU renderer, timezone, language)
  is hashed in JavaScript and written into the page URL as
  `?seed=...&gpu=...`; the seed deterministically picks the fractal kind,
  palette, and starting view, so the same browser always lands on the
  same fractal.
- **volcanic eruption** — raymarched 3D night-time stratovolcano with
  glowing lava cracks, a volumetric ash plume, and embers.

Each visualization is its own translation unit (`src/viz_fractal.c`,
`src/viz_volcano.c`) plugged into a tiny vtable in `src/viz.h`; adding a
new one is a `.c`/`.h` pair plus a single line in `src/viz_registry.c`.

The site runs entirely in the browser via WebGL2.

## Live site

```
https://cabbyworm.github.io/testing/
```

URL parameters:

| param   | meaning                                                              |
| ------- | -------------------------------------------------------------------- |
| viz     | `fractal` (default) or `volcano`                                     |
| seed    | 8 hex digits, e.g. `?seed=deadbeef` (fractal viz)                    |
| type    | `mandelbulb` / `mandelbox` / `sierpinski` / `menger` (fractal viz)   |
| palette | integer 0–5 (fractal viz)                                            |
| power   | float, `param` for the active fractal kind                           |
| scale   | alias of `power` for mandelbox/sierpinski                            |
| dist    | float, initial camera dolly distance                                 |
| yaw     | float, initial camera yaw                                            |
| pitch   | float, initial camera pitch                                          |

In-page controls: **drag** orbits, **scroll wheel / pinch** zooms (telephoto
FOV), **R** resets to the home framing, **H** toggles the overlay,
**Z** toggles auto-zoom (fractal viz only), and **M** opens the
visualization menu (↑/↓ select, **Enter** apply, **Esc**/**M** close).

## Layout

```
src/main.c                 # sokol_app entry, frame loop, input, menu, viz dispatch
src/common.h               # shared types (camera_t), sokol-header includes (no IMPL)
src/viz.h                  # viz_iface vtable + extern registry
src/viz_registry.c         # one-line-per-viz registry
src/viz_fractal.{h,c}      # raymarched 3D fractal viz
src/viz_volcano.{h,c}      # raymarched 3D volcanic eruption viz
src/shell.html             # custom Emscripten shell + fingerprint JS
build.sh                   # emcc invocation (multi-TU), also used by CI
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
