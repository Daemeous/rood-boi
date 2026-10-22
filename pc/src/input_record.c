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
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned pc_vsync_count(void);

typedef struct {
    unsigned vsync;
    char kind; /* 'B' or 'S' */
    int a, b;
    unsigned buttons;
} input_event;

static int mode = -1; /* -1 unknown, 0 off, 1 recording, 2 replaying, 3 replay over */
static FILE* record_file;
static input_event* events;
static size_t event_count, next_button, next_stick;
static unsigned replay_end;
static unsigned last_buttons;
static int have_buttons;
static int last_stick[2] = { 1 << 20, 1 << 20 };
static unsigned replay_buttons;
static int replay_stick[2];
static char path[1024];
static unsigned record_tick;
static int exit_registered;

static void finish_recording(void)
{
    if (record_file) {
        unsigned end = pc_vsync_count();
        /* Rewind notification arrives after the clock has moved back. */
        if (end < record_tick)
            end = record_tick < UINT_MAX ? record_tick + 1 : UINT_MAX;
        fprintf(record_file, "E %u\n", end);
        fclose(record_file);
        record_file = NULL;
        fprintf(stderr, "[input] recording saved: %s\n", path);
    }
}

static void seek_start(void)
{
    next_button = next_stick = 0;
    replay_buttons = 0;
    replay_stick[0] = replay_stick[1] = 0;
}

/* Check ranges before conversion: scanf's integer overflow is undefined. */
static int number(char** cursor, int base, long long min, long long max, long long* value)
{
    char* p = *cursor;
    char* end;
    if (!isspace((unsigned char)*p))
        return 0;
    while (isspace((unsigned char)*p))
        p++;
    errno = 0;
    *value = strtoll(p, &end, base);
    if (p == end || errno || *value < min || *value > max)
        return 0;
    *cursor = end;
    return 1;
}

/* Load into temporary storage; a bad file must not destroy a running replay. */
static int load_replay(const char* file)
{
    FILE* f;
    input_event* loaded = NULL;
    size_t count = 0, capacity = 0;
    unsigned end = 0, previous = 0;
    int ended = 0;
    char line[128];
    if (strlen(file) >= sizeof path || !(f = fopen(file, "r")))
        return -1;
    while (fgets(line, sizeof line, f)) {
        input_event e = { 0 };
        char* p = line;
        long long tick, a = 0, b = 0;
        if (!strchr(line, '\n') && !feof(f))
            goto invalid;
        while (isspace((unsigned char)*p))
            p++;
        if (!*p || *p == '#')
            continue;
        e.kind = *p++;
        if (ended || !number(&p, 10, 0, UINT_MAX, &tick) || tick < previous)
            goto invalid;
        if (e.kind == 'B') {
            if (!number(&p, 16, 0, UINT_MAX, &a))
                goto invalid;
            e.buttons = (unsigned)a;
        } else if (e.kind == 'S') {
            if (!number(&p, 10, -256, 256, &a) || !number(&p, 10, -256, 256, &b))
                goto invalid;
            e.a = (int)a;
            e.b = (int)b;
        } else if (e.kind != 'E') {
            goto invalid;
        }
        while (isspace((unsigned char)*p))
            p++;
        if (*p)
            goto invalid;
        previous = e.vsync = (unsigned)tick;
        if (e.kind == 'E') {
            end = e.vsync;
            ended = 1;
            continue;
        }
        if (count == capacity) {
            size_t grown_capacity = capacity ? capacity * 2 : 1024;
            input_event* grown;
            if (grown_capacity < capacity || grown_capacity > SIZE_MAX / sizeof *loaded)
                goto invalid;
            grown = realloc(loaded, grown_capacity * sizeof *loaded);
            if (!grown)
                goto invalid;
            loaded = grown;
            capacity = grown_capacity;
        }
        loaded[count++] = e;
    }
    if (ferror(f) || (!ended && (!count || previous == UINT_MAX)))
        goto invalid;
    /* Accept flushed recordings from a process that exited without its E line. */
    if (!ended)
        end = previous + 1;
    fclose(f);
    free(events);
    events = loaded;
    event_count = count;
    replay_end = end;
    seek_start();
    memmove(path, file, strlen(file) + 1);
    mode = 2;
    return 2;
invalid:
    fclose(f);
    free(loaded);
    fprintf(stderr, "[input] invalid replay or allocation failure: %s\n", file);
    return -1;
}

