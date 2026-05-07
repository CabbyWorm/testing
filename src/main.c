// Per-client multi-visualization renderer. The browser fingerprint is
// hashed in shell.html and written to the page URL as
// ?seed=<8 hex>&gpu=<short>&viz=<name>. sokol_args parses location.search
// on the emscripten target, so we read those straight out via sargs_value*.
//
// Two visualizations ship today: a raymarched 3D fractal (the original)
// and a raymarched 3D volcanic eruption. They are independent translation
// units (src/viz_fractal.c, src/viz_volcano.c) plugged in via the
// viz_iface vtable in viz.h. Adding more is one .c/.h pair plus one line
// in viz_registry.c.

#define SOKOL_IMPL
#include "common.h"
#include "viz.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

static struct {
    int viz_index;
    camera_t cam;

    bool show_overlay;
    bool show_menu;
    int  menu_sel;
    bool auto_zoom;

    double time_accum;

    bool drag_active;
    float drag_last_x, drag_last_y;
    bool pinch_active;
    float pinch_last_dist;

    uint32_t seed;
    char gpu[80];

    sg_pass_action pass_action;
    sg_buffer fullscreen_tri;
} state;

uint32_t main_get_seed(void) { return state.seed; }
sg_buffer fullscreen_tri_buffer(void) { return state.fullscreen_tri; }

void compute_camera_basis(const camera_t *cam,
                          float out_pos[3],
                          float out_target[3],
                          float *out_fov_scale) {
    float cp = cosf(cam->pitch);
    float r = cam->distance0;
    out_pos[0] = r * cp * sinf(cam->yaw);
    out_pos[1] = r * sinf(cam->pitch);
    out_pos[2] = r * cp * cosf(cam->yaw);
    out_target[0] = 0.0f;
    out_target[1] = 0.0f;
    out_target[2] = 0.0f;
    *out_fov_scale = cam->distance0 / fmaxf(cam->distance, 1e-6f);
}

static uint32_t parse_seed_hex(const char *s) {
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return (uint32_t)strtoul(s, NULL, 16);
}

static int viz_index_for_token(const char *tok) {
    for (int i = 0; i < VIZ_COUNT; i++) {
        if (strcmp(tok, VIZ_REGISTRY[i]->url_token) == 0) return i;
        if (strcmp(tok, VIZ_REGISTRY[i]->name)      == 0) return i;
    }
    return -1;
}

static void switch_viz(int idx) {
    if (idx < 0 || idx >= VIZ_COUNT) return;
    state.viz_index = idx;
    VIZ_REGISTRY[idx]->apply_defaults(&state.cam);
    state.auto_zoom = false;
}

// Called from JS on a device-motion shake. Bumps the seed forward and
// re-applies the active viz's defaults so the shaking visibly resets the
// camera (and, for the fractal viz, picks a new fractal).
WASM_EXPORT void next_fractal(void) {
    state.seed += 0x9E3779B1u;  // golden-ratio constant; well-distributed step
    VIZ_REGISTRY[state.viz_index]->apply_defaults(&state.cam);
}

static void init(void) {
    sargs_setup(&(sargs_desc){ 0 });
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = slog_func });
    sdtx_setup(&(sdtx_desc_t){
        .fonts = { [0] = sdtx_font_kc853() },
        .logger.func = slog_func,
    });

    state.seed = parse_seed_hex(sargs_value_def("seed", "00000000"));

    const char *g = sargs_value_def("gpu", "");
    strncpy(state.gpu, g, sizeof(state.gpu) - 1);
    state.gpu[sizeof(state.gpu) - 1] = 0;
    state.show_overlay = false;

    float verts[] = { -1.0f, -3.0f, -1.0f, 1.0f, 3.0f, 1.0f };
    state.fullscreen_tri = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(verts), .label = "fullscreen-tri",
    });

    // Build every registered viz up front so menu switching is instant.
    // Each viz's init() seeds its own defaults so toggling to a non-active
    // viz via the menu shows reasonable params rather than zeros.
    for (int i = 0; i < VIZ_COUNT; i++) VIZ_REGISTRY[i]->init();

    state.viz_index = 0;
    if (sargs_exists("viz")) {
        int idx = viz_index_for_token(sargs_value("viz"));
        if (idx >= 0) state.viz_index = idx;
    }

    // Order matters: apply_defaults reseeds params + sets the camera home;
    // url_overrides runs after so query-string args win over the seed.
    VIZ_REGISTRY[state.viz_index]->apply_defaults(&state.cam);
    for (int i = 0; i < VIZ_COUNT; i++) VIZ_REGISTRY[i]->url_overrides();

    // ?yaw / ?pitch / ?dist apply to whichever viz ends up active.
    if (sargs_exists("dist"))  state.cam.distance = (float)atof(sargs_value("dist"));
    if (sargs_exists("yaw"))   state.cam.yaw      = (float)atof(sargs_value("yaw"));
    if (sargs_exists("pitch")) state.cam.pitch    = (float)atof(sargs_value("pitch"));

    state.pass_action = (sg_pass_action){
        .colors[0] = { .load_action = SG_LOADACTION_DONTCARE },
    };
}

