// Per-client 3D fractal generator. The browser fingerprint is hashed in
// shell.html and written to the page URL as ?seed=<8 hex>&gpu=<short>.
// sokol_args (third_party/sokol/sokol_args.h:753) parses location.search on
// the emscripten target, so we read the seed straight out via
// sargs_value_def. The seed picks fractal kind, palette, kind parameter,
// and starting framing; the same browser always lands on the same fractal.
//
// Rendering is a fullscreen-triangle GLES3 fragment shader that raymarches
// a distance-estimated 3D fractal — Mandelbulb, Mandelbox, Sierpinski
// tetrahedron, or Menger sponge. sokol_debugtext draws the on-screen
// overlay using its built-in CGA font (no external font files).

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

#define PI 3.14159265358979323846f

typedef struct {
    float cam_pos[3];
    float cam_target[3];
    float aspect;
    float param;
    int   kind;
    int   palette_id;
} fs_params_t;

enum {
    KIND_MANDELBULB = 0,
    KIND_MANDELBOX,
    KIND_SIERPINSKI,
    KIND_MENGER,
    KIND_COUNT,
};

static const char *KIND_NAMES[KIND_COUNT] = {
    "mandelbulb", "mandelbox", "sierpinski", "menger sponge",
};

static const char *PARAM_LABELS[KIND_COUNT] = {
    "power", "scale", "scale", "fold",
};

#define PALETTE_COUNT 6
static const char *PALETTE_NAMES[PALETTE_COUNT] = {
    "ember", "lagoon", "twilight", "sunrise", "moss", "neon",
};

static struct {
    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;
    fs_params_t params;

    float yaw, pitch, distance;
    float yaw0, pitch0, distance0;

