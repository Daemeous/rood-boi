// The photo mode foundation, the host side (photo.h; pc/docs/Extensions.md "Photo mode
// foundation"; the Lua side: pc/src/lua/lua_api_photo.cpp and lua_photo_*.cpp): the pause and its
// renders, the RPC, what the parts share (photo_internal.h); the capabilities' own parts are
// photo_camera.cpp, photo_ui.cpp, photo_capture.cpp and photo_input.cpp.
//
// The pause holds the game at a vsync boundary, M, in dev_rpc.c's paused loop (the same
// loop the RPC's pause uses, with its own flag: neither releases the other). While held,
// what changes the picture (the camera, the hidden layers) asks for a render: the game
// runs from M with nothing to see or hear (pc_photo_rendering: no presents, no audio fed,
// no input, no Lua ticks, deterministic timing, no memory card writes, RPC requests wait)
// until it has drawn render_frames frames, then its state is rewound to M
// (state_rewind.c's photo slot: the picture kept). So the game's state, its vsync count
// and digest, stay M's the whole time; only the picture moves. A render takes the CPU time
// of a few game frames (no pacing), about 20-40 ms.
#include "photo.h"

#include "photo_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

#include "hud/game_query.h"

extern "C" {
int pc_photo_rendering;
void pc_audio_frame(void) __attribute__((weak));               /* audio_libspu.c: a vsync's sound */
void pc_audio_output_duck(float gain) __attribute__((weak));   /* audio_output.c */

/* state_rewind.c (absent where save states are: weak) */
int pc_snapshot_mark_slot(int slot, char* error, int size) __attribute__((weak));
int pc_snapshot_rewind_slot(int slot, char* error, int size) __attribute__((weak));
void pc_snapshot_drop_slot(int slot) __attribute__((weak));
int pc_snapshot_slot_valid(int slot) __attribute__((weak));
unsigned pc_vsync_count(void);
unsigned pc_battle_frame_count(void) __attribute__((weak));
void pc_hw_resync(void) __attribute__((weak));
int pc_hw_photo_mark(void) __attribute__((weak));
int pc_hw_photo_prepare(void) __attribute__((weak));
int pc_hw_photo_restore(void) __attribute__((weak));
void pc_hw_photo_drop(void) __attribute__((weak));
extern int pc_hw_active __attribute__((weak));
extern int pc_game_phase __attribute__((weak)); /* boot_runtime.c: 1 title, 2 attract demo */
extern unsigned int vs_battle_screenTransitionStep __attribute__((weak)); /* 146C.c: a room transition */
int pc_memory_card_busy(void) __attribute__((weak));                      /* memory_card.c: a card file open */
int pc_ui_data_visible(void) __attribute__((weak));                       /* the modern save screen */
}

