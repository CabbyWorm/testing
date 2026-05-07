// Shared types and helpers for the multi-visualization renderer. Includes
// the sokol headers without SOKOL_IMPL — exactly one TU (main.c) is allowed
// to define SOKOL_IMPL, every other TU uses these declarations.
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_args.h"
#include "sokol_debugtext.h"

#define PI 3.14159265358979323846f

typedef struct {
    float yaw, pitch, distance;
    float yaw0, pitch0, distance0;
} camera_t;

// Orbit camera math shared by every visualization. The camera sits on a
// sphere of radius `cam->distance0` around the origin and looks at the
// origin; `cam->distance` feeds fov_scale (telephoto-style zoom) so the
// camera never drives through the surface.
void compute_camera_basis(const camera_t *cam,
                          float out_pos[3],
                          float out_target[3],
                          float *out_fov_scale);

// The fullscreen-triangle vertex buffer is created once in main.c and
// shared across every viz pipeline. main.c owns the bindings; viz draw()
// implementations only need to apply their pipeline + uniforms.
sg_buffer fullscreen_tri_buffer(void);

// Seed accessor so viz modules can read the URL-derived seed without
// depending on main's internal state struct.
uint32_t main_get_seed(void);