    bool drag_active;
    float drag_last_x, drag_last_y;
    bool pinch_active;
    float pinch_last_dist;

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
    "uniform vec3 cam_pos;\n"
    "uniform vec3 cam_target;\n"
    "uniform float aspect;\n"
    "uniform float param;\n"
    "uniform int kind;\n"
    "uniform int palette_id;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "\n"
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
    "\n"
    "float DE_mandelbulb(vec3 p, float power) {\n"
    "  vec3 z = p;\n"
    "  float dr = 1.0;\n"
    "  float r = 0.0;\n"
    "  for (int i = 0; i < 10; i++) {\n"
    "    r = length(z);\n"
    "    if (r > 2.0) break;\n"
    "    float theta = acos(clamp(z.z / r, -1.0, 1.0));\n"
    "    float phi = atan(z.y, z.x);\n"
    "    dr = pow(r, power - 1.0) * power * dr + 1.0;\n"
    "    float zr = pow(r, power);\n"
    "    theta *= power;\n"
    "    phi *= power;\n"
    "    z = zr * vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));\n"
    "    z += p;\n"
    "  }\n"
    "  return 0.5 * log(max(r, 1e-6)) * r / dr;\n"
    "}\n"
    "\n"
    "float DE_mandelbox(vec3 p, float scale) {\n"
    "  vec3 offset = p;\n"
    "  vec3 z = p;\n"
    "  float dr = 1.0;\n"
    "  for (int i = 0; i < 12; i++) {\n"
    "    z = clamp(z, -1.0, 1.0) * 2.0 - z;\n"
    "    float r2 = dot(z, z);\n"
    "    if (r2 < 0.25) { z *= 4.0; dr *= 4.0; }\n"
    "    else if (r2 < 1.0) { float k = 1.0 / r2; z *= k; dr *= k; }\n"
    "    z = scale * z + offset;\n"
    "    dr = abs(scale) * dr + 1.0;\n"
    "  }\n"
    "  return length(z) / abs(dr);\n"
    "}\n"
    "\n"
    "float DE_sierpinski(vec3 p, float scale) {\n"
    "  for (int i = 0; i < 12; i++) {\n"
    "    if (p.x + p.y < 0.0) p.xy = -p.yx;\n"
    "    if (p.x + p.z < 0.0) p.xz = -p.zx;\n"
    "    if (p.y + p.z < 0.0) p.yz = -p.zy;\n"
    "    p = p * scale - vec3(scale - 1.0);\n"
    "  }\n"
    "  return (length(p) - 2.0) * pow(scale, -12.0);\n"
    "}\n"
    "\n"
    "float DE_menger(vec3 p, float fold) {\n"
    "  vec3 b = vec3(1.0);\n"
    "  vec3 q = abs(p / fold) - b;\n"
    "  float d = (length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0)) * fold;\n"
    "  float s = 1.0;\n"
    "  for (int i = 0; i < 5; i++) {\n"
    "    vec3 a = mod(p * s, 2.0 * fold) - fold;\n"
    "    s *= 3.0;\n"
    "    vec3 r = abs(1.0 - 3.0 * abs(a) / fold);\n"
    "    float c = (min(max(r.x, r.y), min(max(r.y, r.z), max(r.z, r.x))) - 1.0) * fold / s;\n"
    "    d = max(d, c);\n"
    "  }\n"
    "  return d;\n"
    "}\n"
    "\n"
    "float DE(vec3 p) {\n"
    "  if (kind == 0) return DE_mandelbulb(p, param);\n"
    "  if (kind == 1) return DE_mandelbox(p, param);\n"
    "  if (kind == 2) return DE_sierpinski(p, param);\n"
    "  return DE_menger(p, param);\n"
    "}\n"
    "\n"
    "vec3 estimate_normal(vec3 p) {\n"
    "  const vec2 e = vec2(0.0006, 0.0);\n"
    "  return normalize(vec3(\n"
    "    DE(p + e.xyy) - DE(p - e.xyy),\n"
    "    DE(p + e.yxy) - DE(p - e.yxy),\n"
    "    DE(p + e.yyx) - DE(p - e.yyx)));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "  vec2 uv = vec2(v_uv.x * 2.0 * aspect, v_uv.y * 2.0);\n"
    "\n"
    "  vec3 fwd = normalize(cam_target - cam_pos);\n"
    "  vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));\n"
    "  vec3 up = cross(right, fwd);\n"
    "  vec3 dir = normalize(fwd + right * uv.x + up * uv.y);\n"
    "\n"
    "  const int MAX_STEPS = 96;\n"
    "  float t = 0.0;\n"
    "  bool hit = false;\n"
    "  int steps = 0;\n"
    "  for (int i = 0; i < MAX_STEPS; i++) {\n"
    "    steps = i;\n"
    "    vec3 p = cam_pos + dir * t;\n"
    "    float d = DE(p);\n"
    "    if (d < 0.0008 * max(t, 1.0)) { hit = true; break; }\n"
    "    if (t > 30.0) break;\n"
    "    t += d * 0.95;\n"
    "  }\n"
    "\n"
    "  if (hit) {\n"
    "    vec3 p = cam_pos + dir * t;\n"
    "    vec3 n = estimate_normal(p);\n"
    "    vec3 light_dir = normalize(vec3(0.5, 0.7, 0.3));\n"
    "    float ndl = max(0.0, dot(n, light_dir));\n"
    "    float ao = 1.0 - float(steps) / float(MAX_STEPS);\n"
    "    vec3 albedo = palette_lookup(ao, palette_id);\n"
    "    vec3 col = albedo * (0.18 + 0.82 * ndl) * (0.45 + 0.55 * ao);\n"
    "    float rim = pow(1.0 - max(0.0, dot(n, -dir)), 2.0);\n"
    "    col += rim * 0.18 * vec3(0.65, 0.75, 0.95);\n"
    "    float fog = 1.0 - exp(-t * 0.04);\n"
    "    col = mix(col, vec3(0.04, 0.05, 0.08), fog * 0.5);\n"
    "    frag_color = vec4(col, 1.0);\n"
    "  } else {\n"
    "    float vy = clamp(0.5 + dir.y * 0.5, 0.0, 1.0);\n"
    "    vec3 sky = mix(vec3(0.02, 0.02, 0.04), vec3(0.05, 0.06, 0.10), vy);\n"
    "    frag_color = vec4(sky, 1.0);\n"
    "  }\n"
    "}\n";

static uint32_t parse_seed_hex(const char *s) {
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return (uint32_t)strtoul(s, NULL, 16);
}

static float param_for_kind(int kind, uint32_t bits) {
    switch (kind) {
        case KIND_MANDELBULB: {
            int t = (int)(bits % 12);
            if (t < 6)        return 8.0f;
            else if (t < 8)   return 7.0f;
            else if (t < 10)  return 9.0f;
            else if (t == 10) return 5.0f;
            else              return 4.0f;
        }
        case KIND_MANDELBOX:
            return -2.0f + ((float)(bits & 0xff) / 255.0f) * 0.5f;
        case KIND_SIERPINSKI:
            return 1.7f + ((float)(bits & 0xff) / 255.0f) * 0.5f;
        case KIND_MENGER:
        default:
            return 1.0f;
    }
}

static float distance_for_kind(int kind) {
    switch (kind) {
        case KIND_MANDELBULB: return 2.6f;
        case KIND_MANDELBOX:  return 4.5f;
        case KIND_SIERPINSKI: return 3.2f;
        case KIND_MENGER:     return 3.8f;
        default:              return 3.0f;
    }
}

static void seed_to_initial_state(uint32_t seed) {
    state.params.kind       = (int)(seed % KIND_COUNT);
    state.params.palette_id = (int)((seed >> 4) % PALETTE_COUNT);
    state.params.param      = param_for_kind(state.params.kind, seed >> 8);

    float jx = (float)((seed >> 16) & 0xff) / 255.0f - 0.5f;
    float jy = (float)((seed >> 24) & 0xff) / 255.0f - 0.5f;
    state.yaw      = jx * 2.0f * PI;
    state.pitch    = jy * 0.6f;
    state.distance = distance_for_kind(state.params.kind);
}

static void apply_url_overrides(void) {
    if (sargs_exists("type")) {
        const char *t = sargs_value("type");
        if      (strcmp(t, "mandelbulb") == 0) state.params.kind = KIND_MANDELBULB;
        else if (strcmp(t, "mandelbox")  == 0) state.params.kind = KIND_MANDELBOX;
        else if (strcmp(t, "sierpinski") == 0) state.params.kind = KIND_SIERPINSKI;
        else if (strcmp(t, "menger")     == 0) state.params.kind = KIND_MENGER;
    }
    if (sargs_exists("palette")) state.params.palette_id = atoi(sargs_value("palette")) % PALETTE_COUNT;
    if (sargs_exists("power"))   state.params.param = (float)atof(sargs_value("power"));
    if (sargs_exists("scale"))   state.params.param = (float)atof(sargs_value("scale"));
    if (sargs_exists("dist"))    state.distance = (float)atof(sargs_value("dist"));
    if (sargs_exists("yaw"))     state.yaw = (float)atof(sargs_value("yaw"));
    if (sargs_exists("pitch"))   state.pitch = (float)atof(sargs_value("pitch"));
}

static void update_cam_pos(void) {
    float cp = cosf(state.pitch);
    state.params.cam_pos[0] = state.distance * cp * sinf(state.yaw);
    state.params.cam_pos[1] = state.distance * sinf(state.pitch);
    state.params.cam_pos[2] = state.distance * cp * cosf(state.yaw);
    state.params.cam_target[0] = 0.0f;
    state.params.cam_target[1] = 0.0f;
    state.params.cam_target[2] = 0.0f;
}

static void init(void) {
    sargs_setup(&(sargs_desc){ 0 });
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = slog_func });
    sdtx_setup(&(sdtx_desc_t){
        .fonts = { [0] = sdtx_font_kc853() },
        .logger.func = slog_func,
    });

    state.seed = parse_seed_hex(sargs_value_def("seed", "00000000"));
    seed_to_initial_state(state.seed);
    apply_url_overrides();
    state.yaw0      = state.yaw;
    state.pitch0    = state.pitch;
    state.distance0 = state.distance;

    const char *g = sargs_value_def("gpu", "");
    strncpy(state.gpu, g, sizeof(state.gpu) - 1);
    state.gpu[sizeof(state.gpu) - 1] = 0;
    state.show_overlay = true;

    float verts[] = { -1.0f, -3.0f, -1.0f, 1.0f, 3.0f, 1.0f };
    state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(verts), .label = "fullscreen-tri",
    });

    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source   = vs_src,
        .fragment_func.source = fs_src,
        .attrs = { [0] = { .glsl_name = "pos" } },
        .uniform_blocks[0] = {
            .stage  = SG_SHADERSTAGE_FRAGMENT,
            .size   = sizeof(fs_params_t),
            .layout = SG_UNIFORMLAYOUT_NATIVE,
            .glsl_uniforms = {
                [0] = { .glsl_name = "cam_pos",    .type = SG_UNIFORMTYPE_FLOAT3 },
                [1] = { .glsl_name = "cam_target", .type = SG_UNIFORMTYPE_FLOAT3 },
                [2] = { .glsl_name = "aspect",     .type = SG_UNIFORMTYPE_FLOAT  },
                [3] = { .glsl_name = "param",      .type = SG_UNIFORMTYPE_FLOAT  },
                [4] = { .glsl_name = "kind",       .type = SG_UNIFORMTYPE_INT    },
                [5] = { .glsl_name = "palette_id", .type = SG_UNIFORMTYPE_INT    },
            },
        },
        .label = "fractal-shader",
    });

    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .layout = { .attrs = { [0].format = SG_VERTEXFORMAT_FLOAT2 } },
        .label  = "fractal-pipeline",
    });

    state.pass_action = (sg_pass_action){
        .colors[0] = { .load_action = SG_LOADACTION_DONTCARE },
    };
}

