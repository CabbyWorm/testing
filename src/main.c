// Spinning cube using sokol_app + sokol_gfx, targeting WebGL2 (GLES3) via Emscripten.
// Adapted from the sokol-samples cube-sapp example, with shaders inlined as
// GLSL ES 3.00 source so we don't need sokol-shdc in the build pipeline.

#include <math.h>
#include <stddef.h>

#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

typedef struct { float m[16]; } mat4;

static mat4 mat4_identity(void) {
    mat4 r = {{0}};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static mat4 mat4_mul(mat4 a, mat4 b) {
    mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) {
                s += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
            r.m[c * 4 + row] = s;
        }
    }
    return r;
}

static mat4 mat4_perspective(float fov_rad, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov_rad * 0.5f);
    mat4 r = {{0}};
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

static mat4 mat4_lookat(float ex, float ey, float ez, float cx, float cy, float cz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = 1.0f / sqrtf(fx*fx + fy*fy + fz*fz);
    fx *= fl; fy *= fl; fz *= fl;
    // up = (0,1,0)
    float sx = fy * 0.0f - fz * 1.0f;
    float sy = fz * 0.0f - fx * 0.0f;
    float sz = fx * 1.0f - fy * 0.0f;
    float sl = 1.0f / sqrtf(sx*sx + sy*sy + sz*sz);
    sx *= sl; sy *= sl; sz *= sl;
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;
    mat4 r = mat4_identity();
    r.m[0] = sx; r.m[4] = sy; r.m[8]  = sz;
    r.m[1] = ux; r.m[5] = uy; r.m[9]  = uz;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx*ex + sy*ey + sz*ez);
    r.m[13] = -(ux*ex + uy*ey + uz*ez);
    r.m[14] = (fx*ex + fy*ey + fz*ez);
    return r;
}

static mat4 mat4_rotate(float angle_rad, float ax, float ay, float az) {
    float l = 1.0f / sqrtf(ax*ax + ay*ay + az*az);
    ax *= l; ay *= l; az *= l;
    float c = cosf(angle_rad), s = sinf(angle_rad), ic = 1.0f - c;
    mat4 r = mat4_identity();
    r.m[0]  = c + ax*ax*ic;
    r.m[1]  = ay*ax*ic + az*s;
    r.m[2]  = az*ax*ic - ay*s;
    r.m[4]  = ax*ay*ic - az*s;
    r.m[5]  = c + ay*ay*ic;
    r.m[6]  = az*ay*ic + ax*s;
    r.m[8]  = ax*az*ic + ay*s;
    r.m[9]  = ay*az*ic - ax*s;
    r.m[10] = c + az*az*ic;
    return r;
}

typedef struct {
    float mvp[16];
} vs_params_t;

static struct {
    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;
    float rx, ry;
} state;

static const char *vs_src =
    "#version 300 es\n"
    "uniform mat4 mvp;\n"
    "layout(location=0) in vec4 position;\n"
    "layout(location=1) in vec4 color0;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  gl_Position = mvp * position;\n"
    "  color = color0;\n"
    "}\n";