namespace {
constexpr int SLOT = 1; /* state_rewind.c: the photo mode's */
enum Phase { RUNNING, ASKED, HELD, RENDERING };
const char* const PHASES[] = { "running", "asked", "held", "rendering" };
Phase phase = RUNNING;
std::vector<std::string> pausers;
bool redraw_wanted;  /* a render asked for */
bool restore_wanted; /* camera finish restores the presentation even if another owner still pauses */
bool view_borrowed, view_captured; /* checkpoint belongs to the camera session, not the whole pause */
bool landing;        /* a render's rewind is under way: the next poll is at M again */
bool rendered;       /* a render ran since the hold began (resync at resume) */
unsigned held_vsync; /* M */
unsigned render_vsyncs, render_start_frames, renders;
double render_start, last_render_ms;
int render_frames = 4;    /* game frames a render runs (RPC photo render_frames) */
int render_max_vsyncs = 40;
std::string last_error;

/* ---- the sound while held (pc_photo_pause_audio) ---- */
struct AudioWish {
    std::string owner;
    int mode;
    float gain;
};
std::vector<AudioWish> audio_wishes;
double audio_next;          /* when the next sound vsync is due (0: not started) */
bool audio_moved;           /* sound vsyncs ran since the mark: the next render marks again */
unsigned audio_vsyncs;      /* sound vsyncs run while held (RPC photo) */
constexpr double SOUND_VSYNC = 1001.0 / 60000.0;
int audio_mode(float* gain)
{
    int mode = PC_PHOTO_AUDIO_KEEP;
    *gain = 1;
    for (const std::string& o : pausers) {
        int m = PC_PHOTO_AUDIO_STOP;
        float g = 1;
        for (const AudioWish& w : audio_wishes)
            if (w.owner == o)
                m = w.mode, g = w.gain;
        mode = std::min(mode, m);
        if (m == PC_PHOTO_AUDIO_DUCK)
            *gain = std::min(*gain, g);
    }
    if (mode != PC_PHOTO_AUDIO_DUCK)
        *gain = 1;
    return pausers.empty() ? PC_PHOTO_AUDIO_STOP : mode;
}
/* Held: the sound driver's ticks and the SPU in real time (pc_audio_frame: what a vsync runs for the
 * sound, nothing else), a few at most to catch up after a render. */
void audio_pump()
{
    float gain;
    int mode = audio_mode(&gain);
    if (pc_audio_output_duck)
        pc_audio_output_duck(mode == PC_PHOTO_AUDIO_DUCK ? gain : 1);
    double now = photo::now_seconds();
    if (mode == PC_PHOTO_AUDIO_STOP || !pc_audio_frame) {
        audio_next = 0;
        return;
    }
    if (audio_next == 0 || now - audio_next > 0.25)
        audio_next = now;
    for (int n = 0; n < 8 && now >= audio_next; n++) {
        pc_audio_frame();
        audio_next += SOUND_VSYNC;
        audio_moved = true;
        ++audio_vsyncs;
    }
}

/* What makes holding the game unsafe now ("" none). */
std::string unsafe()
{
    if (pc_memory_card_busy && pc_memory_card_busy())
        return "memory card I/O in progress";
    if (pc_ui_data_visible && pc_ui_data_visible())
        return "the save screen is open (memory card I/O)";
    if (&pc_game_phase && pc_game_phase != 0)
        return ""; /* the title, an attract demo: nothing loads under a hold */
    pc_game_query q;
    memset(&q, 0, sizeof q);
    if (pc_game_query_fill) {
        pc_game_query_fill(&q);
        if (!q.field)
            return "loading (no room)";
    }
    if (&vs_battle_screenTransitionStep && vs_battle_screenTransitionStep != 0)
        return "a room transition";
    return "";
}

/* ---- notices: what was taken back from an owner without its asking ---- */
struct Notice {
    std::string owner, what, reason;
};
std::deque<Notice> notices;
} // namespace

/* ------------------------------------------------------------ what the parts share */

std::vector<photo::Part>& photo::parts()
{
    static auto* list = new std::vector<Part>();
    return *list;
}
double photo::now_seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
void photo::copy_out(char* out, int size, const std::string& text)
{
    if (out && size > 0)
        snprintf(out, (size_t)size, "%s", text.c_str());
}
bool photo::contains(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}
void photo::notify(const std::string& owner, const char* what, const char* reason)
{
    if (notices.size() < 64)
        notices.push_back({ owner, what, reason });
    fprintf(stderr, "[photo] %s: %s released (%s)\n", owner.c_str(), what, reason);
}
bool photo::json_int(const char* json, const char* key, int* out)
{
    std::string quoted = std::string("\"") + key + "\"";
    const char* p = json ? strstr(json, quoted.c_str()) : nullptr;
    if (!p || !(p = strchr(p + quoted.size(), ':')))
        return false;
    *out = atoi(p + 1);
    return true;
}
std::string photo::json_quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s)
        out += c == '"' || c == '\\' ? '\'' : c;
    return out + "\"";
}
bool photo::paused() { return phase != RUNNING; }
bool photo::settled() { return phase == RUNNING || (phase == HELD && !redraw_wanted && !restore_wanted && !landing); }
unsigned photo::game_frames() { return pc_battle_frame_count ? pc_battle_frame_count() : 0; }

using photo::contains;
using photo::copy_out;
using photo::now_seconds;

/* ------------------------------------------------------------ the poll (dev_rpc.c) */

extern "C" int pc_photo_holding(void) { return phase == HELD; }
extern "C" int pc_photo_restoring(void) { return restore_wanted; }
extern "C" const char* pc_photo_last_error(void) { return last_error.c_str(); }

