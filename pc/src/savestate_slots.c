/* Save state slots for players, as in an emulator: F5 saves the game's whole
 * state to the current slot, F9 loads it, F6 / F7 choose the slot (1-10).
 * The files are pc/save/states/slot<N>.snap (snapshot.c's format). A state
 * loads in the build that made it only (snapshot.c: the code key), so an
 * update of the game makes older states unloadable; the memory cards are not
 * part of a state. Keys bound to a PS1 button (bind_key_*) stay the pad's.
 *
 * The key press only asks; the save or load happens in the next vsync's
 * pc_tick (dev_rpc.c's poll calls pc_savestate_poll), where the game's stack
 * is at the vsync boundary. A load replaces the running game in place
 * (pc_snapshot_load_now) and does not return. dev_overlay.cpp draws the
 * message (pc_savestate_message). */
#include "platform.h"
#include <SDL3/SDL_keycode.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern int pc_snapshot_available(void);                                 /* snapshot.c */
extern int pc_snapshot_point(const char* path, char* error, int size);  /* snapshot.c */
extern int pc_snapshot_load_now(const char* path, char* error, int size);
extern int pc_snapshot_check(const char* path, char* why, int size);

#define SLOTS 10
#define MESSAGE_SECONDS 2.5
static int slot = 1;
static enum { NONE, SAVE, LOAD } request;
static char message[200];
static double message_until;

static double now_seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void say(const char* text)
{
    snprintf(message, sizeof message, "%s", text);
    message_until = now_seconds() + MESSAGE_SECONDS;
    fprintf(stderr, "[savestate] %s\n", text);
}

static void slot_path(char* out, size_t size) { snprintf(out, size, "pc/save/states/slot%d.snap", slot); }

/* "Slot 3: saved 2026-10-19 14:32", "Slot 3: empty", or why it cannot load. */
static void describe_slot(void)
{
    char path[64], text[200], why[160];
    slot_path(path, sizeof path);
    struct stat st;
    if (stat(path, &st)) {
        snprintf(text, sizeof text, "Slot %d: empty", slot);
    } else if (!pc_snapshot_check(path, why, (int)sizeof why)) {
        snprintf(text, sizeof text, "Slot %d: cannot load in this version of the game", slot);
    } else {
        char when[32] = "";
        time_t t = st.st_mtime;
        struct tm* tm = localtime(&t);
        if (tm)
            strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
        snprintf(text, sizeof text, "Slot %d: saved %s", slot, when);
    }
    say(text);
}

/* boot_window.c: a key press (not a repeat) that no PS1 button is bound to.
 * 1 when it is a save state key. */
int pc_savestate_key(int key)
{
    if (key != SDLK_F5 && key != SDLK_F6 && key != SDLK_F7 && key != SDLK_F9)
        return 0;
    if (!pc_snapshot_available()) {
        say("Save states are not available in this session (the game stack is not at its fixed address)");
        return 1;
    }
    char text[80];
    if (key == SDLK_F6 || key == SDLK_F7) {
        slot = key == SDLK_F6 ? (slot == 1 ? SLOTS : slot - 1) : (slot == SLOTS ? 1 : slot + 1);
        describe_slot();
    } else if (key == SDLK_F5) {
        request = SAVE;
        snprintf(text, sizeof text, "Saving to slot %d...", slot);
        say(text);
    } else {
        request = LOAD;
        snprintf(text, sizeof text, "Loading slot %d...", slot);
        say(text);
    }
    return 1;
}

/* dev_rpc.c's poll, inside pc_tick at the vsync boundary. */
void pc_savestate_poll(void)
{
    if (request == NONE)
        return;
    int what = request;
    request = NONE;
    char path[64], error[256], text[320];
    slot_path(path, sizeof path);
    if (what == SAVE) {
        if (pc_snapshot_point(path, error, (int)sizeof error) == 0)
            snprintf(text, sizeof text, "Saved to slot %d", slot);
        else
            snprintf(text, sizeof text, "Slot %d not saved: %s", slot, error);
        say(text);
        return;
    }
    struct stat st;
    if (stat(path, &st)) {
        snprintf(text, sizeof text, "Slot %d is empty", slot);
        say(text);
        return;
    }
    if (!pc_snapshot_check(path, error, (int)sizeof error)) {
        snprintf(text, sizeof text, "Slot %d was saved by another version of the game: it cannot load", slot);
        say(text);
        fprintf(stderr, "[savestate] %s\n", error);
        return;
    }
    snprintf(text, sizeof text, "Loaded slot %d", slot);
    say(text); /* shown after the load: this message is host state */
    pc_snapshot_load_now(path, error, (int)sizeof error); /* does not return when it loads */
    snprintf(text, sizeof text, "Slot %d not loaded: %s", slot, error);
    say(text);
}

/* dev_overlay.cpp: the message to show now, or NULL. */
const char* pc_savestate_message(void) { return message[0] && now_seconds() < message_until ? message : NULL; }
