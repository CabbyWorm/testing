// Raymarched 3D fractal visualization. Reads the URL-derived seed from
// main.c via main_get_seed() and turns it into a fractal kind, palette,
// kind-parameter, and starting framing — same browser, same fractal.
#include "viz_fractal.h"

typedef struct {
    float cam_pos[3];
    float cam_target[3];
    float aspect;
    float param;
    float fov_scale;
    int   kind;
    int   palette_id;
    int   iter_boost;
} fractal_params_t;

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
    "uniform float fov_scale;\n"
    "uniform int kind;\n"
    "uniform int palette_id;\n"
    "uniform int iter_boost;\n"
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
    "  for (int i = 0; i < 24; i++) {\n"
    "    if (i >= 10 + iter_boost) break;\n"
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
    "  for (int i = 0; i < 24; i++) {\n"
    "    if (i >= 12 + iter_boost) break;\n"
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
    "  int n = 0;\n"
    "  for (int i = 0; i < 24; i++) {\n"
    "    if (i >= 12 + iter_boost) break;\n"
    "    if (p.x + p.y < 0.0) p.xy = -p.yx;\n"
    "    if (p.x + p.z < 0.0) p.xz = -p.zx;\n"
    "    if (p.y + p.z < 0.0) p.yz = -p.zy;\n"
    "    p = p * scale - vec3(scale - 1.0);\n"
    "    n = i + 1;\n"
    "  }\n"
    "  return (length(p) - 2.0) * pow(scale, -float(n));\n"
    "}\n"
    "\n"
    "float DE_menger(vec3 p, float fold) {\n"
    "  vec3 b = vec3(1.0);\n"
    "  vec3 q = abs(p / fold) - b;\n"
    "  float d = (length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0)) * fold;\n"
    "  float s = 1.0;\n"
    "  for (int i = 0; i < 14; i++) {\n"
    "    if (i >= 5 + iter_boost) break;\n"
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
    "  float h = 0.0006 / fov_scale;\n"
    "  vec2 e = vec2(h, 0.0);\n"
    "  return normalize(vec3(\n"
    "    DE(p + e.xyy) - DE(p - e.xyy),\n"
    "    DE(p + e.yxy) - DE(p - e.yxy),\n"
    "    DE(p + e.yyx) - DE(p - e.yyx)));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "  vec2 uv = vec2(v_uv.x * 2.0 * aspect, v_uv.y * 2.0) / fov_scale;\n"
    "\n"
    "  vec3 fwd = normalize(cam_target - cam_pos);\n"
    "  vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));\n"
    "  vec3 up = cross(right, fwd);\n"
    "  vec3 dir = normalize(fwd + right * uv.x + up * uv.y);\n"
    "\n"
    "  const int MAX_STEPS = 160;\n"
    "  float t = 0.0;\n"
    "  bool hit = false;\n"
    "  int steps = 0;\n"
    "  for (int i = 0; i < MAX_STEPS; i++) {\n"
    "    steps = i;\n"
    "    vec3 p = cam_pos + dir * t;\n"
    "    float d = DE(p);\n"
    "    if (d < 0.0008 * (t + 0.0005) / fov_scale) { hit = true; break; }\n"
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

static struct {
    sg_pipeline pip;
    fractal_params_t params;
} fractal;

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

static void seed_params(uint32_t seed) {
    fractal.params.kind       = (int)(seed % KIND_COUNT);
    fractal.params.palette_id = (int)((seed >> 4) % PALETTE_COUNT);
    fractal.params.param      = param_for_kind(fractal.params.kind, seed >> 8);
}