extern "C" int pc_photo_prepare_view(void)
{
    if (restore_wanted || !pc_hw_photo_prepare || !pc_hw_photo_prepare())
        return 0;
    view_borrowed = true;
    view_captured = false;
    return 1;
}

extern "C" void pc_photo_restore_view(void)
{
    restore_wanted = true;
    redraw_wanted = false;
}

extern "C" void pc_photo_state_replaced(void)
{
    char owner[256];
    bool had_camera = pc_photo_camera_owner(owner, sizeof owner) != 0;
    if (had_camera) {
        pc_photo_camera_finish(owner);
        photo::notify(owner, "camera", "the game's state was replaced");
    }
    restore_wanted = false;
    view_borrowed = view_captured = false;
    // Called after guest memory changes, before hardware resync. Numeric vsync
    // equality is not identity: seeking to another mark at M also invalidates
    // the old image. Preserve other plugins' holds, but never restore that image.
    if (pc_hw_photo_drop)
        pc_hw_photo_drop();
    if (pc_snapshot_drop_slot)
        pc_snapshot_drop_slot(SLOT);
    redraw_wanted = !pausers.empty() && (had_camera || redraw_wanted || rendered);
    rendered = false;
    if (phase == RENDERING || landing)
        phase = HELD;
    landing = false;
    pc_photo_rendering = 0;
    held_vsync = pc_vsync_count();
    audio_next = 0;
    audio_moved = false;
}

static void redraw_failed()
{
    char owner[256];
    if (pc_photo_camera_owner(owner, sizeof owner)) {
        pc_photo_camera_finish(owner);
        photo::notify(owner, "camera", last_error.c_str());
    }
    redraw_wanted = false;
    if (!view_captured) {
        // No borrowed image was drawn: undo ownership, not the original picture.
        restore_wanted = view_borrowed = false;
        if (pc_hw_photo_drop)
            pc_hw_photo_drop();
    }
}

