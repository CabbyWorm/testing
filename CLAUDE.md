# testing

Multi-visualization sokol → WASM → GitHub Pages testbed. Single page,
viz-switcher menu, one canvas. Live at
[cabbyworm.github.io/testing](https://cabbyworm.github.io/testing/).

The reference implementation for sokol/wasm patterns reused in `doors-dev`.

## Build / test

```sh
./dev.sh                  # emsdk activate + build + serve on :8765
```

Manual: `source <emsdk>/emsdk_env.sh && ./build.sh && python3 -m http.server -d build 8765`.

URL params: `?viz=fractal|volcano`, `?seed=<8hex>`, `?yaw=` `?pitch=` `?dist=`.

Verify UI changes with rodney before declaring done:

```sh
rodney start && rodney open http://127.0.0.1:8765/index.html
rodney waitstable && sleep 1 && rodney screenshot -w 480 -h 800 /tmp/v.png
# Read /tmp/v.png to verify visually.
```

## Adding a viz

Mirror an existing one (`viz_volcano.c` for a fullscreen raymarcher,
`viz_fractal.c` for a seed-driven raymarcher). Define
`const viz_iface VIZ_YOURNAME_IFACE` at the bottom. **Append it to
`src/viz_registry.c` manually** — the registry here is hand-edited, not
auto-generated like in `doors-dev`. Add the new `.c` to `build.sh`'s
source list.

## Gotchas

- One TU defines `SOKOL_IMPL` (currently `src/main.c`).
- Uniform-block `.size` must equal sum of declared `glsl_uniforms`.
  `vec2` = 2 floats, not 4. Mismatch → sokol panics.
- `sokol_debugtext` is ASCII-only — no em-dashes or curly quotes.
- `fov_scale = distance0 / distance` — the orbit camera stays at
  `distance0`; zoom is via FOV narrowing, not camera translation.
- The fullscreen-tri verts `(-1,-3) (-1,1) (3,1)` are intentional;
  `gl_Position.xy = pos.xy` covers the whole screen.

## Shell.html magic

Two non-obvious behaviours in `src/shell.html`:

1. **Browser fingerprint → `?seed=...`.** Hashes UA, screen, timezone,
   language, GPU into 8 hex digits. Same browser → same fractal.
2. **iOS shake permission gate.** A DOM button triggers
   `DeviceMotionEvent.requestPermission()` (Apple requires this from a
   user-tap handler). On grant, JS calls wasm-exported `next_fractal()`.

When scaffolding a new project, copy `doors-dev/src/shell.html` instead
unless you specifically want the fingerprint + shake.

## CI

`.github/workflows/pages.yml` on push to `main`: emsdk 3.1.64 + ccache
+ `./build.sh` + `actions/deploy-pages`. Pages source is "GitHub
Actions" (not gh-pages branch).

## Don't

- Don't commit `build/`.
- Don't reorder `VIZ_REGISTRY` without checking deep links.
- Don't break uniform-block size invariants.

## When to escalate

If a viz needs interactivity, save state, or scheduled reveals,
graduate it into `doors-dev`. The single-page testbed shape is
intentional here.