static void fractal_init(void) {
    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source   = vs_src,
        .fragment_func.source = fs_src,
        .attrs = { [0] = { .glsl_name = "pos" } },
        .uniform_blocks[0] = {
            .stage  = SG_SHADERSTAGE_FRAGMENT,
            .size   = sizeof(fractal_params_t),
            .layout = SG_UNIFORMLAYOUT_NATIVE,
            .glsl_uniforms = {
                [0] = { .glsl_name = "cam_pos",    .type = SG_UNIFORMTYPE_FLOAT3 },
                [1] = { .glsl_name = "cam_target", .type = SG_UNIFORMTYPE_FLOAT3 },
                [2] = { .glsl_name = "aspect",     .type = SG_UNIFORMTYPE_FLOAT  },
                [3] = { .glsl_name = "param",      .type = SG_UNIFORMTYPE_FLOAT  },
                [4] = { .glsl_name = "fov_scale",  .type = SG_UNIFORMTYPE_FLOAT  },
                [5] = { .glsl_name = "kind",       .type = SG_UNIFORMTYPE_INT    },
                [6] = { .glsl_name = "palette_id", .type = SG_UNIFORMTYPE_INT    },
                [7] = { .glsl_name = "iter_boost", .type = SG_UNIFORMTYPE_INT    },
            },
        },
        .label = "fractal-shader",
    });

    fractal.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .layout = { .attrs = { [0].format = SG_VERTEXFORMAT_FLOAT2 } },
        .label  = "fractal-pipeline",
    });

    // Seed-derived defaults so the params are populated even if the user
    // boots into a different viz and toggles to fractal later.
    // url_overrides() runs after init() and may override these.
    seed_params(main_get_seed());
}

static void fractal_apply_defaults(camera_t *cam) {
    // Re-derive params and camera home from the current seed. Called at
    // boot for the active viz, and from next_fractal() on shake-to-switch.
    uint32_t seed = main_get_seed();
    seed_params(seed);

    float jx = (float)((seed >> 16) & 0xff) / 255.0f - 0.5f;
    float jy = (float)((seed >> 24) & 0xff) / 255.0f - 0.5f;
    cam->yaw      = jx * 2.0f * PI;
    cam->pitch    = jy * 0.6f;
    cam->distance = distance_for_kind(fractal.params.kind);
    cam->yaw0      = cam->yaw;
    cam->pitch0    = cam->pitch;
    cam->distance0 = cam->distance;
}

static void fractal_url_overrides(void) {
    if (sargs_exists("type")) {
        const char *t = sargs_value("type");
        if      (strcmp(t, "mandelbulb") == 0) fractal.params.kind = KIND_MANDELBULB;
        else if (strcmp(t, "mandelbox")  == 0) fractal.params.kind = KIND_MANDELBOX;
        else if (strcmp(t, "sierpinski") == 0) fractal.params.kind = KIND_SIERPINSKI;
        else if (strcmp(t, "menger")     == 0) fractal.params.kind = KIND_MENGER;
    }
    if (sargs_exists("palette")) fractal.params.palette_id = atoi(sargs_value("palette")) % PALETTE_COUNT;
    if (sargs_exists("power"))   fractal.params.param = (float)atof(sargs_value("power"));
    if (sargs_exists("scale"))   fractal.params.param = (float)atof(sargs_value("scale"));
}

static void fractal_draw(const camera_t *cam, double time_accum) {
    (void)time_accum;
    compute_camera_basis(cam, fractal.params.cam_pos, fractal.params.cam_target, &fractal.params.fov_scale);
    fractal.params.aspect = sapp_widthf() / sapp_heightf();

    float zoom_decades = log2f(fractal.params.fov_scale);
    int boost = (int)(zoom_decades * 1.5f);
    if (boost < 0)  boost = 0;
    if (boost > 12) boost = 12;
    fractal.params.iter_boost = boost;

    sg_apply_pipeline(fractal.pip);
    sg_bindings bind = (sg_bindings){ .vertex_buffers[0] = fullscreen_tri_buffer() };
    sg_apply_bindings(&bind);
    sg_apply_uniforms(0, &SG_RANGE(fractal.params));
    sg_draw(0, 3, 1);
}

static void fractal_overlay(void) {
    sdtx_color3b(0x80, 0x88, 0x90);
    sdtx_printf("seed     %08x\n", main_get_seed());
    sdtx_printf("type     %s\n", KIND_NAMES[fractal.params.kind]);
    sdtx_printf("palette  %s\n", PALETTE_NAMES[fractal.params.palette_id]);
    sdtx_printf("%-8s %.3f\n", PARAM_LABELS[fractal.params.kind], (double)fractal.params.param);
    sdtx_printf("iters    +%d boost\n", fractal.params.iter_boost);
}

const viz_iface VIZ_FRACTAL_IFACE = {
    .name               = "fractals",
    .url_token          = "fractal",
    .init               = fractal_init,
    .apply_defaults     = fractal_apply_defaults,
    .url_overrides      = fractal_url_overrides,
    .draw               = fractal_draw,
    .overlay            = fractal_overlay,
    .supports_auto_zoom = true,
};
