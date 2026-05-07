// Plug-in interface for one visualization. Each visualization lives in its
// own translation unit (viz_fractal.c, viz_volcano.c, ...) and exposes a
// single `const viz_iface` instance. viz_registry.c collects them into the
// VIZ_REGISTRY array; main.c drives the active one each frame.
#pragma once

#include "common.h"

typedef struct viz_iface {
    const char *name;          // shown in menu and overlay
    const char *url_token;     // matched against ?viz=

    void (*init)(void);
    void (*apply_defaults)(camera_t *cam);
    void (*url_overrides)(void);
    void (*draw)(const camera_t *cam, double time_accum);
    void (*overlay)(void);

    bool supports_auto_zoom;
} viz_iface;

extern const viz_iface *const VIZ_REGISTRY[];
extern const int VIZ_COUNT;
