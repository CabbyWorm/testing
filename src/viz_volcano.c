// Realistic-ish 3D raymarched volcanic eruption. Reuses the orbit camera
// from common.h so drag/scroll behave identically to the fractal viz.
// SDF capped cone for the rock, a flat ground plane below it, and a
// volumetric ash plume layered on top of the surface raymarch.
#include "viz_volcano.h"

typedef struct {
    float cam_pos[3];
    float cam_target[3];
    float aspect;
    float fov_scale;
    float time;
    float eruption;
} volcano_params_t;

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
    "uniform float fov_scale;\n"
    "uniform float time;\n"
    "uniform float eruption;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "\n"
    "float hash13(vec3 p) {\n"
    "  p = fract(p * 0.1031);\n"
    "  p += dot(p, p.yzx + 33.33);\n"
    "  return fract((p.x + p.y) * p.z);\n"
    "}\n"
    "float hash12(vec2 p) {\n"
    "  vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "  p3 += dot(p3, p3.yzx + 33.33);\n"
    "  return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "\n"
    "float vnoise(vec3 p) {\n"
    "  vec3 i = floor(p);\n"
    "  vec3 f = fract(p);\n"
    "  f = f * f * (3.0 - 2.0 * f);\n"
    "  float n000 = hash13(i + vec3(0,0,0));\n"
    "  float n100 = hash13(i + vec3(1,0,0));\n"
    "  float n010 = hash13(i + vec3(0,1,0));\n"
    "  float n110 = hash13(i + vec3(1,1,0));\n"
    "  float n001 = hash13(i + vec3(0,0,1));\n"
    "  float n101 = hash13(i + vec3(1,0,1));\n"
    "  float n011 = hash13(i + vec3(0,1,1));\n"
    "  float n111 = hash13(i + vec3(1,1,1));\n"
    "  return mix(mix(mix(n000,n100,f.x), mix(n010,n110,f.x), f.y),\n"
    "             mix(mix(n001,n101,f.x), mix(n011,n111,f.x), f.y), f.z);\n"
    "}\n"
    "\n"
    "float fbm(vec3 p) {\n"
    "  float a = 0.5, s = 0.0;\n"
    "  for (int i = 0; i < 5; i++) {\n"
    "    s += a * vnoise(p);\n"
    "    p *= 2.03;\n"
    "    a *= 0.5;\n"
    "  }\n"
    "  return s;\n"
    "}\n"
    "\n"
    // IQ capped cone, axis along Y from -h to +h. r_base at y=-h,
    // r_top at y=+h. https://iquilezles.org/articles/distfunctions/
    "float sd_capped_cone(vec3 p, float h, float r_base, float r_top) {\n"
    "  vec2 q = vec2(length(p.xz), p.y);\n"
    "  vec2 k1 = vec2(r_top, h);\n"
    "  vec2 k2 = vec2(r_top - r_base, 2.0 * h);\n"
    "  vec2 ca = vec2(q.x - min(q.x, (q.y < 0.0) ? r_base : r_top), abs(q.y) - h);\n"
    "  vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot(k2, k2), 0.0, 1.0);\n"
    "  float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;\n"
    "  return s * sqrt(min(dot(ca, ca), dot(cb, cb)));\n"
    "}\n"
    "\n"
    // Returns (distance, material_id). 0 = ground, 1 = volcano rock.
    // The crater is left implicit: the cone narrows to r_top=0.35, and
    // the volumetric plume fills the visible opening so we don't need to
    // model lava-as-geometry — emission alone sells the eruption.
    "vec2 map(vec3 p) {\n"
    "  float ground = p.y + 0.8;\n"
    "  vec3 q = p - vec3(0.0, -0.1, 0.0);\n"
    "  float cone = sd_capped_cone(q, 0.7, 1.4, 0.35);\n"
    "  cone += 0.04 * (fbm(q * 3.0) - 0.5);\n"
    "  if (cone < ground) return vec2(cone, 1.0);\n"
    "  return vec2(ground, 0.0);\n"
    "}\n"
    "\n"
    "vec3 estimate_normal(vec3 p) {\n"
    "  float h = 0.0008 / fov_scale;\n"
    "  vec2 e = vec2(h, 0.0);\n"
    "  return normalize(vec3(\n"
    "    map(p + e.xyy).x - map(p - e.xyy).x,\n"
    "    map(p + e.yxy).x - map(p - e.yxy).x,\n"
    "    map(p + e.yyx).x - map(p - e.yyx).x));\n"
    "}\n"
    "\n"
    "vec3 sky_color(vec3 dir) {\n"
    "  float vy = clamp(0.5 + dir.y * 0.5, 0.0, 1.0);\n"
    "  vec3 sky = mix(vec3(0.06, 0.04, 0.10), vec3(0.005, 0.005, 0.02), vy);\n"
    "  if (dir.y > 0.05) {\n"
    "    vec2 sd = dir.xz / max(dir.y, 0.05);\n"
    "    float s = pow(hash12(floor(sd * 180.0)), 60.0);\n"
    "    sky += vec3(s) * 0.9;\n"
    "  }\n"
    "  return sky;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "  vec2 uv = vec2(v_uv.x * 2.0 * aspect, v_uv.y * 2.0) / fov_scale;\n"
    "  vec3 fwd = normalize(cam_target - cam_pos);\n"
    "  vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));\n"
    "  vec3 up = cross(right, fwd);\n"
    "  vec3 dir = normalize(fwd + right * uv.x + up * uv.y);\n"
    "\n"
    "  // Surface raymarch.\n"
    "  const int MAX_STEPS = 140;\n"
    "  float t = 0.0;\n"
    "  bool hit = false;\n"
    "  float mat = 0.0;\n"
    "  for (int i = 0; i < MAX_STEPS; i++) {\n"
    "    vec3 p = cam_pos + dir * t;\n"
    "    vec2 m = map(p);\n"
    "    if (m.x < 0.0008 * (t + 0.5)) { hit = true; mat = m.y; break; }\n"
    "    if (t > 30.0) break;\n"
    "    t += m.x * 0.85;\n"
    "  }\n"
    "\n"
    "  vec3 col;\n"
    "  if (hit) {\n"
    "    vec3 p = cam_pos + dir * t;\n"
    "    vec3 n = estimate_normal(p);\n"
    "    vec3 L = normalize(vec3(0.4, 0.6, 0.3));\n"
    "    float ndl = max(0.0, dot(n, L));\n"
    "    if (mat < 0.5) {\n"
    "      // Ground plane: dark basalt with subtle noise.\n"
    "      float g = 0.45 + 0.55 * fbm(p * 2.0);\n"
    "      vec3 albedo = mix(vec3(0.05,0.04,0.04), vec3(0.10,0.08,0.07), g);\n"
    "      col = albedo * (0.10 + 0.55 * ndl);\n"
    "    } else {\n"
    "      // Volcano rock: dark, with migrating glowing lava cracks that\n"
    "      // concentrate near the rim.\n"
    "      vec3 rock_a = vec3(0.07, 0.05, 0.05);\n"
    "      vec3 rock_b = vec3(0.14, 0.10, 0.08);\n"
    "      float rk = fbm(p * 5.0);\n"
    "      vec3 rock = mix(rock_a, rock_b, rk);\n"
    "      float crack = smoothstep(0.55, 0.85, fbm(p * 8.0 + vec3(0.0, -time * 0.05, 0.0)));\n"
    "      crack *= smoothstep(-0.5, 0.55, p.y);\n"
    "      vec3 lava_em = vec3(2.5, 0.7, 0.10) * crack * (0.8 + 0.4 * eruption);\n"
    "      col = rock * (0.12 + 0.70 * ndl) + lava_em;\n"
    "    }\n"
    "    // Crater glow falls onto every surface.\n"
    "    vec3 crater = vec3(0.0, 0.55, 0.0);\n"
    "    float gd = length(p - crater);\n"
    "    col += vec3(2.2, 0.55, 0.10) * exp(-gd * 1.6) * (0.35 + 0.30 * eruption);\n"
    "    // Distance fog towards the night sky.\n"
    "    float fog = 1.0 - exp(-t * 0.06);\n"
    "    col = mix(col, vec3(0.04, 0.03, 0.05), fog * 0.55);\n"
    "  } else {\n"
    "    col = sky_color(dir);\n"
    "  }\n"
    "\n"
    "  // Volumetric plume layered on top. Marches up to the surface hit\n"
    "  // distance (or 30.0 if the ray missed), accumulating density above\n"
    "  // the crater along a roughly upward cone.\n"
    "  float t_max = hit ? t : 30.0;\n"
    "  vec3 plume_col = vec3(0.0);\n"
    "  float trans = 1.0;\n"
    "  vec3 crater_c = vec3(0.0, 0.55, 0.0);\n"
    "  const int PSTEPS = 32;\n"
    "  float dt = t_max / float(PSTEPS);\n"
    "  // Per-pixel jitter to break up step banding.\n"
    "  float t0 = dt * (0.5 + 0.5 * hash12(gl_FragCoord.xy));\n"
    "  for (int i = 0; i < PSTEPS; i++) {\n"
    "    float ti = t0 + float(i) * dt;\n"
    "    if (ti > t_max) break;\n"
    "    vec3 p = cam_pos + dir * ti;\n"
    "    float h = p.y - crater_c.y;\n"
    "    if (h < 0.0) continue;\n"
    "    // Plume widens with height.\n"
    "    float radial = max(0.0, length(p.xz) - 0.10 - 0.18 * h);\n"
    "    float shape = exp(-radial * 5.0) * exp(-h * 0.35);\n"
    "    float n = fbm(p * 1.3 + vec3(0.0, -time * 0.45, time * 0.05));\n"
    "    float density = shape * smoothstep(0.25, 0.85, n) * (0.7 + 0.6 * eruption);\n"
    "    if (density < 0.001) continue;\n"
    "    // Emission near the crater (orange glow), gray ash above.\n"
    "    vec3 ash = mix(vec3(0.55, 0.45, 0.40), vec3(0.18, 0.16, 0.16), smoothstep(0.0, 1.5, h));\n"
    "    vec3 emit = vec3(2.6, 0.7, 0.10) * exp(-h * 2.2);\n"
    "    // Sparse hot embers scattered in the plume.\n"
    "    float ember = step(0.985, hash13(floor(p * 30.0) + floor(time * 8.0)));\n"
    "    emit += vec3(3.0, 1.4, 0.30) * ember * 6.0;\n"
    "    vec3 sample_col = ash + emit;\n"
    "    float a = 1.0 - exp(-density * dt * 1.8);\n"
    "    plume_col += trans * a * sample_col;\n"
    "    trans *= (1.0 - a);\n"
    "    if (trans < 0.01) break;\n"
    "  }\n"
    "  col = col * trans + plume_col;\n"
    "\n"
    "  // Reinhard tonemap so lava and embers don't clip the display.\n"
    "  col = col / (1.0 + col);\n"
    "  // Mild gamma so the night sky doesn't look flat.\n"
    "  col = pow(col, vec3(1.0 / 1.1));\n"
    "  frag_color = vec4(col, 1.0);\n"
    "}\n";

