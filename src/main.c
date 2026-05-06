// Per-client fractal generator. The browser fingerprint is hashed in
// shell.html and written to the page URL as ?seed=<8 hex>&gpu=<short>.
// sokol_args (third_party/sokol/sokol_args.h:753) parses location.search
// on the emscripten target, so we read the seed straight out via
// sargs_value_def. The seed picks fractal type, palette, julia constant
// and starting view; the same browser always lands on the same fractal.
//
// Rendering is a fullscreen-triangle GLES3 fragment shader. sokol_debugtext
// (third_party/sokol/util/sokol_debugtext.h) draws the on-screen overlay
// without us having to ship a font file.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_args.h"
#include "sokol_debugtext.h"

typedef struct {
    float center[2];
    float julia_c[2];
    float zoom;
    float aspect;
    int   kind;
    int   palette_id;
} fs_params_t;

enum {
    KIND_MANDELBROT = 0,
    KIND_JULIA,
    KIND_BURNINGSHIP,
    KIND_TRICORN,
    KIND_COUNT,
};

static const char *KIND_NAMES[KIND_COUNT] = {
    "mandelbrot", "julia", "burning ship", "tricorn",
};

#define PALETTE_COUNT 6
static const char *PALETTE_NAMES[PALETTE_COUNT] = {
    "ember", "lagoon", "twilight", "sunrise", "moss", "neon",
};

static const float JULIA_CS[][2] = {
    {-0.7f,     0.27015f},
    {-0.8f,     0.156f},
    { 0.285f,   0.0f},
    { 0.285f,   0.01f},
    {-0.4f,     0.6f},
    {-0.835f,  -0.2321f},
    { 0.355f,   0.355f},
    {-0.74543f, 0.11301f},
};
#define JULIA_C_COUNT (sizeof(JULIA_CS) / sizeof(JULIA_CS[0]))

static struct {
    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;
    fs_params_t params;
    fs_params_t initial;     // for the R reset

    bool drag_active;
    float drag_last_x, drag_last_y;
    bool pinch_active;
    float pinch_last_dist, pinch_last_mx, pinch_last_my;

    bool show_overlay;
    uint32_t seed;
    char gpu[80];
} state;

static const char *vs_src =
    "#version 300 es\n"
    "layout(location=0) in vec2 pos;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  gl_Position = vec4(pos, 0.0, 1.0);\n"
    "  v_uv = pos * 0.5;\n"
    "}\n";

