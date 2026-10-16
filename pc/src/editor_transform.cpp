// R (scale) on the editor's objects (editor_transform.h).
#include "editor_transform.h"

#include <math.h>

namespace rood_transform {
namespace {
float within(float v, float lo, float hi) { return fminf(fmaxf(v, lo), hi); }
} // namespace

float uniform_ratio(const float ratio[3])
{
    float best = 1, far_from_one = -1;
    for (int k = 0; k < 3; k++) {
        float r = fmaxf(ratio[k], 0.001f), d = fabsf(logf(r));
        if (d > far_from_one)
            far_from_one = d, best = r;
    }
    return best;
}
void scale_light(const rood_lights::Light& start, const float ratio[3], rood_lights::Light& out)
{
    out = start;
    if (start.type == 2) {
        out.size[0] = within(start.size[0] * ratio[0], RECT_SIZE_MIN, RECT_SIZE_MAX);
        out.size[1] = within(start.size[1] * ratio[1], RECT_SIZE_MIN, RECT_SIZE_MAX);
        out.radius = within(start.radius * ratio[2], RADIUS_MIN, RADIUS_MAX);
        return;
    }
    out.radius = within(start.radius * uniform_ratio(ratio), RADIUS_MIN, RADIUS_MAX);
}
void scale_actor(const prefab::Actor& start, const float ratio[3], prefab::Actor& out)
{
    out = start;
    out.scale = within(start.scale * uniform_ratio(ratio), SCALE_MIN, SCALE_MAX);
}
void scale_volume(const float start[3], const float ratio[3], float out[3])
{
    for (int k = 0; k < 3; k++)
        out[k] = fmaxf(start[k] * ratio[k], VOLUME_MIN);
}
} // namespace rood_transform
