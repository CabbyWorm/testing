// One-line-per-viz registry. Adding a new visualization is a matter of
// dropping a viz_<name>.{h,c} pair into src/, listing the .c in build.sh,
// and appending its iface here.
#include "viz.h"
#include "viz_fractal.h"
#include "viz_volcano.h"

const viz_iface *const VIZ_REGISTRY[] = {
    &VIZ_FRACTAL_IFACE,
    &VIZ_VOLCANO_IFACE,
};

const int VIZ_COUNT = (int)(sizeof(VIZ_REGISTRY) / sizeof(VIZ_REGISTRY[0]));