static const char *fs_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform vec2 center;\n"
    "uniform vec2 julia_c;\n"
    "uniform float zoom;\n"
    "uniform float aspect;\n"
    "uniform int kind;\n"
    "uniform int palette_id;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "vec3 palette_lookup(float t, int id) {\n"
    "  vec3 a, b, c, d;\n"
    "  if (id == 0)      { a = vec3(0.55,0.30,0.15); b = vec3(0.45,0.45,0.35); c = vec3(1.0,1.0,1.0); d = vec3(0.00,0.10,0.20); }\n"
    "  else if (id == 1) { a = vec3(0.20,0.45,0.55); b = vec3(0.30,0.40,0.50); c = vec3(1.0,1.0,1.0); d = vec3(0.50,0.20,0.25); }\n"
    "  else if (id == 2) { a = vec3(0.50,0.50,0.50); b = vec3(0.50,0.50,0.50); c = vec3(2.0,1.0,0.0); d = vec3(0.50,0.20,0.25); }\n"
    "  else if (id == 3) { a = vec3(0.80,0.50,0.40); b = vec3(0.20,0.40,0.20); c = vec3(2.0,1.0,1.0); d = vec3(0.00,0.25,0.25); }\n"
    "  else if (id == 4) { a = vec3(0.30,0.50,0.30); b = vec3(0.30,0.50,0.40); c = vec3(1.0,1.0,1.0); d = vec3(0.00,0.33,0.67); }\n"
    "  else              { a = vec3(0.50,0.00,0.50); b = vec3(0.50,0.50,0.50); c = vec3(1.0,1.0,0.5); d = vec3(0.80,0.90,0.30); }\n"
    "  return a + b * cos(6.28318530718 * (c * t + d));\n"
    "}\n"
    "void main() {\n"
    "  vec2 uv = vec2(v_uv.x * aspect, v_uv.y) * zoom + center;\n"
    "  vec2 z, c;\n"
    "  if (kind == 1) { z = uv; c = julia_c; }\n"
    "  else           { z = vec2(0.0); c = uv; }\n"
    "  const int MAX_ITER = 200;\n"
    "  int n = MAX_ITER;\n"
    "  for (int i = 0; i < MAX_ITER; i++) {\n"
    "    float x = z.x;\n"
    "    float y = z.y;\n"
    "    if (kind == 2) { x = abs(x); y = abs(y); }\n"
    "    if (kind == 3) { y = -y; }\n"
    "    z = vec2(x*x - y*y, 2.0*x*y) + c;\n"
    "    if (dot(z, z) > 256.0) { n = i; break; }\n"
    "  }\n"
    "  if (n >= MAX_ITER) {\n"
    "    frag_color = vec4(0.02, 0.02, 0.04, 1.0);\n"
    "  } else {\n"
    "    float mu = float(n) + 1.0 - log2(0.5 * log2(max(dot(z, z), 1.0001)));\n"
    "    float t = clamp(mu / float(MAX_ITER), 0.0, 1.0);\n"
    "    frag_color = vec4(palette_lookup(sqrt(t), palette_id), 1.0);\n"
    "  }\n"
    "}\n";

static uint32_t parse_seed_hex(const char *s) {
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return (uint32_t)strtoul(s, NULL, 16);
}

static void seed_to_initial_state(uint32_t seed, fs_params_t *p) {
    p->kind       = (int)(seed % KIND_COUNT);
    p->palette_id = (int)((seed >> 4) % PALETTE_COUNT);
    int julia_idx = (int)((seed >> 8) % JULIA_C_COUNT);
    p->julia_c[0] = JULIA_CS[julia_idx][0];
    p->julia_c[1] = JULIA_CS[julia_idx][1];

    float jx = (float)((seed >> 12) & 0xff) / 255.0f - 0.5f;
    float jy = (float)((seed >> 20) & 0xff) / 255.0f - 0.5f;

    switch (p->kind) {
        case KIND_MANDELBROT:
            p->center[0] = -0.5f + 0.4f * jx;
            p->center[1] =  0.0f + 0.4f * jy;
            break;
        case KIND_JULIA:
            p->center[0] = 0.0f + 0.3f * jx;
            p->center[1] = 0.0f + 0.3f * jy;
            break;
        case KIND_BURNINGSHIP:
            p->center[0] = -0.4f + 0.3f * jx;
            p->center[1] = -0.5f + 0.3f * jy;
            break;
        default:
            p->center[0] = 0.0f + 0.4f * jx;
            p->center[1] = 0.0f + 0.4f * jy;
            break;
    }
    p->zoom = 3.0f;
}

static void apply_url_overrides(fs_params_t *p) {
    if (sargs_exists("type")) {
        const char *t = sargs_value("type");
        if      (strcmp(t, "mandelbrot")  == 0) p->kind = KIND_MANDELBROT;
        else if (strcmp(t, "julia")       == 0) p->kind = KIND_JULIA;
        else if (strcmp(t, "burningship") == 0) p->kind = KIND_BURNINGSHIP;
        else if (strcmp(t, "tricorn")     == 0) p->kind = KIND_TRICORN;
    }
    if (sargs_exists("palette")) p->palette_id = atoi(sargs_value("palette")) % PALETTE_COUNT;
    if (sargs_exists("zoom"))    p->zoom       = (float)atof(sargs_value("zoom"));
    if (sargs_exists("cx"))      p->center[0]  = (float)atof(sargs_value("cx"));
    if (sargs_exists("cy"))      p->center[1]  = (float)atof(sargs_value("cy"));
    if (sargs_exists("jx"))      p->julia_c[0] = (float)atof(sargs_value("jx"));
    if (sargs_exists("jy"))      p->julia_c[1] = (float)atof(sargs_value("jy"));
}

static void init(void) {
    sargs_setup(&(sargs_desc){ .logger.func = slog_func });
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = slog_func });
    sdtx_setup(&(sdtx_desc_t){
        .fonts = { [0] = sdtx_font_kc853() },
        .logger.func = slog_func,
    });

    state.seed = parse_seed_hex(sargs_value_def("seed", "00000000"));
    seed_to_initial_state(state.seed, &state.params);
    apply_url_overrides(&state.params);
    state.initial = state.params;

    const char *g = sargs_value_def("gpu", "");
    strncpy(state.gpu, g, sizeof(state.gpu) - 1);
    state.gpu[sizeof(state.gpu) - 1] = 0;
    state.show_overlay = true;

    float verts[] = { -1.0f, -3.0f, -1.0f, 1.0f, 3.0f, 1.0f };
    state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(verts), .label = "fullscreen-tri",
    });

    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = vs_src,
        .fragment_func.source = fs_src,
        .attrs = { [0] = { .glsl_name = "pos" } },
        .uniform_blocks[0] = {
            .stage  = SG_SHADERSTAGE_FRAGMENT,
            .size   = sizeof(fs_params_t),
            .layout = SG_UNIFORMLAYOUT_NATIVE,
            .glsl_uniforms = {
                [0] = { .glsl_name = "center",     .type = SG_UNIFORMTYPE_FLOAT2 },
                [1] = { .glsl_name = "julia_c",    .type = SG_UNIFORMTYPE_FLOAT2 },
                [2] = { .glsl_name = "zoom",       .type = SG_UNIFORMTYPE_FLOAT  },
                [3] = { .glsl_name = "aspect",     .type = SG_UNIFORMTYPE_FLOAT  },
                [4] = { .glsl_name = "kind",       .type = SG_UNIFORMTYPE_INT    },
                [5] = { .glsl_name = "palette_id", .type = SG_UNIFORMTYPE_INT    },
            },
        },
        .label = "fractal-shader",
    });

    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .layout = { .attrs = { [0].format = SG_VERTEXFORMAT_FLOAT2 } },
        .label = "fractal-pipeline",
    });

    state.pass_action = (sg_pass_action){
        .colors[0] = { .load_action = SG_LOADACTION_DONTCARE },
    };
}