static struct {
    sg_pipeline pip;
    volcano_params_t params;
} volcano;

static void volcano_init(void) {
    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source   = vs_src,
        .fragment_func.source = fs_src,
        .attrs = { [0] = { .glsl_name = "pos" } },
        .uniform_blocks[0] = {
            .stage  = SG_SHADERSTAGE_FRAGMENT,
            .size   = sizeof(volcano_params_t),
            .layout = SG_UNIFORMLAYOUT_NATIVE,
            .glsl_uniforms = {
                [0] = { .glsl_name = "cam_pos",    .type = SG_UNIFORMTYPE_FLOAT3 },
                [1] = { .glsl_name = "cam_target", .type = SG_UNIFORMTYPE_FLOAT3 },
                [2] = { .glsl_name = "aspect",     .type = SG_UNIFORMTYPE_FLOAT  },
                [3] = { .glsl_name = "fov_scale",  .type = SG_UNIFORMTYPE_FLOAT  },
                [4] = { .glsl_name = "time",       .type = SG_UNIFORMTYPE_FLOAT  },
                [5] = { .glsl_name = "eruption",   .type = SG_UNIFORMTYPE_FLOAT  },
            },
        },
        .label = "volcano-shader",
    });

    volcano.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .layout = { .attrs = { [0].format = SG_VERTEXFORMAT_FLOAT2 } },
        .label  = "volcano-pipeline",
    });
}

