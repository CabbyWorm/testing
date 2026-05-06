# Sokol on WebAssembly, served from GitHub Pages

A small [Sokol](https://github.com/floooh/sokol) (sokol_app + sokol_gfx)
application — a spinning, vertex-coloured cube — compiled to WebAssembly with
Emscripten and deployed to GitHub Pages.

The site runs entirely in the browser via WebGL2.

## Live site

After the first successful CI run, the page is served at:

```
https://cabbyworm.github.io/testing/
```

## Layout

```
src/main.c              # spinning cube, sokol_app + sokol_gfx, inline GLES3 shaders
src/shell.html          # custom Emscripten HTML shell
build.sh                # local build helper (also used by CI)
third_party/sokol       # vendored as a git submodule
.github/workflows/pages.yml  # CI: emsdk -> emcc -> deploy-pages
```

## One-time GitHub setup

In the repo's **Settings → Pages**, set **Source = "GitHub Actions"**. After
that, every push to `main` (or to the development branch this repo was
bootstrapped on) builds and deploys automatically.

## Local build

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
activated in the current shell (so `emcc` is on `PATH`).

```sh
git submodule update --init --recursive
./build.sh
python3 -m http.server -d build
# then open http://localhost:8000
```

The build outputs `build/index.html`, `build/index.js`, and `build/index.wasm`.
