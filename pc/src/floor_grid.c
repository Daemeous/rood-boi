/* The room's floor grid's last steps (floor_grid.h). */
#include <string.h>

#include "floor_grid.h"

enum { M = PC_FLOOR_GRID_MAX };

int pc_floor_grid_finish(int n, int fill, const float* lowest, const unsigned char* has, const unsigned char* occupied,
    float* heights, unsigned char* cells)
{
    static float grown[M * M];
    static unsigned char known[M * M], next[M * M], ground[M * M], dilated[M * M];
    if (n <= 0 || n > M)
        return 0;
    int any = 0;
    for (int k = 0; k < n * n; ++k)
        any |= has[k];
    if (!any)
        return 0;
    /* each cell the lowest of its 3 x 3 (cells with a level floor) */
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int at = j * n + i, found = 0;
            float y = 0;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    int x = i + di, z = j + dj;
                    if (x < 0 || z < 0 || x >= n || z >= n || !has[z * n + x])
                        continue;
                    float h = lowest[z * n + x];
                    if (!found || h > y)
                        y = h, found = 1;
                }
            grown[at] = y, next[at] = (unsigned char)found;
        }
    /* cells still without one: the mean of their neighbours', growing outward */
    for (int round = 0; round < 2 * n; ++round) {
        int missing = 0;
        memcpy(known, next, (size_t)(n * n));
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                int at = j * n + i;
                if (known[at])
                    continue;
                float sum = 0;
                int count = 0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        int x = i + di, z = j + dj;
                        if (x >= 0 && z >= 0 && x < n && z < n && known[z * n + x])
                            sum += grown[z * n + x], count++;
                    }
                if (count)
                    grown[at] = sum / count, next[at] = 1;
                else
                    missing++;
            }
        if (!missing)
            break;
    }
    memcpy(heights, grown, sizeof(float) * (size_t)(n * n));
    /* the room's interior: its floor, and the holes a closing fills */
    if (fill < 0)
        fill = 0;
    for (int k = 0; k < n * n; ++k)
        ground[k] = occupied[k] || has[k];
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int hit = 0;
            for (int dj = -fill; dj <= fill && !hit; ++dj)
                for (int di = -fill; di <= fill && !hit; ++di) {
                    int x = i + di, z = j + dj;
                    hit = x >= 0 && z >= 0 && x < n && z < n && ground[z * n + x];
                }
            dilated[j * n + i] = (unsigned char)hit;
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int at = j * n + i, closed = 1;
            for (int dj = -fill; dj <= fill && closed; ++dj)
                for (int di = -fill; di <= fill && closed; ++di) {
                    int x = i + di, z = j + dj;
                    /* past the grid's edge: floor only where the edge cell is (a floor reaching the
                     * bounds keeps its edge; an empty strip along the edge stays empty) */
                    if (x < 0 || z < 0 || x >= n || z >= n) {
                        x = x < 0 ? 0 : x >= n ? n - 1 : x, z = z < 0 ? 0 : z >= n ? n - 1 : z;
                        closed = ground[z * n + x];
                    } else
                        closed = dilated[z * n + x];
                }
            cells[at] = ground[at] ? PC_FLOOR_SOLID : closed ? PC_FLOOR_FILLED : PC_FLOOR_EMPTY;
        }
    return 1;
}