static void pan(float dx_px, float dy_px) {
    float s = state.params.zoom / sapp_heightf();
    state.params.center[0] -= dx_px * s;
    state.params.center[1] += dy_px * s;
}

static void zoom_about_pixel(float factor, float px, float py) {
    float aspect = state.params.aspect;
    float old_zoom = state.params.zoom;
    float new_zoom = old_zoom * factor;
    if (new_zoom < 1e-6f) new_zoom = 1e-6f;
    if (new_zoom > 8.0f)  new_zoom = 8.0f;
    float u = (px / sapp_widthf()) - 0.5f;
    float v = 0.5f - (py / sapp_heightf());
    state.params.center[0] += u * aspect * (old_zoom - new_zoom);
    state.params.center[1] += v * (old_zoom - new_zoom);
    state.params.zoom = new_zoom;
}

static void event(const sapp_event *e) {
    switch (e->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                state.drag_active = true;
                state.drag_last_x = e->mouse_x;
                state.drag_last_y = e->mouse_y;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            if (state.drag_active) {
                pan(e->mouse_x - state.drag_last_x, e->mouse_y - state.drag_last_y);
                state.drag_last_x = e->mouse_x;
                state.drag_last_y = e->mouse_y;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) state.drag_active = false;
            break;
        case SAPP_EVENTTYPE_MOUSE_LEAVE:
            state.drag_active = false;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL: {
            float factor = expf(-e->scroll_y * 0.15f);
            zoom_about_pixel(factor, e->mouse_x, e->mouse_y);
            break;
        }
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
            if (e->num_touches >= 2) {
                state.drag_active = false;
                state.pinch_active = true;
                float dx = e->touches[1].pos_x - e->touches[0].pos_x;
                float dy = e->touches[1].pos_y - e->touches[0].pos_y;
                state.pinch_last_dist = sqrtf(dx*dx + dy*dy);
                state.pinch_last_mx = (e->touches[0].pos_x + e->touches[1].pos_x) * 0.5f;
                state.pinch_last_my = (e->touches[0].pos_y + e->touches[1].pos_y) * 0.5f;
            } else if (e->num_touches == 1) {
                state.pinch_active = false;
                state.drag_active = true;
                state.drag_last_x = e->touches[0].pos_x;
                state.drag_last_y = e->touches[0].pos_y;
            }
            break;
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
            if (state.pinch_active && e->num_touches >= 2) {
                float dx = e->touches[1].pos_x - e->touches[0].pos_x;
                float dy = e->touches[1].pos_y - e->touches[0].pos_y;
                float dist = sqrtf(dx*dx + dy*dy);
                float mx = (e->touches[0].pos_x + e->touches[1].pos_x) * 0.5f;
                float my = (e->touches[0].pos_y + e->touches[1].pos_y) * 0.5f;
                if (state.pinch_last_dist > 1e-3f) {
                    float ratio = dist / state.pinch_last_dist;
                    zoom_about_pixel(1.0f / ratio, mx, my);
                    pan(mx - state.pinch_last_mx, my - state.pinch_last_my);
                }
                state.pinch_last_dist = dist;
                state.pinch_last_mx = mx;
                state.pinch_last_my = my;
            } else if (state.drag_active && e->num_touches >= 1) {
                float x = e->touches[0].pos_x;
                float y = e->touches[0].pos_y;
                pan(x - state.drag_last_x, y - state.drag_last_y);
                state.drag_last_x = x;
                state.drag_last_y = y;
            }
            break;
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
            if (e->num_touches == 0) {
                state.drag_active = false;
                state.pinch_active = false;
            } else if (state.pinch_active && e->num_touches < 2) {
                state.pinch_active = false;
                state.drag_active = true;
                state.drag_last_x = e->touches[0].pos_x;
                state.drag_last_y = e->touches[0].pos_y;
            } else if (state.drag_active && e->num_touches >= 1) {
                state.drag_last_x = e->touches[0].pos_x;
                state.drag_last_y = e->touches[0].pos_y;
            }
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (e->key_code == SAPP_KEYCODE_R) {
                state.params = state.initial;
            } else if (e->key_code == SAPP_KEYCODE_H) {
                state.show_overlay = !state.show_overlay;
            }
            break;
        default:
            break;
    }
}