static void orbit(float dx_px, float dy_px) {
    const float kSensitivity = 0.005f;
    state.cam.yaw   -= dx_px * kSensitivity;
    state.cam.pitch += dy_px * kSensitivity;
    const float lim = PI * 0.5f - 0.01f;
    if (state.cam.pitch >  lim) state.cam.pitch =  lim;
    if (state.cam.pitch < -lim) state.cam.pitch = -lim;
}

static void dolly(float factor) {
    state.cam.distance *= factor;
    if (state.cam.distance < 0.001f) state.cam.distance = 0.001f;
    if (state.cam.distance > 10.0f)  state.cam.distance = 10.0f;
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
            if (state.show_menu) {
                switch (e->key_code) {
                    case SAPP_KEYCODE_UP:
                    case SAPP_KEYCODE_W:
                        state.menu_sel = (state.menu_sel - 1 + VIZ_COUNT) % VIZ_COUNT;
                        break;
                    case SAPP_KEYCODE_DOWN:
                    case SAPP_KEYCODE_S:
                        state.menu_sel = (state.menu_sel + 1) % VIZ_COUNT;
                        break;
                    case SAPP_KEYCODE_ENTER:
                    case SAPP_KEYCODE_SPACE:
                        switch_viz(state.menu_sel);
                        state.show_menu = false;
                        break;
                    case SAPP_KEYCODE_ESCAPE:
                    case SAPP_KEYCODE_M:
                        state.show_menu = false;
                        break;
                    default:
                        break;
                }
                break;
            }
            if (e->key_code == SAPP_KEYCODE_M) {
                state.show_menu = true;
                state.menu_sel = state.viz_index;
            } else if (e->key_code == SAPP_KEYCODE_R) {
                state.cam.yaw      = state.cam.yaw0;
                state.cam.pitch    = state.cam.pitch0;
                state.cam.distance = state.cam.distance0;
                state.auto_zoom = false;
            } else if (e->key_code == SAPP_KEYCODE_H) {
                state.show_overlay = !state.show_overlay;
            } else if (e->key_code == SAPP_KEYCODE_Z) {
                if (VIZ_REGISTRY[state.viz_index]->supports_auto_zoom) {
                    state.auto_zoom = !state.auto_zoom;
                }
            }
            break;
        default:
            break;
    }
}

static void draw_overlay(void) {
    sdtx_canvas(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f);
    sdtx_origin(1.0f, 1.0f);
    sdtx_home();
    sdtx_font(0);
    sdtx_color3b(0xd8, 0xe0, 0xe6);
    sdtx_printf("YOUR SOKOL VIZ\n");
    sdtx_color3b(0x80, 0x88, 0x90);
    sdtx_printf("\n");
    sdtx_printf("viz      %s\n", VIZ_REGISTRY[state.viz_index]->name);

    VIZ_REGISTRY[state.viz_index]->overlay();

    sdtx_color3b(0x80, 0x88, 0x90);
    sdtx_printf("camera   yaw %+0.3f  pitch %+0.3f  zoom %.1fx%s\n",
                (double)state.cam.yaw, (double)state.cam.pitch,
                (double)(state.cam.distance0 / state.cam.distance),
                state.auto_zoom ? "  [auto]" : "");
    sdtx_printf("gpu      %s\n", state.gpu[0] ? state.gpu : "?");
    sdtx_color3b(0x50, 0x58, 0x60);
    sdtx_printf("\ndrag orbit  scroll/pinch dolly%s  R reset  H hide  M menu",
                VIZ_REGISTRY[state.viz_index]->supports_auto_zoom ? "  Z auto-zoom" : "");
    sdtx_draw();
}

static void draw_menu(void) {
    sdtx_canvas(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f);
    sdtx_origin(2.0f, 2.0f);
    sdtx_home();
    sdtx_font(0);
    sdtx_color3b(0xd8, 0xe0, 0xe6);
    sdtx_printf("== VISUALIZATION ==\n\n");
    for (int i = 0; i < VIZ_COUNT; i++) {
        bool sel    = (i == state.menu_sel);
        bool active = (i == state.viz_index);
        if (sel) sdtx_color3b(0xd8, 0xe0, 0xe6);
        else     sdtx_color3b(0x80, 0x88, 0x90);
        sdtx_printf("%s %s", sel ? ">" : " ", VIZ_REGISTRY[i]->name);
        if (active) {
            sdtx_color3b(0x60, 0x80, 0x60);
            sdtx_printf("  [active]");
        }
        sdtx_printf("\n");
    }
    sdtx_color3b(0x50, 0x58, 0x60);
    sdtx_printf("\nup/down select   ENTER apply   M close");
    sdtx_draw();
}

static void frame(void) {
    state.time_accum += sapp_frame_duration();

    if (state.auto_zoom && VIZ_REGISTRY[state.viz_index]->supports_auto_zoom) {
        float dt = (float)sapp_frame_duration();
        state.cam.distance *= expf(-dt * 0.25f);
        state.cam.yaw      += dt * 0.18f;
        if (state.cam.distance < 0.001f) state.cam.distance = state.cam.distance0;
    }

    sg_begin_pass(&(sg_pass){ .action = state.pass_action, .swapchain = sglue_swapchain() });

    VIZ_REGISTRY[state.viz_index]->draw(&state.cam, state.time_accum);

    if (state.show_overlay) draw_overlay();
    if (state.show_menu)    draw_menu();

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
        .window_title = "your sokol viz",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
