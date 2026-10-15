/* Picking in the editor's view (editor_scene.cpp): the world ray under a
 * window pixel, where a ray enters a fog volume (a box or an ellipsoid
 * turned about y, placed as atmosphere.cpp and medium.glsl place it), and
 * the window rectangle a box select covers. Plain math
 * (pc/tests/selection_probe.cpp). */
#ifndef ROOD_EDITOR_PICKING_H
#define ROOD_EDITOR_PICKING_H

namespace rood_pick {
/* The ray under window pixel (x, y) of a view (column-major view and
 * projection with clip w > 0 in front, as pc_overlay_view, and its window
 * rectangle x, y, w, h): its origin on the near plane and its unit
 * direction. False when the matrices do not invert. */
bool ray(const float view[16], const float projection[16], const float rect[4], float x, float y, float origin[3],
    float dir[3]);
/* The distance along a ray (unit `dir`) to where it enters a fog volume
 * (centre, full size, yaw in degrees; a box, else an ellipsoid inside the
 * same size), or -1 when it misses it or starts inside it. */
float volume_hit(const float origin[3], const float dir[3], const float centre[3], const float size[3], float yaw,
    bool sphere);
/* Whether (x, y) lies in the rectangle with corners (x0, y0) and (x1, y1), in either order. */
bool in_rect(float x, float y, float x0, float y0, float x1, float y1);
} // namespace rood_pick
#endif
