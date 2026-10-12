/* Smart cutaway walls for gameplay (gameplay_walls.c; Native_Rendering.md
 * "Smart walls"). */
#ifndef ROOD_GAMEPLAY_WALLS_H
#define ROOD_GAMEPLAY_WALLS_H
#ifdef __cplusplus
extern "C" {
#endif

/* Settings keys and RPC graphics, same names. */
extern int pc_gameplay_walls; /* 0 original (the game's facing mask), 1 smart */
extern int pc_gameplay_walls_ghost; /* smart: occluding groups drawn as ghosts instead of left out */
extern float pc_gameplay_walls_opacity; /* a ghost's share of pixels kept */
extern float pc_gameplay_walls_radius; /* world units around Ashley's body that count as in the way */

/* func_8009723C (room_geometry.c), the current room: once per frame before
 * its group walk (data NULL while the editor's free camera flies; facing:
 * the mask it tests; mode: its mode argument, which picks the alternate
 * set). Returns 1 when smart walls decide this frame's groups (0: the
 * game's mask alone, as with gameplay_walls original). */
int pc_gameplay_walls_frame(const void* data, unsigned facing, unsigned mode);
/* For group `index` (kept: the game's own test keeps it): 1 to draw it,
 * with the share of its pixels left out now and in the frame before (an
 * ordered dither) and whether it is a ghost (edges kept). */
int pc_gameplay_walls_group(unsigned index, int kept, float* cut, float* cut_previous, int* ghost);
/* RPC room_groups: 0 off / unknown, 1 decided this frame, 2 stale. counts
 * (grid cells this frame): in the focus region nearer than the focus, seen
 * in front of the play area, over void, all its front-facing cells. */
int pc_gameplay_walls_info(unsigned index, float* shown, int* occluding, int counts[4]);

#ifdef __cplusplus
}
#endif
#endif