static const char *fs_src =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = color;\n"
    "}\n";

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });

    float vertices[] = {
        // pos                  color
        -1.0f, -1.0f, -1.0f,    1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, -1.0f, -1.0f,    1.0f, 0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, -1.0f,    1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f,  1.0f, -1.0f,    1.0f, 0.0f, 0.0f, 1.0f,

        -1.0f, -1.0f,  1.0f,    0.0f, 1.0f, 0.0f, 1.0f,
         1.0f, -1.0f,  1.0f,    0.0f, 1.0f, 0.0f, 1.0f,
         1.0f,  1.0f,  1.0f,    0.0f, 1.0f, 0.0f, 1.0f,
        -1.0f,  1.0f,  1.0f,    0.0f, 1.0f, 0.0f, 1.0f,

        -1.0f, -1.0f, -1.0f,    0.0f, 0.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, -1.0f,    0.0f, 0.0f, 1.0f, 1.0f,
        -1.0f,  1.0f,  1.0f,    0.0f, 0.0f, 1.0f, 1.0f,
        -1.0f, -1.0f,  1.0f,    0.0f, 0.0f, 1.0f, 1.0f,

         1.0f, -1.0f, -1.0f,    1.0f, 0.5f, 0.0f, 1.0f,
         1.0f,  1.0f, -1.0f,    1.0f, 0.5f, 0.0f, 1.0f,
         1.0f,  1.0f,  1.0f,    1.0f, 0.5f, 0.0f, 1.0f,
         1.0f, -1.0f,  1.0f,    1.0f, 0.5f, 0.0f, 1.0f,

        -1.0f, -1.0f, -1.0f,    0.0f, 0.5f, 1.0f, 1.0f,
        -1.0f, -1.0f,  1.0f,    0.0f, 0.5f, 1.0f, 1.0f,
         1.0f, -1.0f,  1.0f,    0.0f, 0.5f, 1.0f, 1.0f,
         1.0f, -1.0f, -1.0f,    0.0f, 0.5f, 1.0f, 1.0f,

        -1.0f,  1.0f, -1.0f,    1.0f, 0.0f, 0.5f, 1.0f,
        -1.0f,  1.0f,  1.0f,    1.0f, 0.0f, 0.5f, 1.0f,
         1.0f,  1.0f,  1.0f,    1.0f, 0.0f, 0.5f, 1.0f,
         1.0f,  1.0f, -1.0f,    1.0f, 0.0f, 0.5f, 1.0f,
    };
    state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(vertices),
        .label = "cube-vertices",
    });

    uint16_t indices[] = {
        0, 1, 2,  0, 2, 3,
        6, 5, 4,  7, 6, 4,
        8, 9, 10,  8, 10, 11,
        14, 13, 12,  15, 14, 12,
        16, 17, 18,  16, 18, 19,
        22, 21, 20,  23, 22, 20,
    };
    state.bind.index_buffer = sg_make_buffer(&(sg_buffer_desc){
        .usage.index_buffer = true,
        .data = SG_RANGE(indices),
        .label = "cube-indices",
    });

    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = vs_src,
        .fragment_func.source = fs_src,
        .attrs = {
            [0] = { .glsl_name = "position" },
            [1] = { .glsl_name = "color0" },
        },
        .uniform_blocks[0] = {
            .stage = SG_SHADERSTAGE_VERTEX,
            .size = sizeof(vs_params_t),
            .layout = SG_UNIFORMLAYOUT_NATIVE,
            .glsl_uniforms = {
                [0] = { .glsl_name = "mvp", .type = SG_UNIFORMTYPE_MAT4 },
            },
        },
        .label = "cube-shader",
    });

    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT3,
                [1].format = SG_VERTEXFORMAT_FLOAT4,
            },
        },
        .index_type = SG_INDEXTYPE_UINT16,
        .cull_mode = SG_CULLMODE_BACK,
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true,
        },
        .label = "cube-pipeline",
    });

    state.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 0.07f, 0.08f, 0.10f, 1.0f },
        },
    };
}

static void frame(void) {
    const float dt = (float)sapp_frame_duration();
    state.rx += 1.0f * dt;
    state.ry += 1.4f * dt;

    const float aspect = sapp_widthf() / sapp_heightf();
    mat4 proj = mat4_perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.01f, 100.0f);
    mat4 view = mat4_lookat(0.0f, 1.5f, 6.0f, 0.0f, 0.0f, 0.0f);
    mat4 rxm = mat4_rotate(state.rx, 1.0f, 0.0f, 0.0f);
    mat4 rym = mat4_rotate(state.ry, 0.0f, 1.0f, 0.0f);
    mat4 model = mat4_mul(rxm, rym);
    mat4 mvp = mat4_mul(proj, mat4_mul(view, model));

    vs_params_t vs_params;
    for (int i = 0; i < 16; i++) vs_params.mvp[i] = mvp.m[i];

    sg_begin_pass(&(sg_pass){ .action = state.pass_action, .swapchain = sglue_swapchain() });
    sg_apply_pipeline(state.pip);
    sg_apply_bindings(&state.bind);
    sg_apply_uniforms(0, &SG_RANGE(vs_params));
    sg_draw(0, 36, 1);
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .width = 800,
        .height = 600,
        .sample_count = 4,
        .window_title = "Sokol cube on WASM",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
