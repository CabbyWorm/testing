# Working in `testing`

A multi-visualization sokol → WebAssembly → GitHub Pages testbed. This is
the **single-page** sibling of `doors-dev`: one canvas, a viz-switcher menu,
and N independent visualizations plugged in via a vtable. Live site:
[cabbyworm.github.io/testing](https://cabbyworm.github.io/testing/).

This repo doubles as the source-of-truth for sokol/wasm patterns we reuse
elsewhere — most of `doors-dev` was scaffolded by reading from here.

## Where things live

```
build.sh               — emcc invocation; lists every viz_*.c explicitly.
dev.sh                 — emsdk activation + ./build.sh + python3 -m http.server :8765.
src/main.c             — sokol_app entry, frame loop, input, menu, viz dispatch.
                         Defines SOKOL_IMPL exactly once.
src/common.h           — sokol header includes (no IMPL), camera_t, shared
                         helpers (compute_camera_basis, fullscreen_tri_buffer,
                         main_get_seed).
src/viz.h              — viz_iface vtable + extern VIZ_REGISTRY array.
src/viz_registry.c     — one-line-per-viz registry (manual, NOT auto-generated).
src/viz_fractal.{h,c}  — raymarched 3D fractal (Mandelbulb / Mandelbox / etc.)
src/viz_volcano.{h,c}  — raymarched 3D night-time stratovolcano with plume.
src/shell.html         — emscripten shell with browser-fingerprint seeding
                         and iOS shake-to-switch-fractal button.
third_party/sokol      — git submodule, pinned commit.
.github/workflows/pages.yml — push-to-main → emsdk → emcc → deploy-pages action.
```

## The viz_iface contract (`src/viz.h`)

Each visualization is a self-contained TU exporting one `const viz_iface`:

```c
typedef struct viz_iface {
    const char *name;          // shown in menu and overlay
    const char *url_token;     // matched against ?viz=

    void (*init)(void);
    void (*apply_defaults)(camera_t *cam);
    void (*url_overrides)(void);                // parse ?type= ?palette= etc.
    void (*draw)(const camera_t *cam, double time_accum);
    void (*overlay)(void);                       // sokol_debugtext text

    bool supports_auto_zoom;                     // 'Z' key dolly-loop
} viz_iface;
```

The shared orbit camera lives in `main.c`'s `state.cam`. Every viz reads from
it; `apply_defaults` sets the home framing for that viz; `url_overrides`
applies `?yaw=`/`?pitch=`/`?dist=` and viz-specific URL params after init.

## Adding a new viz

1. Create `src/viz_yourname.{h,c}` (mirror `viz_volcano.c` for a fullscreen
   raymarcher, or `viz_fractal.c` for a seed-driven raymarcher).
2. Define `const viz_iface VIZ_YOURNAME_IFACE` at the bottom.
3. Append it to `src/viz_registry.c` (the registry is **manual** here —
   doors-dev's auto-discovery is a doors-only convenience).
4. Add `src/viz_yourname.c` to the source list in `build.sh`.
5. `./dev.sh` and verify locally; `?viz=yourname` should pick it.

## Sokol patterns (the reference implementation lives here)

- One TU defines `SOKOL_IMPL` (currently `src/main.c`); everyone else uses
  the declarations from `common.h`.
- Shaders are inline GLSL 300 ES strings (`-DSOKOL_GLES3`, WebGL2). No
  sokol-shdc step.
- Uniform-block `.size` MUST equal sum of declared `glsl_uniforms` sizes.
  Validation panics on mismatch.
- For fullscreen raymarchers: bind `fullscreen_tri_buffer()`, draw 3 verts,
  pass camera info via uniforms. The triangle's verts are
  `(-1,-3) (-1,1) (3,1)` so `gl_Position.xy = pos.xy` covers the whole
  screen.
- The orbit camera has `distance0` (constant home distance) and `distance`
  (current "telephoto" zoom). The shader computes
  `fov_scale = distance0 / distance` and uses it to narrow the FOV instead
  of moving the camera through the surface.
- Don't put non-ASCII characters in `sdtx_printf` calls. `sokol_debugtext`
  is ASCII-only.

## Shell.html magic

The shell does two things beyond canvas plumbing:

1. **Browser fingerprint → seed.** Hashes the user agent, screen size,
   timezone, language, and GPU renderer into an 8-hex seed and writes it to
   the URL as `?seed=...`. The C side reads it via `sargs_value("seed")`
   so the same browser always lands on the same fractal.
2. **iOS shake-to-switch.** A small DOM button gates
   `DeviceMotionEvent.requestPermission()` on iOS 13+ (Apple requires the
   permission prompt to fire from a user-tap handler, which sokol's canvas
   eats). Once granted, the JS listens to `devicemotion` and calls the
   wasm-exported `next_fractal()` on a hard shake.

Reuse this shell when scaffolding sister projects only if you want the
fingerprint seed; for a clean app shell, copy `doors-dev/src/shell.html`
instead (no fingerprint, has localStorage bridges and a toast).

## CI / deploy

`.github/workflows/pages.yml` runs on push to `main`:

1. `mymindstorm/setup-emsdk@v14` with version 3.1.64
2. Cache `~/.emscripten_cache` keyed on `hashFiles('build.sh')`
3. `hendrikmuhs/ccache-action` for the compiler
4. `./build.sh` (with `EM_COMPILER_WRAPPER=ccache`)
5. `actions/upload-pages-artifact` then `actions/deploy-pages`

GitHub Pages is configured to source from "GitHub Actions" (Settings →
Pages). No deploy-key dance — same-repo, OIDC-authenticated.

## Local build

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).
`dev.sh` sources emsdk from `$EMSDK`, `~/emsdk`, or `/home/user/emsdk`
(whichever it finds first):

```sh
git submodule update --init --recursive
./dev.sh                    # builds + serves on :8765
```

Manual:

```sh
source /path/to/emsdk/emsdk_env.sh
./build.sh
python3 -m http.server -d build 8765
```

## Testing — drive rodney

Same flow as doors-dev:

```bash
./build.sh
cd build && python3 -m http.server 8765 --bind 127.0.0.1 &
rodney start
rodney open http://127.0.0.1:8765/index.html
rodney waitstable && sleep 1
rodney screenshot -w 480 -h 800 /tmp/viz.png
# Read tool to view the screenshot.
```

For specific viz: `?viz=fractal` or `?viz=volcano`. Seed override:
`?seed=deadbeef`.

## Relationship to `doors-dev`

`doors-dev` is the larger sibling — same sokol/wasm/Pages stack, but with:

- 22 daily days behind a 3D advent-calendar door menu
- Build-time date filtering (only revealed days compile in)
- Two-repo split (private dev + public release)
- Daily cron CI

If you find yourself reaching for "I want this viz to be interactive,
need input callbacks, save state, etc." — that's doors-dev territory; copy
patterns from `doors-dev/CLAUDE.md`.

## Don't

- Don't commit `build/` (gitignored).
- Don't reorder VIZ_REGISTRY without checking `?viz=N` deep links.
- Don't put non-ASCII in `sdtx_printf`.
- Don't break uniform-block size invariants — sokol panics on mismatch.
