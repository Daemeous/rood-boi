/* R (scale) on the editor's objects (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Scale semantics"): a gizmo scale's ratios per local axis (right, up,
 * forward: a light's frame, an actor's rotation basis, a fog volume's yawed
 * x, y, z) mapped onto each kind's shape. Never brightness: no function here
 * touches an intensity or a colour. Plain maths (pc/tests/editor_tools_probe.cpp). */
#ifndef ROOD_EDITOR_TRANSFORM_H
#define ROOD_EDITOR_TRANSFORM_H
#include "lights_internal.h"

namespace rood_transform {
/* The schema's ranges (editor_schema.cpp light_kind and actor_kind; fog volumes at least 2). */
constexpr float RADIUS_MIN = 32, RADIUS_MAX = 4096, RECT_SIZE_MIN = 8, RECT_SIZE_MAX = 4096, SCALE_MIN = 0.05f, SCALE_MAX = 20,
                VOLUME_MIN = 2;
/* The ratio a uniform shape takes: the axis whose ratio is furthest from 1
 * (ImGuizmo's centre handle moves all three alike, an axis handle one). */
float uniform_ratio(const float ratio[3]);
/* A light from `start` (as the drag began): point: radius x the uniform
 * ratio; spot: radius (its range) x the uniform ratio, the cone kept; rect:
 * width x right, height x up, radius x forward. */
void scale_light(const rood_lights::Light& start, const float ratio[3], rood_lights::Light& out);
/* A light actor: its scale x the uniform ratio (its lights follow in prefab::evaluate). */
void scale_actor(const prefab::Actor& start, const float ratio[3], prefab::Actor& out);
/* A fog volume's size per axis. */
void scale_volume(const float start[3], const float ratio[3], float out[3]);
} // namespace rood_transform
#endif