static int open_recording(const char* file)
{
    FILE* f;
    if (strlen(file) >= sizeof path)
        return -1;
    /* Do not truncate the same file while its old stream is still open. */
    if (mode == 1) {
        finish_recording();
        mode = 0;
    }
    f = fopen(file, "w");
    if (!f)
        return -1;
    if (fprintf(f, "# rood-boi input recording (pc/src/input_record.c)\n") < 0
        || fflush(f) || (!exit_registered && atexit(finish_recording))) {
        fclose(f);
        return -1;
    }
    exit_registered = 1;
    record_file = f;
    memmove(path, file, strlen(file) + 1);
    free(events);
    events = NULL;
    event_count = 0;
    replay_end = 0;
    seek_start();
    have_buttons = 0;
    last_stick[0] = last_stick[1] = 1 << 20;
    record_tick = pc_vsync_count();
    mode = 1;
    return 1;
}

static int start(void)
{
    const char* file;
    if (mode >= 0)
        return mode;
    mode = 0;
    if ((file = getenv("ROOD_REPLAY")) && *file)
        load_replay(file);
    else if ((file = getenv("ROOD_RECORD")) && *file)
        open_recording(file);
    return mode;
}

int pc_input_record_rpc(const char* file)
{
    start();
    if (mode == 2 || mode == 3)
        return -1;
    if (!file || !*file) {
        finish_recording();
        mode = 0;
        return 0;
    }
    return open_recording(file) == 1 ? 0 : -1;
}

int pc_input_replay_rpc(const char* file)
{
    start();
    if (mode == 1)
        return -1;
    if (!file || !*file) {
        free(events);
        events = NULL;
        event_count = 0;
        replay_end = 0;
        seek_start();
        mode = 0;
        return 0;
    }
    return load_replay(file) == 2 ? 0 : -1;
}

void pc_input_rewound(void)
{
    if (mode == 1) {
        finish_recording();
        mode = 0;
    } else if (mode == 2 || mode == 3) {
        seek_start();
        mode = 2;
    }
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
            replay_buttons = events[next_button].buttons;
    }
    for (; next_stick < event_count; next_stick++) {
        if (events[next_stick].vsync > vsync)
            break;
        if (events[next_stick].kind == 'S')
            replay_stick[0] = events[next_stick].a, replay_stick[1] = events[next_stick].b;
    }
}

/* The cutscene player's take (cutscene.cpp): logs the pad and stick the game read per vsync and, while
 * a part of the take replays, gives the logged values back (a file replay wins over it). */
extern int pc_take_buttons(unsigned vsync, unsigned* pad) __attribute__((weak));
extern int pc_take_stick(unsigned vsync, float stick[2], int* valid) __attribute__((weak));

/* The pad for this vblank: live (recorded) or replayed. `live` is the
 * keyboard and gamepad, `rpc` the developer RPC's input: both are recorded
 * (so a route driven over the RPC can be recorded too), and neither applies
 * while a replay runs. */
unsigned pc_input_buttons(unsigned live, unsigned rpc)
{
    unsigned vsync = pc_vsync_count();
    unsigned pad = live | rpc;
    if (pc_take_buttons && start() != 2)
        pc_take_buttons(vsync, &pad);
    switch (start()) {
    case 1:
        record_tick = vsync;
        if (!have_buttons || pad != last_buttons) {
            have_buttons = 1;
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

extern int pc_rpc_stick(float out[2]) __attribute__((weak));
/* The camera stick: combine all live sources before recording/replay, like
 * buttons. camera_stick.c must not add the RPC stick a second time. */
int pc_input_stick(float out[2], int live_valid)
{
    unsigned vsync = pc_vsync_count();
    float rpc[2];
    if (!live_valid) out[0] = out[1] = 0;
    if (pc_rpc_stick && pc_rpc_stick(rpc)) {
        out[0] += rpc[0]; out[1] += rpc[1];
        live_valid = 1;
    }
    for (int k = 0; k < 2; k++) {
        if (out[k] != out[k]) out[k] = 0; /* NaN is not a pad value. */
        out[k] = out[k] > 1 ? 1 : out[k] < -1 ? -1 : out[k];
    }
    int q[2];
    if (pc_take_stick && start() != 2)
        pc_take_stick(vsync, out, &live_valid); /* logged as it is (no quantising: live play unchanged) */
    switch (start()) {
    case 1:
        record_tick = vsync;
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
/* (A take does not switch to virtual scanlines: it logs the measured ones, boot_runtime.c.) */

/* For the RPC: 0 off, 1 recording, 2 replaying, 3 replay over; the file; the replay's end. */
int pc_input_record_state(const char** file, unsigned* end)
{
    start();
    *file = path;
    *end = replay_end;
    return mode;
}