static void orbit(float dx_px, float dy_px) {
    const float kSensitivity = 0.005f;
    state.yaw   -= dx_px * kSensitivity;
    state.pitch += dy_px * kSensitivity;
    const float lim = PI * 0.5f - 0.01f;
    if (state.pitch >  lim) state.pitch =  lim;
    if (state.pitch < -lim) state.pitch = -lim;
}

static void dolly(float factor) {
    state.distance *= factor;
    if (state.distance < 1.2f)  state.distance = 1.2f;
    if (state.distance > 10.0f) state.distance = 10.0f;
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
                orbit(e->mouse_x - state.drag_last_x, e->mouse_y - state.drag_last_y);
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
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            dolly(expf(-e->scroll_y * 0.15f));
            break;
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
            if (e->num_touches >= 2) {
                state.drag_active = false;
                state.pinch_active = true;
                float dx = e->touches[1].pos_x - e->touches[0].pos_x;
                float dy = e->touches[1].pos_y - e->touches[0].pos_y;
                state.pinch_last_dist = sqrtf(dx*dx + dy*dy);
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
                if (state.pinch_last_dist > 1e-3f) {
                    dolly(state.pinch_last_dist / dist);
                }
                state.pinch_last_dist = dist;
            } else if (state.drag_active && e->num_touches >= 1) {
                float x = e->touches[0].pos_x;
                float y = e->touches[0].pos_y;
                orbit(x - state.drag_last_x, y - state.drag_last_y);
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
                state.yaw      = state.yaw0;
                state.pitch    = state.pitch0;
                state.distance = state.distance0;
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
    update_cam_pos();

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
        sdtx_printf("%-8s %.3f\n", PARAM_LABELS[state.params.kind], (double)state.params.param);
        sdtx_printf("camera   yaw %+0.3f  pitch %+0.3f  d %.2f\n",
                    (double)state.yaw, (double)state.pitch, (double)state.distance);
        sdtx_printf("gpu      %s\n", state.gpu[0] ? state.gpu : "?");
        sdtx_color3b(0x50, 0x58, 0x60);
        sdtx_printf("\ndrag orbit  scroll/pinch dolly  R reset  H hide");
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