extern "C" int pc_photo_poll(void)
{
    if (landing) { /* at M again (state_rewind.c restored it), the render's picture kept */
        landing = false;
        pc_photo_rendering = 0;
        phase = HELD;
    }
    if (phase == RENDERING) {
        ++render_vsyncs;
        unsigned frames = pc_battle_frame_count ? pc_battle_frame_count() - render_start_frames : 0;
        if (((int)frames < render_frames || render_vsyncs < 2) && (int)render_vsyncs < render_max_vsyncs)
            return 1; /* this vsync runs too */
        last_render_ms = (now_seconds() - render_start) * 1000;
        ++renders;
        landing = true;
        char error[160] = "";
        if (pc_snapshot_rewind_slot)
            pc_snapshot_rewind_slot(SLOT, error, (int)sizeof error); /* does not return when it rewinds */
        /* It could not: the game is a render ahead of M (never seen so far: the mark was taken). */
        landing = false;
        pc_photo_rendering = 0;
        phase = HELD;
        last_error = std::string("rewind failed: ") + error;
        fprintf(stderr, "[photo] %s (the game is %u vsyncs past the pause)\n", last_error.c_str(), render_vsyncs);
        return 0;
    }
    if (phase == ASKED) {
        if (pausers.empty()) {
            phase = RUNNING;
            restore_wanted = redraw_wanted = false;
            view_borrowed = view_captured = false;
            if (pc_hw_photo_drop)
                pc_hw_photo_drop();
            return 0;
        }
        std::string why = unsafe();
        if (!why.empty())
            return 0; /* asked again at the next boundary */
        phase = HELD;
        held_vsync = pc_vsync_count();
        rendered = false;
        audio_next = 0;
        audio_vsyncs = 0;
        fprintf(stderr, "[photo] held at vsync %u (%s)\n", held_vsync, pausers.front().c_str());
    }
    if (phase != HELD)
        return 0;
    if (pc_vsync_count() != held_vsync) {
        /* Another state replaced the held one (a save state loaded, the RPC's rewind): held there now. */
        fprintf(stderr, "[photo] the game's state was replaced (vsync %u, held %u): held there\n", pc_vsync_count(),
            held_vsync);
        pc_photo_state_replaced();
    }
    // The game is held at M, but its last presentation may be M-1. Restore
    // that renderer checkpoint rather than running M again with no camera
    // override (which produces a different camera and picture).
    if (restore_wanted || (pausers.empty() && view_captured)) {
        if (view_captured) {
            double started = now_seconds();
            if (!pc_hw_photo_restore || !pc_hw_photo_restore()) {
                last_error = "cannot restore the original presentation: renderer configuration changed";
                fprintf(stderr, "[photo] %s\n", last_error.c_str());
                if (pc_hw_resync)
                    pc_hw_resync();
            } else {
                // Compatibility: renders counts completed presentation updates,
                // including a restore (zero simulated game vsyncs).
                ++renders;
                render_vsyncs = 0;
                last_render_ms = (now_seconds() - started) * 1000;
            }
            rendered = false;
        }
        if (pc_hw_photo_drop)
            pc_hw_photo_drop();
        view_borrowed = view_captured = false;
        restore_wanted = false;
        redraw_wanted = false;
    }
    if (pausers.empty() && !redraw_wanted) {
        phase = RUNNING;
        redraw_wanted = false;
        audio_next = 0;
        audio_moved = false;
        if (pc_audio_output_duck)
            pc_audio_output_duck(1);
        if (rendered && pc_hw_resync)
            pc_hw_resync(); /* the hardware target is M's again (the renders' pictures were not) */
        rendered = false;
        view_borrowed = view_captured = false;
        if (pc_hw_photo_drop)
            pc_hw_photo_drop();
        if (pc_snapshot_drop_slot)
            pc_snapshot_drop_slot(SLOT);
        fprintf(stderr, "[photo] resumed at vsync %u\n", pc_vsync_count());
        return 0;
    }
    audio_pump();
    if (!redraw_wanted)
        return 0;
    redraw_wanted = false;
    if (!(&pc_hw_active && pc_hw_active)) {
        last_error = "a render needs the hardware renderer";
        redraw_failed();
        return 0;
    }
    if (!pc_snapshot_mark_slot || !pc_snapshot_rewind_slot) {
        last_error = "a render needs save states (x86)";
        redraw_failed();
        return 0;
    }
    if (audio_moved && pc_snapshot_drop_slot) /* the sound went on: the moment a render returns to is now */
        pc_snapshot_drop_slot(SLOT);
    audio_moved = false;
    if (!pc_snapshot_slot_valid(SLOT)) {
        char error[160] = "";
        if (pc_snapshot_mark_slot(SLOT, error, (int)sizeof error)) {
            last_error = std::string("cannot keep the moment: ") + error;
            fprintf(stderr, "[photo] %s\n", last_error.c_str());
            redraw_failed();
            return 0;
        }
    }
    if (view_borrowed) {
        if (!pc_hw_photo_mark || !pc_hw_photo_mark()) {
            last_error = "cannot keep the presented image";
            redraw_failed();
            return 0;
        }
        view_captured = true;
    }
    /* A render: this vsync and the next ones run from M, then back to it. The hardware target starts
     * from M's VRAM (the last render left its own). */
    if (pc_hw_resync)
        pc_hw_resync();
    phase = RENDERING;
    pc_photo_rendering = 1;
    rendered = true;
    render_vsyncs = 0;
    render_start_frames = pc_battle_frame_count ? pc_battle_frame_count() : 0;
    render_start = now_seconds();
    last_error.clear();
    return 1;
}

extern "C" int pc_photo_rpc(const char* params, char* out, int size)
{
    int n;
    if (photo::json_int(params, "render_frames", &n))
        render_frames = std::max(1, std::min(30, n));
    for (const photo::Part& p : photo::parts())
        if (p.params)
            p.params(params);
    float audio_gain_now;
    std::string owners;
    for (const std::string& o : pausers)
        owners += (owners.empty() ? "" : ",") + photo::json_quote(o);
    char head[512];
    snprintf(head, sizeof head,
        "{\"phase\":\"%s\",\"owners\":[%s],\"held_vsync\":%u,\"renders\":%u,\"last_render_ms\":%.2f,"
        "\"last_render_vsyncs\":%u,\"render_frames\":%d,\"audio_vsyncs\":%u,\"audio\":%d,\"redraw_wanted\":%s,\"mark\":%s,\"error\":%s",
        PHASES[phase], owners.c_str(), held_vsync, renders, last_render_ms, render_vsyncs, render_frames,
        audio_vsyncs, audio_mode(&audio_gain_now), redraw_wanted ? "true" : "false", pc_snapshot_slot_valid && pc_snapshot_slot_valid(SLOT) ? "true" : "false",
        photo::json_quote(last_error).c_str());
    std::string reply = head;
    for (const photo::Part& p : photo::parts())
        if (p.json)
            p.json(reply);
    reply += "}";
    if ((int)reply.size() >= size)
        return -1;
    memcpy(out, reply.c_str(), reply.size() + 1);
    return 0;
}

