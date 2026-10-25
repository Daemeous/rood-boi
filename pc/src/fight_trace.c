/* Fight trace (RPC trace, pc/tools/encounter.py): one JSON line per frame in
 * which something changed, after JEschete's VS-NativePort Combat Lab trace
 * (CC0-1.0). Each line: the frame (pc_vsync_count), the random draws since
 * the last line and rand()'s state (boot_sdk.c), and the game's view
 * (encounter_lab.c pc_fight_snapshot: room, pad, action, characters).
 * Frames where nothing changed are left out. The first line says when and
 * why the trace began, the last why it ended. */
#include <stdio.h>
#include <string.h>
#include <time.h>

extern int pc_fight_snapshot(char*, int) __attribute__((weak));
extern void pc_fight_snapshot_reset(void) __attribute__((weak));
extern unsigned pc_vsync_count(void);
extern unsigned pc_rand_draws;
extern void pc_encounter_vsync(void) __attribute__((weak)); /* encounter_lab.c */
extern unsigned pc_rand_state(void);

static FILE* file;
static char path[512];
static unsigned frames_left, lines, frames_seen, draws_at;
static char last[16384], now[16384];

static void finish(const char* why)
{
    if (!file)
        return;
    fprintf(file, "{\"event\":\"end\",\"frame\":%u,\"why\":\"%s\",\"lines\":%u,\"frames\":%u}\n", pc_vsync_count(), why,
        lines, frames_seen);
    fclose(file);
    file = NULL;
}

/* frames 0: until stopped. note: the caller's (a preset's name), a JSON string or empty. */
int pc_fight_trace_start(const char* to, unsigned frames, const char* note)
{
    if (!pc_fight_snapshot)
        return 0;
    finish("restarted");
    file = fopen(to, "w");
    if (!file)
        return 0;
    snprintf(path, sizeof path, "%s", to);
    frames_left = frames, lines = frames_seen = 0;
    last[0] = 0;
    draws_at = pc_rand_draws;
    pc_fight_snapshot_reset();
    fprintf(file, "{\"event\":\"begin\",\"frame\":%u,\"time\":%lld,\"frames\":%u,\"note\":%s,\"rng\":%u}\n",
        pc_vsync_count(), (long long)time(NULL), frames, note && *note ? note : "null", pc_rand_state());
    return 1;
}

void pc_fight_trace_stop(void) { finish("stopped"); }

int pc_fight_trace_status(char* out, int size)
{
    char quoted[2 * sizeof path];
    int n = 0;
    for (const char* c = path; *c; c++) { /* Windows paths: escape the backslashes */
        if (*c == '\\' || *c == '"')
            quoted[n++] = '\\';
        quoted[n++] = *c;
    }
    quoted[n] = 0;
    return snprintf(out, size, "{\"tracing\":%s,\"path\":\"%s\",\"lines\":%u,\"frames\":%u,\"frames_left\":%u}",
        file ? "true" : "false", quoted, lines, frames_seen, frames_left);
}

/* Every game frame (boot_runtime.c), tracing or not: the encounter lab's
 * start of a fight first, then the trace's line. */
void pc_fight_trace_vsync(void)
{
    unsigned draws;
    if (pc_encounter_vsync)
        pc_encounter_vsync();
    if (!file)
        return;
    if (!pc_fight_snapshot(now, sizeof now))
        return;
    ++frames_seen;
    draws = pc_rand_draws - draws_at;
    if (draws || strcmp(now, last)) {
        fprintf(file, "{\"frame\":%u,\"draws\":%u,\"rng\":%u,\"game\":%s}\n", pc_vsync_count(), draws, pc_rand_state(),
            now);
        memcpy(last, now, strlen(now) + 1);
        draws_at = pc_rand_draws;
        ++lines;
    }
    if (frames_left && !--frames_left)
        finish("frames");
    else if (lines % 64 == 0)
        fflush(file);
}