static void frame(void) {
    state.params.aspect = sapp_widthf() / sapp_heightf();

    sg_begin_pass(&(sg_pass){ .action = state.pass_action, .swapchain = sglue_swapchain() });
    sg_apply_pipeline(state.pip);
    sg_apply_bindings(&state.bind);
    sg_apply_uniforms(0, &SG_RANGE(state.params));
    sg_draw(0, 3, 1);

    if (state.show_overlay) {
        sdtx_canvas(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f);
        sdtx_origin(1.0f, 1.0f);
        sdtx_home();
        sdtx_font(0);
        sdtx_color3b(0xd8, 0xe0, 0xe6);
        sdtx_printf("YOUR SOKOL FRACTAL\n");
        sdtx_color3b(0x80, 0x88, 0x90);
        sdtx_printf("\n");
        sdtx_printf("seed     %08x\n", state.seed);
        sdtx_printf("type     %s\n", KIND_NAMES[state.params.kind]);
        sdtx_printf("palette  %s\n", PALETTE_NAMES[state.params.palette_id]);
        if (state.params.kind == KIND_JULIA) {
            sdtx_printf("c        %+0.5f %+0.5fi\n",
                        (double)state.params.julia_c[0],
                        (double)state.params.julia_c[1]);
        }
        sdtx_printf("center   %+0.6f %+0.6fi\n",
                    (double)state.params.center[0],
                    (double)state.params.center[1]);
        sdtx_printf("zoom     %.4gx\n", (double)(3.0f / state.params.zoom));
        sdtx_printf("gpu      %s\n", state.gpu[0] ? state.gpu : "?");
        sdtx_color3b(0x50, 0x58, 0x60);
        sdtx_printf("\ndrag pan  scroll/pinch zoom  R reset  H hide");
        sdtx_draw();
    }

    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    sdtx_shutdown();
    sg_shutdown();
    sargs_shutdown();
}

sapp_desc sokol_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .event_cb = event,
        .cleanup_cb = cleanup,
        .width = 1024,
        .height = 720,
        .sample_count = 1,
        .window_title = "your sokol fractal",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