/* ------------------------------------------------------------ pause */

extern "C" int pc_photo_pause(const char* owner, char* why, int size)
{
    if (contains(pausers, owner))
        return 0;
    if (phase == RUNNING) {
        std::string reason = unsafe();
        if (!reason.empty()) {
            copy_out(why, size, "cannot pause now: " + reason);
            return -1;
        }
        phase = ASKED;
    }
    pausers.push_back(owner);
    return 0;
}
extern "C" void pc_photo_resume(const char* owner)
{
    pc_photo_pause_audio(owner, PC_PHOTO_AUDIO_STOP, 1);
    pausers.erase(std::remove(pausers.begin(), pausers.end(), owner), pausers.end());
    if (pausers.empty() && phase == ASKED) {
        phase = RUNNING;
        restore_wanted = redraw_wanted = false;
        view_borrowed = view_captured = false;
        if (pc_hw_photo_drop)
            pc_hw_photo_drop();
    }
}
extern "C" void pc_photo_pause_audio(const char* owner, int mode, float gain)
{
    audio_wishes.erase(std::remove_if(audio_wishes.begin(), audio_wishes.end(),
                           [&](const AudioWish& w) { return w.owner == owner; }),
        audio_wishes.end());
    if (mode != PC_PHOTO_AUDIO_STOP)
        audio_wishes.push_back({ owner, mode, std::max(0.0f, std::min(1.0f, gain)) });
}
extern "C" int pc_photo_paused(void) { return phase != RUNNING; }
extern "C" int pc_photo_paused_by(const char* owner) { return contains(pausers, owner); }
extern "C" void pc_photo_redraw(void)
{
    if (phase == ASKED || phase == HELD)
        redraw_wanted = true;
}

/* ------------------------------------------------------------ everything an owner holds */

extern "C" void pc_photo_release(const char* owner)
{
    std::string o = owner;
    for (const photo::Part& p : photo::parts())
        p.release(o);
    pc_photo_resume(owner);
}
extern "C" void pc_photo_release_all(void)
{
    std::vector<std::string> owners = pausers;
    for (const photo::Part& p : photo::parts())
        p.owners(owners);
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    for (const std::string& o : owners) {
        photo::notify(o, "everything", "released by the safety (Escape, Start) or an engine reload");
        pc_photo_release(o.c_str());
    }
}
extern "C" int pc_photo_engine_unload(char* why, int size)
{
    if (phase == RENDERING || landing) {
        copy_out(why, size, "retry: a photo mode render is running");
        return -1;
    }
    pc_photo_release_all();
    if (view_captured && pc_hw_photo_restore)
        pc_hw_photo_restore();
    if (pc_hw_photo_drop)
        pc_hw_photo_drop();
    rendered = false;
    restore_wanted = false;
    view_borrowed = view_captured = false;
    for (const photo::Part& p : photo::parts())
        p.reset();
    /* Held: the state is M's (renders rewind); resumed here, the next library runs on from it. */
    phase = RUNNING;
    pausers.clear();
    audio_wishes.clear();
    audio_next = 0;
    audio_moved = false;
    if (pc_audio_output_duck)
        pc_audio_output_duck(1);
    redraw_wanted = false;
    if (pc_snapshot_drop_slot)
        pc_snapshot_drop_slot(SLOT);
    pc_photo_rendering = 0; /* (crosses the reload: a host-owned block) */
    return 0;
}

/* Lua (lua_api_photo.cpp): what was taken back without the owner asking. */
extern "C" int pc_photo_notice_next(char* owner, int owner_size, char* what, int what_size, char* reason,
    int reason_size)
{
    if (notices.empty())
        return 0;
    copy_out(owner, owner_size, notices.front().owner);
    copy_out(what, what_size, notices.front().what);
    copy_out(reason, reason_size, notices.front().reason);
    notices.pop_front();
    return 1;
}
