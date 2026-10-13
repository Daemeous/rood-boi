/* Input recording and replay, for repeatable tests of a route through the game.
 *
 *   ROOD_RECORD=path   write the pad (as the game reads it at each vblank) and the
 *                      right-stick camera to `path` while playing
 *   ROOD_REPLAY=path   play such a file back from the start, instead of the
 *                      keyboard, gamepad and developer RPC buttons; they return
 *                      when it ends
 * (The RPC's buttons are recorded with the live ones, so a route driven over
 * the RPC can be recorded too.)
 *
 * Both start with the process: a replay reproduces a session from boot, so it
 * needs the same memory card contents (copy the card beside the recording)
 * and the same game arguments (--skip-movies). Events are
 * keyed by vblank (pc_vsync_count), the game's own clock, so a replay is
 * frame-exact at any speed (--fast, fast-forward).
 *
 * File: one event per line, written when a value changes:
 *   B <vsync> <buttons hex>      PS1 pad bits (pc_window_buttons, before R3's
 *                                camera handling)
 *   S <vsync> <x> <y>            camera stick, -256..256 (camera_stick.c)
 *   E <vsync>                    end of the recording
 * Lines starting with # are comments (the header). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned pc_vsync_count(void);

typedef struct {
    unsigned vsync;
    char kind; /* 'B' or 'S' */
    int a, b;
} input_event;

static int mode = -1; /* -1 unknown, 0 off, 1 recording, 2 replaying, 3 replay over */
static FILE* record_file;
static input_event* events;
static int event_count, next_button, next_stick;
static unsigned replay_end;
static unsigned last_buttons = ~0u;
static int last_stick[2] = { 1 << 20, 1 << 20 };
static int replay_buttons, replay_stick[2];
static const char* path;

static void finish_recording(void)
{
    if (record_file) {
        fprintf(record_file, "E %u\n", pc_vsync_count());
        fclose(record_file);
        record_file = NULL;
        fprintf(stderr, "[input] recording saved: %s\n", path);
    }
}

static int start(void)
{
    if (mode >= 0)
        return mode;
    mode = 0;
    if ((path = getenv("ROOD_REPLAY")) && *path) {
        FILE* f = fopen(path, "r");
        char line[128];
        int capacity = 0;
        if (!f) {
            fprintf(stderr, "[input] cannot read replay %s\n", path);
            return mode;
        }
        while (fgets(line, sizeof line, f)) {
            input_event e = { 0 };
            if (line[0] == 'E' && sscanf(line + 1, "%u", &replay_end) == 1)
                continue;
            if ((line[0] == 'B' && sscanf(line + 1, "%u %x", &e.vsync, (unsigned*)&e.a) == 2)
                || (line[0] == 'S' && sscanf(line + 1, "%u %d %d", &e.vsync, &e.a, &e.b) == 3)) {
                e.kind = line[0];
                if (event_count == capacity) {
                    input_event* grown = realloc(events, (capacity ? capacity * 2 : 1024) * sizeof *events);
                    if (!grown)
                        break;
                    events = grown;
                    capacity = capacity ? capacity * 2 : 1024;
                }
                events[event_count++] = e;
            }
        }
        fclose(f);
        if (!replay_end && event_count)
            replay_end = events[event_count - 1].vsync + 1;
        mode = 2;
        fprintf(stderr, "[input] replaying %s: %d events, %u vblanks\n", path, event_count, replay_end);
    } else if ((path = getenv("ROOD_RECORD")) && *path) {
        record_file = fopen(path, "w");
        if (!record_file) {
            fprintf(stderr, "[input] cannot write recording %s\n", path);
            return mode;
        }
        fprintf(record_file, "# rood-boi input recording (pc/src/input_record.c)\n");
        atexit(finish_recording);
        mode = 1;
        fprintf(stderr, "[input] recording to %s\n", path);
    }
    return mode;
}

/* Advance the replay to `vsync`: the latest value of each kind at or before it. */
static void replay_to(unsigned vsync)
{
    if (vsync >= replay_end) {
        mode = 3;
        fprintf(stderr, "[input] replay finished at vblank %u: live input from here\n", vsync);
        return;
    }
    for (; next_button < event_count; next_button++) {
        if (events[next_button].vsync > vsync)
            break;
        if (events[next_button].kind == 'B')
            replay_buttons = events[next_button].a;
    }
    for (; next_stick < event_count; next_stick++) {
        if (events[next_stick].vsync > vsync)
            break;
        if (events[next_stick].kind == 'S')
            replay_stick[0] = events[next_stick].a, replay_stick[1] = events[next_stick].b;
    }
}

/* The pad for this vblank: live (recorded) or replayed. `live` is the
 * keyboard and gamepad, `rpc` the developer RPC's input: both are recorded
 * (so a route driven over the RPC can be recorded too), and neither applies
 * while a replay runs. */
unsigned pc_input_buttons(unsigned live, unsigned rpc)
{
    unsigned vsync = pc_vsync_count();
    unsigned pad = live | rpc;
    switch (start()) {
    case 1:
        if (pad != last_buttons) {
            fprintf(record_file, "B %u %x\n", vsync, pad);
            fflush(record_file); /* kept if the game stops without exiting */
            last_buttons = pad;
        }
        return pad;
    case 2:
        replay_to(vsync);
        if (mode == 2)
            return (unsigned)replay_buttons;
        return pad;
    default:
        return pad;
    }
}

/* The camera stick (-1..1): records `live`, or replaces it while replaying. */
int pc_input_stick(float out[2], int live_valid)
{
    unsigned vsync = pc_vsync_count();
    int q[2];
    switch (start()) {
    case 1:
        for (int k = 0; k < 2; k++)
            q[k] = live_valid ? (int)(out[k] * 256 + (out[k] < 0 ? -0.5f : 0.5f)) : 0;
        if (q[0] != last_stick[0] || q[1] != last_stick[1]) {
            fprintf(record_file, "S %u %d %d\n", vsync, q[0], q[1]);
            fflush(record_file);
            last_stick[0] = q[0], last_stick[1] = q[1];
        }
        /* What is recorded is what is used: the quantised value. */
        out[0] = q[0] / 256.0f, out[1] = q[1] / 256.0f;
        return live_valid;
    case 2:
        replay_to(vsync);
        if (mode == 2) {
            out[0] = replay_stick[0] / 256.0f, out[1] = replay_stick[1] / 256.0f;
            return 1;
        }
        return live_valid;
    default:
        return live_valid;
    }
}

/* While recording or replaying (and after a replay), the game's scanline timer
 * reads virtual scanlines (boot_runtime.c func_800E45B4): the AI uses it as a
 * per-frame time budget, so measured host time would make a replay diverge. */
int pc_input_deterministic(void) { return start() > 0; }

/* For the RPC: 0 off, 1 recording, 2 replaying, 3 replay over; the file; the replay's end. */
int pc_input_record_state(const char** file, unsigned* end)
{
    start();
    *file = path ? path : "";
    *end = replay_end;
    return mode;
}