static void volcano_apply_defaults(camera_t *cam) {
    cam->yaw      = 0.6f;
    cam->pitch    = 0.05f;
    cam->distance = 5.0f;
    cam->yaw0      = cam->yaw;
    cam->pitch0    = cam->pitch;
    cam->distance0 = cam->distance;
}

static void volcano_url_overrides(void) {
    // Reserved for future ?eruption= / ?lava= overrides.
}

static void volcano_draw(const camera_t *cam, double time_accum) {
    compute_camera_basis(cam, volcano.params.cam_pos, volcano.params.cam_target, &volcano.params.fov_scale);
    volcano.params.aspect = sapp_widthf() / sapp_heightf();
    volcano.params.time = (float)time_accum;
    volcano.params.eruption = 0.55f + 0.40f * sinf((float)time_accum * 0.30f);

    sg_apply_pipeline(volcano.pip);
    sg_bindings bind = (sg_bindings){ .vertex_buffers[0] = fullscreen_tri_buffer() };
    sg_apply_bindings(&bind);
    sg_apply_uniforms(0, &SG_RANGE(volcano.params));
    sg_draw(0, 3, 1);
}

static void volcano_overlay(void) {
    sdtx_color3b(0x80, 0x88, 0x90);
    sdtx_printf("scene    night-time stratovolcano\n");
    sdtx_printf("time     %7.1fs\n", (double)volcano.params.time);
    sdtx_printf("eruption %7.2f\n",  (double)volcano.params.eruption);
}

const viz_iface VIZ_VOLCANO_IFACE = {
    .name               = "volcanic eruption",
    .url_token          = "volcano",
    .init               = volcano_init,
    .apply_defaults     = volcano_apply_defaults,
    .url_overrides      = volcano_url_overrides,
    .draw               = volcano_draw,
    .overlay            = volcano_overlay,
    .supports_auto_zoom = false,
};
