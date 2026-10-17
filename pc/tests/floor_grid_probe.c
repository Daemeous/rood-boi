/* The room's floor grid's last steps (pc/src/floor_grid.c) without the game:
 * where the room is (its cells), holes filled, nothing grown past its walls;
 * heights as before. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "floor_grid.h"

enum { N = 12 };
static int failures;
static float lowest[N * N], heights[N * N];
static unsigned char has[N * N], occupied[N * N], cells[N * N];

static void check(int ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static void clear(void)
{
    memset(lowest, 0, sizeof lowest);
    memset(has, 0, sizeof has);
    memset(occupied, 0, sizeof occupied);
}
/* Level floor at height y over cells i0..i1 x j0..j1. */
static void floor_rect(int i0, int j0, int i1, int j1, float y)
{
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            lowest[j * N + i] = y, has[j * N + i] = 1, occupied[j * N + i] = 1;
}
static void hole(int i0, int j0, int i1, int j1)
{
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            has[j * N + i] = 0, occupied[j * N + i] = 0;
}
static int all_cells(int i0, int j0, int i1, int j1, int kind)
{
    int ok = 1;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            ok &= cells[j * N + i] == kind;
    return ok;
}

int main(void)
{
    /* An L-shaped room: floor (y 0) over x 0-5, z 0-11 and x 6-11, z 0-5; the
     * quadrant x 6-11, z 6-11 lies outside its walls. */
    clear();
    floor_rect(0, 0, 5, 11, 0);
    floor_rect(6, 0, 11, 5, 0);
    check(pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells), "a floor: a grid");
    check(all_cells(6, 6, 11, 11, PC_FLOOR_EMPTY), "the cells outside the walls stay empty (not grown into)");
    check(all_cells(0, 0, 5, 11, PC_FLOOR_SOLID) && all_cells(6, 0, 11, 5, PC_FLOOR_SOLID), "the floor's cells are solid");
    check(fabsf(heights[8 * N + 8]) < 1e-3f, "a cell without a floor still gets a height (the old fog's base)");
    pc_floor_grid_finish(N, 2, lowest, has, occupied, heights, cells);
    check(all_cells(6, 6, 11, 11, PC_FLOOR_EMPTY), "a concave corner is never filled (fill 2)");

    /* Holes in a full floor. */
    clear();
    floor_rect(0, 0, 11, 11, 0);
    hole(5, 5, 5, 5);
    hole(1, 8, 2, 9);
    hole(7, 7, 9, 9);
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(cells[5 * N + 5] == PC_FLOOR_FILLED, "fill 1: a one-cell hole is filled");
    check(all_cells(1, 8, 2, 9, PC_FLOOR_FILLED), "fill 1: a two-cell hole is filled");
    check(all_cells(7, 7, 9, 9, PC_FLOOR_EMPTY), "fill 1: a three-cell hole stays empty");
    pc_floor_grid_finish(N, 2, lowest, has, occupied, heights, cells);
    check(all_cells(7, 7, 9, 9, PC_FLOOR_FILLED), "fill 2: the three-cell hole is filled");
    pc_floor_grid_finish(N, 0, lowest, has, occupied, heights, cells);
    check(cells[5 * N + 5] == PC_FLOOR_EMPTY, "fill 0: nothing is filled");

    /* An empty strip along the grid's edge (outside the walls, as wide as the closing or less) stays empty. */
    for (int fill = 1; fill <= 2; ++fill)
        for (int width = 1; width <= fill; ++width) {
            char what[96];
            clear();
            floor_rect(0, 0, N - 1 - width, N - 1, 0);
            pc_floor_grid_finish(N, fill, lowest, has, occupied, heights, cells);
            snprintf(what, sizeof what, "fill %d: a %d-cell empty strip along the grid's edge stays empty", fill, width);
            check(all_cells(N - width, 0, N - 1, N - 1, PC_FLOOR_EMPTY), what);
        }
    /* A notch open to the grid's edge is not a hole; one a cell in from it is. */
    clear();
    floor_rect(0, 0, 11, 11, 0);
    hole(5, 0, 5, 0);
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(cells[5] == PC_FLOOR_EMPTY, "fill 1: a notch open to the grid's edge stays empty");
    clear();
    floor_rect(0, 0, 11, 11, 0);
    hole(5, 1, 5, 1);
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(cells[1 * N + 5] == PC_FLOOR_FILLED, "fill 1: a hole one cell in from the edge is filled");

    /* Heights as before: each cell the lowest (largest y, y down) of its 3 x 3. */
    clear();
    floor_rect(0, 0, 11, 11, 0);
    floor_rect(3, 3, 3, 3, -100); /* a crate top */
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(fabsf(heights[3 * N + 3]) < 1e-3f && cells[3 * N + 3] == PC_FLOOR_SOLID,
        "a raised cell takes the floor around it (the lowest of its 3 x 3)");

    /* A slope: a floor (occupied) without a level face. */
    clear();
    floor_rect(0, 0, 11, 11, 0);
    has[6 * N + 6] = 0;
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(cells[6 * N + 6] == PC_FLOOR_SOLID, "a sloped floor is solid without a level face");

    clear();
    check(!pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells), "no floor: no grid");
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
