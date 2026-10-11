/* Editor viewport overlays and snapping (editor_overlays.cpp): the room's
 * edges and points drawn over the lit view (occluded ones dimmed), the
 * characters' wireframes, and the snap targets the light tools use (grid,
 * point, edge, edge midpoint, surface) with Houdini-style highlighting of
 * the points around a snapped point. Editor only; gameplay never calls it. */
#ifndef ROOD_EDITOR_OVERLAYS_H
#define ROOD_EDITOR_OVERLAYS_H
#ifdef __cplusplus
extern "C" {
#endif

/* A view of the editing tools: column-major view and projection matrices
 * (clip w > 0 in front) and its window rectangle (x, y, w, h). */
typedef struct {
    float view[16], projection[16], rect[4];
} pc_overlay_view;
/* World position and normal of the surface under a window pixel. */
typedef int (*pc_overlay_pick)(float x, float y, float world[3], float normal[3]);

/* Show switches (settings keys editor_wireframe, editor_points,
 * editor_character_wireframe, editor_snap_show, editor_occluded_opacity;
 * RPC graphics, same names). */
extern int pc_overlay_wire; /* room edges */
extern int pc_overlay_points; /* room points (vertices) */
extern int pc_overlay_actor_wire; /* characters, weapons and props */
extern int pc_overlay_snap_show; /* points and edges appear while a point/edge snap drag runs */
extern float pc_overlay_occluded; /* opacity of hidden edges and points relative to visible ones */

/* Snapping (settings keys editor_snap_point, editor_snap_edge,
 * editor_snap_midpoint, editor_snap_surface, editor_snap_radius,
 * editor_snap_highlight, editor_snap_offset). Grid snapping is the light
 * tools' own (pc_lights_snap, editor_snap_grid). */
extern int pc_snap_point, pc_snap_edge, pc_snap_midpoint, pc_snap_surface;
extern float pc_snap_radius; /* search radius around the cursor, window pixels (at UI scale 1) */
extern float pc_snap_highlight; /* neighbour highlight radius, world units */
extern float pc_snap_offset; /* surface snap: distance off the surface along its normal */

enum { PC_SNAP_NONE, PC_SNAP_POINT, PC_SNAP_MIDPOINT, PC_SNAP_EDGE, PC_SNAP_SURFACE };
#define PC_SNAP_MODE_POINT 1u
#define PC_SNAP_MODE_EDGE 2u
#define PC_SNAP_MODE_MIDPOINT 4u
#define PC_SNAP_MODE_SURFACE 8u
typedef struct {
    int type; /* PC_SNAP_* */
    float position[3]; /* the snapped world point (surface: offset along the normal) */
    float surface[3], normal[3]; /* surface snap: the point on the surface and its normal */
    float screen[2]; /* window pixel of the target (surface: of the surface point) */
    float distance; /* from the cursor, window pixels */
    float depth; /* clip w */
    int index; /* point or edge index */
    int occluded; /* behind other room surfaces or on a back face */
} pc_snap_result;

/* The configured modes (pc_snap_*), with point snapping inverted when
 * `toggle_point` (V held during a drag). */
unsigned pc_snap_modes(int toggle_point);
/* Find the snap target near window pixel (x, y): point, then edge midpoint,
 * then edge (within pc_snap_radius pixels), then the surface under the
 * cursor. Returns the type (PC_SNAP_NONE: nothing). */
int pc_overlay_snap(const pc_overlay_view* v, float x, float y, unsigned modes, pc_overlay_pick pick,
    pc_snap_result* out);
/* Points within `radius` of `position` (shown room points), nearest first:
 * xyz and the highlight weight (1 at the point, falling to 0 at radius). */
int pc_overlay_neighbours(const float position[3], float radius, float (*out)[4], int max);

/* Overlays for this frame on the given ImGui draw list (an ImDrawList*):
 * `snapping` shows points and edges while a snap drag runs
 * (pc_overlay_snap_show). */
void pc_overlay_draw(const pc_overlay_view* v, void* draw_list, int snapping);
/* Highlight a snap result: the target, its neighbours with falloff, and a
 * readout of the snapped coordinate next to it. */
void pc_overlay_draw_snap(const pc_overlay_view* v, void* draw_list, const pc_snap_result* r);
/* A result to keep highlighted (RPC editor_snap highlight), or NULL to stop. */
void pc_overlay_hold_snap(const pc_snap_result* r);

typedef struct {
    int points, edges; /* in the room (welded; quads' diagonals are not edges) */
    int shown_points, shown_edges; /* in groups the view draws, outside the cutaway */
    int visible_samples, occluded_samples, pending_samples; /* the occlusion test's state */
    int bvh_triangles, character_triangles;
    float occlusion_ms, draw_ms; /* last frame */
    int drawn_lines, drawn_points; /* last frame */
} pc_overlay_stats;
void pc_overlay_get_stats(pc_overlay_stats* out);
/* Bring the geometry and its occlusion up to date for this view now (RPC). */
void pc_overlay_update(const pc_overlay_view* v);
/* Shown points / edges for tests: world position, window pixel, occluded. */
int pc_overlay_sample_points(const pc_overlay_view* v, float (*world)[3], float (*screen)[2], int* occluded, int max);
int pc_overlay_sample_edges(const pc_overlay_view* v, float (*world)[6], float (*screen)[4], int* occluded, int max);

#ifdef __cplusplus
}
#endif
#endif
