/* Save state slots for players, as in an emulator, while the setting
 * save_states is on (off by default): the bound keys or gamepad buttons
 * (boot_controller.h PC_BIND_STATE_*: by default F5 saves the current slot,
 * F9 loads it, F6 / F7 choose the slot, 1-10) act on
 * pc/save/states/slot<N>.snap (snapshot.c's format). A state loads in the
 * build that made it only (snapshot.c: the code key), so an update of the
 * game makes older states unloadable; the memory cards are not part of a
 * state.
 *
 * A host unit (build_boot.py HOST_UNITS): the engine's input calls in
 * (pc_savestate_actions: boot_window.c's keys, boot_gamepad.c's buttons);
 * the press only asks, and the save or load happens in the next vsync's
 * pc_tick (dev_rpc.c's poll calls pc_savestate_poll), where the game's stack
 * is at the vsync boundary. A load replaces the running game in place
 * (pc_snapshot_load_now) and does not return. What happened is a notice
 * (pc_savestate_notice): savestate_notice.cpp makes it the HUD element
 * "notice" (pc/src/hud), which rood.hud draws. */
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

extern int pc_snapshot_available(void);                                 /* snapshot.c */
extern int pc_snapshot_point(const char* path, char* error, int size);  /* snapshot.c */
extern int pc_snapshot_load_now(const char* path, char* error, int size);
extern int pc_snapshot_check(const char* path, char* why, int size);

/* settings save_states (project_settings.cpp): the keys act only while on. */
int pc_save_states;

#define SLOTS 10
#define NOTICE_VSYNCS 150 /* about 2.5 s */
enum { ACTION_SAVE = 1, ACTION_LOAD = 2, ACTION_PREV = 4, ACTION_NEXT = 8 }; /* PC_BIND_STATE_* order */
static int slot = 1;
static enum { NONE, SAVE, LOAD } request;
static char notice[32]; /* fits the HUD's element text (hud.h PC_HUD_TEXT) */
static double notice_time; /* wall clock: the vsync count is the game's (a load moves it) */
static int notice_slot;

static double now_seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void say(const char* fmt, int n)
{
    snprintf(notice, sizeof notice, fmt, n);
    notice_time = now_seconds();
    notice_slot = n;
    fprintf(stderr, "[savestate] %s\n", notice);
}

static void slot_path(char* out, size_t size) { snprintf(out, size, "pc/save/states/slot%d.snap", slot); }

/* "Slot 3: 10-19 14:32", "Slot 3: empty", or that it cannot load. */
static void describe_slot(void)
{
    char path[64], why[160];
    slot_path(path, sizeof path);
    struct stat st;
    if (stat(path, &st)) {
        say("Slot %d: empty", slot);
    } else if (!pc_snapshot_check(path, why, (int)sizeof why)) {
        say("Slot %d: another version", slot);
    } else {
        char when[16] = "", fmt[32];
        time_t t = st.st_mtime;
        struct tm* tm = localtime(&t);
        if (tm)
            strftime(when, sizeof when, "%m-%d %H:%M", tm);
        snprintf(fmt, sizeof fmt, "Slot %%d: %s", when);
        say(fmt, slot);
    }
}

/* The engine's input: the save state actions pressed now (bit n: PC_BIND_STATE_FIRST + n). */
void pc_savestate_actions(unsigned bits)
{
    if (!pc_save_states || !bits)
        return;
    if (!pc_snapshot_available()) {
        say("Save states unavailable", 0);
        return;
    }
    if (bits & (ACTION_PREV | ACTION_NEXT)) {
        if (bits & ACTION_PREV)
            slot = slot == 1 ? SLOTS : slot - 1;
        if (bits & ACTION_NEXT)
            slot = slot == SLOTS ? 1 : slot + 1;
        describe_slot();
    }
    if (bits & ACTION_SAVE) {
        request = SAVE;
        say("Saving to slot %d...", slot);
    } else if (bits & ACTION_LOAD) {
        request = LOAD;
        say("Loading slot %d...", slot);
    }
}

/* dev_rpc.c's poll, inside pc_tick at the vsync boundary. */
void pc_savestate_poll(void)
{
    if (request == NONE)
        return;
    int what = request;
    request = NONE;
    char path[64], error[256];
    slot_path(path, sizeof path);
    if (what == SAVE) {
        if (pc_snapshot_point(path, error, (int)sizeof error) == 0)
            say("Saved to slot %d", slot);
        else {
            fprintf(stderr, "[savestate] slot %d not saved: %s\n", slot, error);
            say("Slot %d not saved", slot);
        }
        return;
    }
    struct stat st;
    if (stat(path, &st)) {
        say("Slot %d is empty", slot);
        return;
    }
    if (!pc_snapshot_check(path, error, (int)sizeof error)) {
        fprintf(stderr, "[savestate] slot %d: %s\n", slot, error);
        say("Slot %d: another version", slot);
        return;
    }
    say("Loaded slot %d", slot); /* shown after the load: this notice is host state */
    pc_snapshot_load_now(path, error, (int)sizeof error); /* does not return when it loads */
    fprintf(stderr, "[savestate] slot %d not loaded: %s\n", slot, error);
    say("Slot %d not loaded", slot);
}

/* savestate_notice.cpp: the notice shown now (1), its text, slot, age and life in vsyncs. */
int pc_savestate_notice(char* text, int size, int* notice_slot_out, int* age, int* life)
{
    double elapsed = (now_seconds() - notice_time) * 59.94; /* in vsyncs */
    if (!notice[0] || elapsed < 0 || elapsed >= NOTICE_VSYNCS)
        return 0;
    snprintf(text, (size_t)size, "%s", notice);
    *notice_slot_out = notice_slot;
    *age = (int)elapsed;
    *life = NOTICE_VSYNCS;
    return 1;
}
