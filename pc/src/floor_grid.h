/* The room's floor grid's last steps (scene_capture.c pc_scene_floor_grid),
 * without the game. Rooms: pc/docs/Vulkan_Renderer.md "The room's interior
 * and the void". */
#ifndef ROOD_FLOOR_GRID_H
#define ROOD_FLOOR_GRID_H
#ifdef __cplusplus
extern "C" {
#endif
/* A cell (scene_capture.h pc_scene_floor_cells): no floor (outside the
 * room's walls, the void), a floor, a small hole the closing fills. */
#define PC_FLOOR_EMPTY 0
#define PC_FLOOR_SOLID 1
#define PC_FLOOR_FILLED 2
#define PC_FLOOR_GRID_MAX 64
/* From an n x n grid (n <= PC_FLOOR_GRID_MAX; [j * n + i]: i in x, j in z):
 * lowest (where has is set) the lowest level up-facing face over the cell
 * (world y, y down: the largest), occupied where any up-facing face of the
 * room covers it (level or sloped; not a black shell):
 * - heights: each cell the lowest of the level floors in its 3 x 3, the
 *   cells without one the mean of their neighbours', growing outward (every
 *   cell gets one, as before the cells existed);
 * - cells: SOLID where occupied or has; FILLED where a closing (dilated by
 *   `fill` cells, then eroded by `fill`, a square; outside the grid counts
 *   as floor while eroding only where the grid's edge cell next to it has
 *   one) covers a cell without a floor: holes up to 2 x fill cells across,
 *   never past the room's edge (an empty strip along the grid's edge stays
 *   empty) or into a concave corner; EMPTY elsewhere.
 * Returns 0 (outputs untouched) when no cell has a level floor. */
int pc_floor_grid_finish(int n, int fill, const float* lowest, const unsigned char* has, const unsigned char* occupied,
    float* heights, unsigned char* cells);
#ifdef __cplusplus
}
#endif
#endif
