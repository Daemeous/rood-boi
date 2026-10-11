/* Where the runtime writes diagnostics and reads per-room lights, so that
 * parallel test runs (pc/tests/run.py) cannot overwrite each other's files
 * or pick up the user's lighting edits:
 *   ROOD_OUT     diagnostic output (frames, checkpoints, dumps): pc/build/boot
 *   ROOD_LIGHTS  the user lighting layer (lighting_layers.h): pc/user/lighting
 * Paths are relative to the repository root (the game's working directory). */
#ifndef PC_PATHS_H
#define PC_PATHS_H
#include <stdio.h>
#include <stdlib.h>

static inline const char* pc_path_join(const char* variable, const char* fallback, const char* name)
{
    /* A few rotating buffers so one call can take two paths (rename). */
    static char buffers[4][512];
    static int next;
    char* out = buffers[next++ & 3];
    const char* dir = getenv(variable);
    snprintf(out, sizeof buffers[0], "%s/%s", dir && *dir ? dir : fallback, name);
    return out;
}
static inline const char* pc_out_path(const char* name)
{
    return pc_path_join("ROOD_OUT", "pc/build/boot", name);
}
#endif
