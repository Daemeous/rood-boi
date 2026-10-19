/* Frame cost (frame_cost.h): wall-clock sections and counters per vsync. */
#define _POSIX_C_SOURCE 200809L
#include "frame_cost.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int pc_cost_on;
static uint64_t sums[PC_COST_SECTIONS], counts[PC_COUNTERS];
static uint64_t vsyncs, game_from;
static const char* const section_names[PC_COST_SECTIONS] = { "game", "host", "draw", "raster", "emissive", "upload",
    "readback", "gpu_copy", "fence_wait", "present", "debug_ppm" };
static const char* const counter_names[PC_COUNTERS] = { "upload_kb", "uploads", "readbacks", "gpu_copies", "hashed_texels",
    "debug_ppm", "passes", "barriers", "raster_px", "skipped_px" };

__attribute__((constructor)) static void cost_init(void) { pc_cost_on = getenv("ROOD_PERF") != NULL; }
uint64_t pc_cost_clock(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
void pc_cost_end(int section, uint64_t start)
{
    if (pc_cost_on && start)
        sums[section] += pc_cost_clock() - start;
}
void pc_cost_count(int counter, unsigned n)
{
    if (pc_cost_on)
        counts[counter] += n;
}
void pc_cost_vsync(void)
{
    if (!pc_cost_on)
        return;
    if (game_from) {
        sums[PC_COST_GAME] += pc_cost_clock() - game_from;
        ++vsyncs;
    }
    game_from = 0;
}
void pc_cost_game_resumes(uint64_t start)
{
    if (!pc_cost_on)
        return;
    game_from = pc_cost_clock();
    if (start)
        sums[PC_COST_HOST] += game_from - start;
}
int pc_cost_json(char* out, int size, int reset, const char* extra)
{
    if (size <= 0)
        return 0;
    double per = vsyncs ? 1e-6 / (double)vsyncs : 0.0;
    int n = snprintf(out, (size_t)size, ",\"cpu_vsyncs\":%llu,\"cpu_sections\":{\"vsync\":%.4f",
        (unsigned long long)vsyncs, (double)(sums[PC_COST_GAME] + sums[PC_COST_HOST]) * per);
    for (int i = 0; i < PC_COST_SECTIONS && n < size; i++)
        n += snprintf(out + n, (size_t)(size - n), ",\"%s\":%.4f", section_names[i], (double)sums[i] * per);
    if (extra && *extra && n < size)
        n += snprintf(out + n, (size_t)(size - n), ",%s", extra);
    if (n < size)
        n += snprintf(out + n, (size_t)(size - n), "},\"cpu_counts\":{");
    for (int i = 0; i < PC_COUNTERS && n < size; i++)
        n += snprintf(out + n, (size_t)(size - n), "%s\"%s\":%.2f", i ? "," : "", counter_names[i],
            (double)counts[i] * per * 1e6);
    if (n < size)
        n += snprintf(out + n, (size_t)(size - n), "}");
    if (reset) {
        for (int i = 0; i < PC_COST_SECTIONS; i++)
            sums[i] = 0;
        for (int i = 0; i < PC_COUNTERS; i++)
            counts[i] = 0;
        vsyncs = 0;
    }
    return n < size ? n : size - 1;
}
