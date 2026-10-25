// The cutscene player (cutscene.h; pc/docs/Cutscene_Player.md).
//
// A take: frame 0 is the vsync it started at (start_vsync); frames [first, head) are logged: for each,
// the pad buttons and camera stick the game read and every value the scanline estimate returned
// (boot_runtime.c func_800E45B4, the one host-clock input of game state). Frames below replay_end are
// replayed: the input hooks give the logged values back instead of the live ones, so a frame played
// again is the frame played before, byte for byte. Rewind marks (state_rewind.c's take slots) are
// taken at the vsync boundary every pc_take_interval frames into a ring bounded by count and memory.
// Going to frame t: rewind to the last mark at or before t, then step (unpaced) to t, paused there.
//
// Everything here runs on the game thread at the vsync boundary (dev_rpc.c's poll: pc_take_vsync once
// per vsync, pc_take_idle in the paused loop) or in the input hooks during a frame.
#include "cutscene_internal.h"
#include "cutscene_sequencer.h"
#include "light_prefabs.h"
#include "photo.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#endif

extern "C" {
unsigned pc_vsync_count(void);
void pc_rpc_set_paused(int value);
void pc_rpc_request_step(long vsyncs);
int pc_rpc_is_paused(void);
int pc_snapshot_mark_slot(int slot, char* error, int size) __attribute__((weak));
int pc_snapshot_rewind_slot(int slot, char* error, int size) __attribute__((weak));
void pc_snapshot_drop_slot(int slot) __attribute__((weak));
int pc_snapshot_slot_valid(int slot) __attribute__((weak));
size_t pc_snapshot_slot_bytes(int slot) __attribute__((weak));
int pc_snapshot_point(const char* path, char* error, int size) __attribute__((weak));
int pc_snapshot_load_now(const char* path, char* error, int size) __attribute__((weak));
int pc_snapshot_available(void) __attribute__((weak));
int pc_battle_event_threads(int state[2], int offset[4], int opcode[4], void* handler[4]) __attribute__((weak));
int pc_battle_event_id(void) __attribute__((weak));
int pc_memory_card_busy(void) __attribute__((weak));
extern int pc_game_phase __attribute__((weak));
extern int pc_title_input_ready __attribute__((weak));
int pc_room_warp_request(int zone, int room, int entry, char* why, int size) __attribute__((weak));
int pc_room_warp_phase(void) __attribute__((weak));
void pc_room_warp_acknowledge(void) __attribute__((weak));
void pc_room_warp_cancel(void) __attribute__((weak));
extern int pc_room_warp_enemies __attribute__((weak));
extern int pc_room_warp_scripts __attribute__((weak));
extern volatile int pc_ending_request __attribute__((weak));
int pc_photo_camera_begin(const char* owner, char* why, int size) __attribute__((weak));
int pc_story_request(const unsigned char start[12], const signed char* values, int count, int start_event,
    const int sfx[4]) __attribute__((weak));
int pc_story_phase(void) __attribute__((weak));
void pc_story_where(int* zone, int* room) __attribute__((weak));
void pc_story_cancel(void) __attribute__((weak));
void pc_story_done(void) __attribute__((weak));
int pc_room_door(int zone, int room, int entry, const int cell[4], unsigned char door[12]) __attribute__((weak));
extern int pc_title_cursor __attribute__((weak));
void pc_photo_camera_finish(const char* owner) __attribute__((weak));

int pc_take_interval = 30;
int pc_take_marks_max = 64;
int pc_take_budget_mb = 2048;
int pc_instant_replay = 0;
int pc_instant_replay_seconds = 30;
int pc_instant_replay_interval = 60;
int pc_cutscene_auto_advance = 1;
int pc_cutscene_dialogue_hold = 150;
int pc_battle_event_waiting(void) __attribute__((weak));
}

namespace cutscene {
bool armed = false;
bool hide_text = false;
int capture_count = 30;
bool window_open = false;
bool pad_focus = false;
unsigned pad_focus_vsync = 0;
} // namespace cutscene

namespace {
using cutscene::Scene;

enum { TAKE_FIRST = 2, TAKE_SLOTS = 512 }; // state_rewind.c's PC_REWIND_TAKE_FIRST / _SLOTS
const char* const CAPTURE_OWNER = "cutscene.capture";
const char* const CAMERA_OWNER = "cutscene.camera";

struct Frame {
    uint32_t buttons;
    float stick[2];
    int stick_valid;
    size_t scan_start; // index into the take's scanline values (absolute: scan_base + i)
};
struct Mark {
    int frame, slot;
    size_t bytes;
};
struct Take {
    bool on = false, rolling = false;
    bool story = false; // a story scene's (the viewer's): its dialogue may go on by itself
    std::string scene, name;
    unsigned start_vsync = 0;
    int first = 0, head = 0, replay_end = 0;
    std::vector<Frame> frames; // [first, head)
    std::vector<uint16_t> scan;
    size_t scan_base = 0;
    std::vector<Mark> marks; // by frame
    std::map<int, unsigned> inject; // a recipe's buttons on live frames
    long divergences = 0;
    int scan_frame = -1;
    size_t scan_k = 0;
    // what the event interpreter did in this take's live frames (the viewer's tests: it ran, not a bare warp)
    int event_frames = 0, dialogue_frames = 0;
    std::vector<int> event_ids;
    std::vector<long long> event_places; // (id, thread, offset), at most 4096
} take;
bool slot_used[TAKE_SLOTS];
bool playing = false;
int speed = 1;
double mark_ms = 0;
std::string message;

// A pending go-to.
int seek_target = -1;
bool landing = false; // a rewind of ours is landing (the next boundary is the mark's)
enum After { AFTER_NONE, AFTER_REPRO } after_seek = AFTER_NONE;
int repro_frame = -1;
std::string repro_path;

// Captures.
enum CapPhase { CAP_IDLE, CAP_WAIT, CAP_STEP } cap_phase = CAP_IDLE;
int capture_left = 0, capture_id = 0, capture_step_from = 0;
bool cap_held = false; /* this capture holds the photo mode's pause (the UI hidden) */
int cap_waits = 0;
std::string last_capture, capture_error;

// A scene being reached.
struct Recipe {
    bool on = false;
    Scene scene;
    int phase = 0, waited = 0;
    int saved_scripts = 0, saved_enemies = 0;
    unsigned press = 0; // buttons a recipe holds down before a take (the title menu)
    int press_frames = 0, press_wait = 0;
    bool fast = false; // unpaced (a story scene's script played on to its event)
} recipe;
int dialogue_wait = 0; // live frames the running event has waited for the player
std::string return_snapshot; // the game before this session's first story scene (Back to the game)
bool return_pending = false;

// A repro being loaded (pc_snapshot_load_now does not return; the landing finishes it).
struct Jump {
    bool pending = false, landing = false;
    std::string snapshot, scene, name;
    int base = 0, frame = 0;
    std::vector<Frame> frames;
    std::vector<uint16_t> scan;
} jump_state;

bool camera_on = false;
int last_event_state = 0;
std::vector<Scene> catalog;
bool catalog_loaded = false;
std::map<int, std::string> opcode_names;
bool opcodes_loaded = false;

double now_ms()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int current_frame() { return (int)(pc_vsync_count() - take.start_vsync); }
bool in_take(int f) { return take.on && f >= take.first && f < take.head; }
Frame& frame_at(int f) { return take.frames[(size_t)(f - take.first)]; }
size_t scan_end(int f)
{
    return f + 1 < take.head ? frame_at(f + 1).scan_start : take.scan_base + take.scan.size();
}

bool make_dirs(const std::string& path)
{
    std::string part;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!part.empty() && part != "." && part.back() != ':') {
#ifdef _WIN32
                if (mkdir(part.c_str()) && errno != EEXIST)
#else
                if (mkdir(part.c_str(), 0755) && errno != EEXIST)
#endif
                    return false;
            }
        }
        if (i < path.size())
            part += path[i];
    }
    return true;
}
bool exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}
std::string read_file(const std::string& path)
{
    std::string text;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    fclose(f);
    return text;
}
std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\', out += c;
        else if ((unsigned char)c < 0x20) {
            char u[8];
            snprintf(u, sizeof u, "\\u%04x", c);
            out += u;
        } else
            out += c;
    }
    return out + "\"";
}
std::string scene_dir() { return std::string(cutscene::dir()) + "/" + (take.scene.empty() ? "take" : take.scene); }

// ---- marks ----

void drop_mark(size_t i)
{
    if (pc_snapshot_drop_slot)
        pc_snapshot_drop_slot(TAKE_FIRST + take.marks[i].slot);
    slot_used[take.marks[i].slot] = false;
    take.marks.erase(take.marks.begin() + (long)i);
}
size_t marks_bytes()
{
    size_t total = 0;
    for (const Mark& m : take.marks)
        total += m.bytes;
    return total;
}
void drop_all_marks()
{
    while (!take.marks.empty())
        drop_mark(take.marks.size() - 1);
}
/* The mark that goes when the ring is over its count or memory: the rolling take's oldest; a scene's
 * keeps its start and its newest and thins the rest (the mark whose neighbours are closest), so the
 * marks stay spread over the whole take. */
size_t victim()
{
    if (take.rolling || take.marks.size() < 3)
        return 0;
    size_t v = 1;
    int best = 1 << 30;
    for (size_t i = 1; i + 1 < take.marks.size(); i++) {
        int gap = take.marks[i + 1].frame - take.marks[i - 1].frame;
        if (gap < best)
            best = gap, v = i;
    }
    return v;
}
size_t budget_bytes() { return (size_t)std::max(64, pc_take_budget_mb) << 20; }
int marks_max() { return std::max(2, std::min((int)TAKE_SLOTS, pc_take_marks_max)); }
void enforce_budget()
{
    while (take.marks.size() > 1 && ((int)take.marks.size() > marks_max() || marks_bytes() > budget_bytes()))
        drop_mark(victim());
}
const Mark* mark_at_or_before(int f)
{
    const Mark* best = nullptr;
    for (const Mark& m : take.marks)
        if (m.frame <= f)
            best = &m;
    return best;
}
bool has_mark(int f)
{
    for (const Mark& m : take.marks)
        if (m.frame == f)
            return true;
    return false;
}
void take_mark(int f)
{
    if (!pc_snapshot_mark_slot || has_mark(f))
        return;
    if (pc_memory_card_busy && pc_memory_card_busy())
        return; /* (a card file open: its host side is not rewound) */
    /* A slot: a mark that has to go gives its own (its buffers are reused: no fresh memory to fault in),
     * else a free one. */
    int slot = -1;
    size_t each = take.marks.empty() ? 0 : take.marks.back().bytes;
    int window = std::max(2, pc_instant_replay_seconds) * 60;
    if (!take.marks.empty()
        && ((take.rolling && take.marks.size() > 1 && take.marks[1].frame <= f - window)
            || (int)take.marks.size() >= marks_max() || marks_bytes() + each > budget_bytes())) {
        size_t v = victim();
        slot = take.marks[v].slot;
        take.marks.erase(take.marks.begin() + (long)v);
    }
    for (int i = 0; i < TAKE_SLOTS && slot < 0; i++)
        if (!slot_used[i])
            slot = i;
    if (slot < 0)
        return;
    slot_used[slot] = true;
    char error[160] = "";
    double t0 = now_ms();
    if (pc_snapshot_mark_slot(TAKE_FIRST + slot, error, (int)sizeof error)) {
        message = std::string("mark refused: ") + error;
        if (pc_snapshot_drop_slot)
            pc_snapshot_drop_slot(TAKE_FIRST + slot);
        slot_used[slot] = false;
        return;
    }
    mark_ms = now_ms() - t0;
    Mark m = { f, slot, pc_snapshot_slot_bytes ? pc_snapshot_slot_bytes(TAKE_FIRST + slot) : 0 };
    auto at = std::upper_bound(take.marks.begin(), take.marks.end(), f,
        [](int frame, const Mark& x) { return frame < x.frame; });
    take.marks.insert(at, m);
    enforce_budget();
}

// ---- the take ----

void end_take()
{
    // Scene switches and automatic take endings bypass stop(). Never carry the
    // borrowed camera (or its photo pause) into the next scene or back to play.
    cutscene::free_camera(false, nullptr);
    cutscene::sequencer_end();
    drop_all_marks();
    take = Take();
    seek_target = -1;
    landing = false;
    after_seek = AFTER_NONE;
    speed = 1;
    playing = false;
}
void begin_take(const std::string& scene, const std::string& name, bool rolling)
{
    end_take();
    take.on = true;
    take.rolling = rolling;
    take.scene = scene;
    take.name = name;
    cutscene::sequencer_begin(scene);
    take.start_vsync = pc_vsync_count();
    message = (rolling ? "instant replay: " : "take: ") + name;
}
/* At boundary f: the frame f about to run is logged from now on (live), a mark when one is due, the
 * rolling take's window kept. */
void on_frame(int f)
{
    while (take.head <= f) {
        Frame fr = { 0, { 0, 0 }, 0, take.scan_base + take.scan.size() };
        if (!take.frames.empty()) {
            const Frame& last = take.frames.back();
            fr.buttons = last.buttons;
            fr.stick[0] = last.stick[0], fr.stick[1] = last.stick[1], fr.stick_valid = last.stick_valid;
        }
        take.frames.push_back(fr);
        ++take.head;
    }
    int interval = std::max(1, take.rolling ? pc_instant_replay_interval : pc_take_interval);
    if (f == take.first || f % interval == 0)
        take_mark(f);
    if (take.rolling && f >= take.head - 1) {
        int window = std::max(2, pc_instant_replay_seconds) * 60;
        while (take.marks.size() > 1 && take.marks[1].frame <= f - window)
            drop_mark(0);
        /* The log before the oldest mark can never replay again: let it go in chunks. */
        int keep = take.marks.empty() ? f : take.marks[0].frame;
        if (keep - take.first > 1200) {
            size_t n = (size_t)(keep - take.first);
            size_t scan_n = take.frames[n].scan_start - take.scan_base;
            take.frames.erase(take.frames.begin(), take.frames.begin() + (long)n);
            take.scan.erase(take.scan.begin(), take.scan.begin() + (long)scan_n);
            take.scan_base += scan_n;
            take.first = keep;
        }
    }
}
/* Frames from f on dropped (the boundary f is kept): what plays from here is live and logged anew. */
void truncate_at(int f)
{
    cutscene::sequencer_truncate(std::max(f, take.first));
    if (f >= take.head)
        return;
    f = std::max(f, take.first);
    size_t scan_n = frame_at(f).scan_start - take.scan_base;
    take.frames.resize((size_t)(f - take.first));
    take.scan.resize(scan_n);
    take.head = f;
    take.replay_end = std::min(take.replay_end, f);
    for (size_t i = take.marks.size(); i-- > 0;)
        if (take.marks[i].frame > f)
            drop_mark(i);
}

// ---- going to a frame ----

bool start_seek(int target, std::string* why)
{
    int f = current_frame();
    if (target < take.first) {
        *why = "frame " + std::to_string(target) + " is before the take's first logged frame "
            + std::to_string(take.first);
        return false;
    }
    seek_target = target;
    pc_rpc_set_paused(1);
    playing = false;
    // Rewind marks contain game state, not the renderer's in-flight native
    // lists/targets. Replay a short lead-in instead of landing directly on a
    // mark with an empty or stale picture. The requested game frame is unchanged.
    int warm_from = std::max(take.first, target - 16);
    if (target >= f) {
        const Mark* m = mark_at_or_before(warm_from);
        if (!m || m->frame <= f || target == f) {
            if (target > f)
                pc_rpc_request_step(target - f);
            return true;
        }
    }
    const Mark* m = mark_at_or_before(warm_from);
    if (!m) {
        seek_target = -1;
        *why = "no mark at or before frame " + std::to_string(target);
        return false;
    }
    /* The frame f about to run is not logged yet when it is live: it is not part of the take. */
    if (f >= take.replay_end && take.head == f + 1)
        truncate_at(f);
    take.replay_end = take.head;
    landing = true;
    char error[160] = "";
    pc_room_look_clear(); // Never carry a transient look through a restored room's baseline.
    pc_snapshot_rewind_slot(TAKE_FIRST + m->slot, error, (int)sizeof error); /* does not return when it rewinds */
    landing = false;
    seek_target = -1;
    *why = std::string("rewind failed: ") + error;
    return false;
}
void write_repro_here();
void seek_service(int f)
{
    if (seek_target < 0)
        return;
    if (f == seek_target) {
        seek_target = -1;
        pc_rpc_set_paused(1);
        playing = false;
        if (after_seek == AFTER_REPRO) {
            after_seek = AFTER_NONE;
            write_repro_here();
        }
        return;
    }
    std::string why;
    if (f < seek_target) {
        if (pc_rpc_is_paused())
            pc_rpc_request_step(seek_target - f);
    } else if (!start_seek(seek_target, &why)) {
        message = why;
        seek_target = -1;
    }
}

// ---- captures ----

std::string capture_name(int f, int n)
{
    char base[64];
    snprintf(base, sizeof base, "/f%06d", f);
    std::string path = scene_dir() + base + (n > 1 ? "-" + std::to_string(n) : std::string()) + ".png";
    return path;
}
void capture_service(int f)
{
    if (capture_left <= 0 || seek_target >= 0)
        return;
    if (cap_phase == CAP_STEP) {
        if (f == capture_step_from + 1 && pc_rpc_is_paused())
            cap_phase = CAP_IDLE;
        else
            return;
    }
    if (cap_phase == CAP_IDLE) {
        char why[200] = "", out[512] = "";
        /* The photo mode's hold redraws the frozen moment with the HUD (and the dialogue) hidden. It
         * refuses some moments (a room transition, loading, a card write): then the picture as it is
         * shown (the overlay is never in it). */
        cap_held = !pc_photo_pause(CAPTURE_OWNER, why, (int)sizeof why);
        capture_error = cap_held ? "" : std::string("as shown (") + why + ")";
        if (cap_held)
            pc_photo_ui_hide(CAPTURE_OWNER,
                PC_PHOTO_HIDE_HUD | PC_PHOTO_HIDE_OVERLAY | (cutscene::hide_text ? PC_PHOTO_HIDE_GAME2D : 0));
        if (!make_dirs(scene_dir())) {
            capture_error = "cannot make " + scene_dir();
            capture_left = 0;
            pc_photo_release(CAPTURE_OWNER);
            return;
        }
        std::string path;
        for (int n = 1; n < 1000; n++)
            if (!exists(path = capture_name(f, n)))
                break;
        capture_id = pc_photo_capture(CAPTURE_OWNER, path.c_str(), 1, out, (int)sizeof out, why, (int)sizeof why);
        if (!capture_id) {
            capture_error = why;
            capture_left = 0;
            pc_photo_release(CAPTURE_OWNER);
            return;
        }
        cap_phase = CAP_WAIT;
        cap_waits = 0;
        return;
    }
    if (cap_phase == CAP_WAIT) {
        pc_photo_capture_result r;
        if (!pc_photo_capture_result_get(capture_id, &r) || !r.done) {
            if (!cap_held && ++cap_waits > 600) { /* (no present takes it: given up, not hung) */
                pc_photo_release(CAPTURE_OWNER);
                capture_error = "no picture came (" + capture_error + ")";
                capture_left = 0;
                cap_phase = CAP_IDLE;
            }
            return;
        }
        if (cap_held) {
            pc_photo_ui_hide(CAPTURE_OWNER, 0);
            pc_photo_resume(CAPTURE_OWNER);
        }
        if (r.ok)
            last_capture = r.path;
        else
            capture_error = r.error, capture_left = 1;
        if (--capture_left > 0) {
            cap_phase = CAP_STEP;
            capture_step_from = f;
            pc_rpc_request_step(1);
        } else
            cap_phase = CAP_IDLE;
    }
}

// ---- repro files ----

bool write_log(const std::string& path, int from)
{
    FILE* out = fopen(path.c_str(), "w");
    if (!out)
        return false;
    fprintf(out, "# rood-boi cutscene take log (pc/src/cutscene.cpp): F frame buttons stick_valid stick_x stick_y n "
                 "scanlines...\n");
    for (int f = from; f < take.head; f++) {
        const Frame& fr = frame_at(f);
        size_t a = fr.scan_start - take.scan_base, b = scan_end(f) - take.scan_base;
        fprintf(out, "F %d %x %d %.9g %.9g %zu", f, fr.buttons, fr.stick_valid, fr.stick[0], fr.stick[1], b - a);
        for (size_t i = a; i < b; i++)
            fprintf(out, " %u", take.scan[i]);
        fputc('\n', out);
    }
    bool ok = !ferror(out);
    return fclose(out) == 0 && ok;
}
bool read_log(const std::string& path, int base, std::vector<Frame>& frames, std::vector<uint16_t>& scan)
{
    FILE* in = fopen(path.c_str(), "r");
    if (!in)
        return false;
    frames.clear();
    scan.clear();
    char tag[4];
    bool ok = true;
    while (ok && fscanf(in, " %3s", tag) == 1) {
        if (tag[0] == '#') {
            int c;
            while ((c = fgetc(in)) != EOF && c != '\n') {
            }
            continue;
        }
        int f, valid;
        float sx, sy;
        unsigned buttons;
        size_t n;
        if (strcmp(tag, "F") || fscanf(in, "%d %x %d %f %f %zu", &f, &buttons, &valid, &sx, &sy, &n) != 6
            || f != base + (int)frames.size() || n > 100000) {
            ok = false;
            break;
        }
        Frame fr = { buttons, { sx, sy }, valid, scan.size() };
        frames.push_back(fr);
        for (size_t i = 0; i < n; i++) {
            unsigned v;
            if (fscanf(in, "%u", &v) != 1) {
                ok = false;
                break;
            }
            scan.push_back((uint16_t)v);
        }
    }
    fclose(in);
    return ok && !frames.empty();
}
void write_repro_here()
{
    int base = current_frame();
    std::string dir = scene_dir(), stem;
    char name[64];
    snprintf(name, sizeof name, "/repro-f%06d", repro_frame);
    stem = dir + name;
    char error[200] = "";
    if (!make_dirs(dir)) {
        message = "cannot make " + dir;
    } else if (!pc_snapshot_point || pc_snapshot_point((stem + ".snap").c_str(), error, (int)sizeof error)) {
        message = std::string("repro: cannot save the state: ") + (pc_snapshot_point ? error : "no save states");
    } else if (!write_log(stem + ".log", base)) {
        message = "repro: cannot write " + stem + ".log";
    } else {
        const char* settings = getenv("ROOD_SETTINGS");
        std::string json = "{\n  \"scene\": " + quote(take.scene) + ",\n  \"name\": " + quote(take.name)
            + ",\n  \"frame\": " + std::to_string(repro_frame) + ",\n  \"base_frame\": " + std::to_string(base)
            + ",\n  \"snapshot\": " + quote(stem + ".snap") + ",\n  \"log\": " + quote(stem + ".log")
            + ",\n  \"settings\": " + quote(settings ? settings : "") + ",\n  \"settings_text\": "
            + quote(settings ? read_file(settings) : "") + ",\n  \"jump\": "
            + quote("RPC cutscene {\"action\": \"jump\", \"repro\": \"" + stem + ".json\"}") + "\n}\n";
        FILE* out = fopen((stem + ".json").c_str(), "w");
        if (out && fputs(json.c_str(), out) >= 0 && fclose(out) == 0) {
            repro_path = stem + ".json";
            message = "repro written: " + repro_path;
        } else {
            if (out)
                fclose(out);
            message = "repro: cannot write " + stem + ".json";
        }
    }
    std::string why;
    if (repro_frame != base && !start_seek(repro_frame, &why))
        message = why;
    repro_frame = -1;
}

// ---- recipes ----

void finish_recipe(const char* why)
{
    if (recipe.scene.kind == "story" && pc_story_cancel) {
        pc_story_cancel();
        if (pc_story_done && pc_story_phase && pc_story_phase() >= 3)
            pc_story_done();
    }
    recipe.press_frames = 0;
    if (recipe.scene.kind == "warp" && &pc_room_warp_scripts) {
        pc_room_warp_scripts = recipe.saved_scripts;
        pc_room_warp_enemies = recipe.saved_enemies;
    }
    if (why)
        message = why;
    recipe.on = false;
}
/* A story scene (story_boot.c): its flags and room asked for, then the game starts there as from a loaded
 * save (from the title: New Game). The take starts once the room is in (its entry script plays the scene)
 * or the scene's event was started there. */
/* While a story scene is being reached: what the running event waits for is pressed at once. */
void answer(bool vsync)
{
    if (vsync && recipe.press_frames == 0 && recipe.press_wait-- <= 0 && pc_battle_event_waiting
        && pc_battle_event_waiting()) {
        int mask = pc_battle_event_waiting();
        recipe.press = (unsigned)(mask & 0x20 ? 0x20 : mask & -mask);
        recipe.press_frames = 4;
        recipe.press_wait = 4; /* (released between presses) */
    }
}
void story_service(bool vsync)
{
    const Scene& s = recipe.scene;
    if (vsync && recipe.press_frames > 0)
        recipe.press_frames--;
    if (recipe.phase == 0) {
        unsigned char door[12];
        if (!pc_room_door(s.zone, s.room, s.entry, s.cell, door)) {
            finish_recipe(("no zone " + std::to_string(s.zone) + " room " + std::to_string(s.room)).c_str());
            return;
        }
        std::vector<signed char> flags(1088, 0);
        for (const auto& f : s.flags)
            if (f.first >= 0 && f.first < (int)flags.size())
                flags[(size_t)f.first] = (signed char)f.second;
        bool title = &pc_game_phase && pc_game_phase == 1;
        if (!title && return_snapshot.empty() && pc_snapshot_point && !take.on) {
            /* the game as it is: Back to the game returns here */
            std::string path = std::string(cutscene::dir()) + "/return-to-game.snap";
            char error[200] = "";
            make_dirs(cutscene::dir());
            if (!pc_snapshot_point(path.c_str(), error, (int)sizeof error))
                return_snapshot = path;
        }
        end_take();
        if (!pc_story_request(door, flags.data(), (int)flags.size(), s.start_event, s.sound)) {
            finish_recipe("a story scene is starting already");
            return;
        }
        recipe.phase = title ? 1 : 3;
        recipe.waited = 0;
        pc_rpc_set_paused(0);
        return;
    }
    if (recipe.phase == 1 || recipe.phase == 2) {
        /* the title menu: the cursor to New Game (Up), then Start */
        if (!(&pc_title_input_ready && pc_title_input_ready)) {
            if (recipe.waited > 1200)
                finish_recipe("the title menu did not take input");
            return;
        }
        if (recipe.press_frames > 0 || recipe.press_wait-- > 0)
            return;
        if (&pc_title_cursor && pc_title_cursor != 0 && recipe.phase == 1) {
            recipe.press = 1u << 12; /* Up (boot_controller.h order: ... start up right down left) */
            recipe.press_frames = 2;
            recipe.press_wait = 20;
            if (recipe.waited > 600)
                recipe.phase = 2;
            return;
        }
        recipe.press = 1u << 11; /* Start */
        recipe.press_frames = 4;
        recipe.phase = 3;
        recipe.waited = 0;
        return;
    }
    int phase = pc_story_phase();
    if (recipe.phase == 3 && phase == 3 && s.start_event >= 0) {
        /* a trigger's scene: the room's entry script plays first (unpaced, dialogue answered); the room may
         * start the trigger's event itself on the way (its conditions met as in the story) */
        int id = pc_battle_event_id ? pc_battle_event_id() : -1;
        if (id == s.start_event && cutscene::event_info().state == 4) {
            pc_story_done();
            recipe.phase = 4;
            recipe.waited = 0;
            phase = 4;
        } else {
            recipe.fast = true;
            answer(vsync);
        }
    }
    if (recipe.phase == 3 && phase >= (s.start_event >= 0 ? 4 : 3)) {
        pc_story_done();
        recipe.phase = 4;
        recipe.waited = 0;
    }
    if (recipe.phase == 4 && s.until >= 0) {
        /* an EVT file a script chains to: its script plays on (unpaced, dialogue answered) until it runs */
        int id = pc_battle_event_id ? pc_battle_event_id() : -1;
        cutscene::EventInfo e = cutscene::event_info();
        int zone = -1, room = -1;
        if (pc_story_where)
            pc_story_where(&zone, &room);
        /* An EVT file is known by its number; a room's script by the room it runs in (its id keeps the
         * number of the last EVT file run: (1 << 12) | _evtFile). */
        bool reached = (s.until >> 12) == 1
            ? (id >> 12) == 1 && zone == s.until_room[0] && room == s.until_room[1]
            : id == s.until && (s.until >> 12 == 0 || s.until_room[0] < 0
                                   || (zone == s.until_room[0] && room == s.until_room[1]));
        if (!(reached && e.state == 4)) {
            recipe.fast = true;
            answer(vsync);
            if (recipe.waited > 30000) {
                recipe.fast = false;
                finish_recipe("the scene's script did not reach its event");
            }
            return;
        }
        recipe.fast = false;
        recipe.press_frames = 0;
    }
    if (recipe.phase == 4) {
        recipe.fast = false;
        finish_recipe(nullptr);
        begin_take(s.id, s.name, false);
        take.story = true;
        playing = true;
        pc_rpc_set_paused(0);
        on_frame(current_frame());
        message = "playing " + s.name;
        return;
    }
    if (recipe.waited > 3600)
        finish_recipe(phase == 1 ? "the game did not take the scene (in a menu?)" : "the scene did not start in time");
}

void recipe_service(bool vsync)
{
    if (!recipe.on)
        return;
    const Scene& s = recipe.scene;
    if (vsync)
        recipe.waited++;
    if (s.kind == "new_game") {
        /* At the title menu: frame 0 is the Start press (held 4 frames, as the tests' checkpoint does). */
        begin_take(s.id, s.name, false);
        for (int i = 0; i < 4; i++)
            take.inject[i] = 1u << 11; /* Start (boot_controller.h order: l2 r2 l1 r1 tri cir cro sqr sel l3 r3 start) */
        finish_recipe(nullptr);
        playing = true;
        pc_rpc_set_paused(0);
        on_frame(current_frame());
        return;
    }
    if (s.kind == "warp") {
        if (recipe.phase == 0) {
            char why[200] = "";
            recipe.saved_scripts = pc_room_warp_scripts;
            recipe.saved_enemies = pc_room_warp_enemies;
            pc_room_warp_scripts = 1;
            pc_room_warp_enemies = 1;
            if (!pc_room_warp_request(s.zone, s.room, s.entry, why, (int)sizeof why)) {
                finish_recipe((std::string("warp refused: ") + why).c_str());
                return;
            }
            recipe.phase = 1;
            pc_rpc_set_paused(0);
            return;
        }
        /* The take starts at the arrival (3: the room loaded, fading in; its entry event may already run and
         * hold the warp there until it ends), or at its end when that was too quick to see. */
        int phase = pc_room_warp_phase();
        if (phase >= 2)
            recipe.phase = 2;
        if (phase >= 3 || (recipe.phase == 2 && phase == 0)) {
            finish_recipe(nullptr);
            begin_take(s.id, s.name, false);
            playing = true;
            on_frame(current_frame());
        } else if (phase == 0 && recipe.phase == 1 && recipe.waited > 30) {
            finish_recipe("the warp did not happen (a door or an event in the way?)");
        } else if (recipe.waited > 3600) {
            pc_room_warp_cancel();
            finish_recipe("the warp took too long");
        }
        return;
    }
    if (s.kind == "story") {
        story_service(vsync);
        return;
    }
    if (s.kind == "ending") {
        if (recipe.phase == 0) {
            pc_ending_request = 1;
            recipe.phase = 1;
            pc_rpc_set_paused(0);
        } else if (!pc_ending_request) {
            finish_recipe(nullptr);
            begin_take(s.id, s.name, false);
            playing = true;
            on_frame(current_frame());
        } else if (recipe.waited > 600) {
            pc_ending_request = 0;
            finish_recipe("the ending did not start (only from the field, with no event running)");
        }
        return;
    }
    finish_recipe(("unknown recipe " + s.kind).c_str());
}

/* The event's work in a take (live frames): frames it ran, the events seen, the script places it was at. */
void note_event()
{
    cutscene::EventInfo e = cutscene::event_info();
    if (!e.running)
        return;
    take.event_frames++;
    if (pc_battle_event_waiting && (pc_battle_event_waiting() & 0x20))
        take.dialogue_frames++;
    if (std::find(take.event_ids.begin(), take.event_ids.end(), e.id) == take.event_ids.end())
        take.event_ids.push_back(e.id);
    for (int i = 0; i < 4 && take.event_places.size() < 4096; i++) {
        if (e.threads[i].offset < 0)
            continue;
        long long key = (long long)e.id << 32 | (long long)i << 24 | e.threads[i].offset;
        if (std::find(take.event_places.begin(), take.event_places.end(), key) == take.event_places.end())
            take.event_places.push_back(key);
    }
}

/* A story scene's dialogue goes on by itself (pc_cutscene_auto_advance; the player's other takes are left to
 * the player's input, as before): when the event has
 * waited pc_cutscene_dialogue_hold vsyncs for the player (a box shown, a button awaited), the button it
 * waits for is pressed on the next live frame. The press is a logged input like any other: replays and
 * seeks give it back exactly. */
void auto_advance(int f)
{
    if (!pc_cutscene_auto_advance || !pc_battle_event_waiting || !take.story || f < take.replay_end
        || f < take.head - 1) {
        dialogue_wait = 0;
        return;
    }
    int mask = pc_battle_event_waiting();
    if (!mask) {
        dialogue_wait = 0;
        return;
    }
    if (++dialogue_wait < std::max(1, pc_cutscene_dialogue_hold))
        return;
    dialogue_wait = 0;
    unsigned press = (unsigned)(mask & 0x20 ? 0x20 : mask & -mask); /* Circle, or the lowest awaited */
    for (int i = 1; i <= 4; i++) /* (held 4 vsyncs: a game frame may take 2 to 4) */
        take.inject[f + i] |= press;
}

// ---- the boundary ----

void service(bool vsync)
{
    unsigned v = pc_vsync_count();
    if (jump_state.landing) {
        jump_state.landing = false;
        end_take();
        take.on = true;
        take.scene = jump_state.scene;
        take.name = jump_state.name;
        cutscene::sequencer_begin(take.scene);
        take.start_vsync = v - (unsigned)jump_state.base;
        take.first = jump_state.base;
        take.frames.swap(jump_state.frames);
        take.scan.swap(jump_state.scan);
        take.head = take.first + (int)take.frames.size();
        take.replay_end = take.head;
        pc_rpc_set_paused(1);
        on_frame(take.first);
        seek_target = jump_state.frame;
        message = "repro loaded: frame " + std::to_string(jump_state.frame);
    }
    if (return_pending) {
        return_pending = false;
        char error[200] = "";
        std::string path = return_snapshot;
        return_snapshot.clear(); /* (the game is that one again) */
        pc_snapshot_load_now(path.c_str(), error, (int)sizeof error); /* does not return */
        return_snapshot = path;
        message = std::string("cannot go back to the game: ") + error;
        return;
    }
    if (jump_state.pending) {
        jump_state.pending = false;
        char error[200] = "";
        end_take();
        jump_state.landing = true;
        pc_snapshot_load_now(jump_state.snapshot.c_str(), error, (int)sizeof error); /* does not return */
        jump_state.landing = false;
        message = std::string("repro: cannot load the state: ") + error;
        return;
    }
    recipe_service(vsync);
    if (vsync && pc_battle_event_id) {
        int id = pc_battle_event_id();
        int event = id >= 0;
        if (cutscene::armed && event && !last_event_state && !recipe.on && (!take.on || take.rolling)) {
            char name[32];
            snprintf(name, sizeof name, "event-%04x", id);
            begin_take(name, name, false);
            playing = !pc_rpc_is_paused();
        }
        last_event_state = event;
    }
    if (!take.on) {
        if (vsync && pc_instant_replay && !recipe.on && !(pc_memory_card_busy && pc_memory_card_busy()))
            begin_take("replay", "instant replay", true);
        else
            return;
    }
    int f = (int)(v - take.start_vsync);
    if (vsync) {
        auto_advance(f);
        if (f >= take.head - 1 && f >= take.replay_end) {
            cutscene::sequencer_sample(f, cutscene::event_info());
            if (!take.rolling) note_event();
        }
    }
    if (landing) {
        landing = false; /* at the mark (state_rewind.c restored it; dev_rpc.c paused) */
    } else if (f < take.first || f > take.head) {
        /* Another state replaced this one (a save state loaded, the RPC's own rewind). */
        message = "the game's state was replaced: the take ended";
        end_take();
        return;
    }
    if (vsync) {
        take.scan_frame = -1;
        on_frame(f);
    }
    cutscene::sequencer_prune(take.first);
    cutscene::sequencer_evaluate(f);
    seek_service(f);
    capture_service(f);
}

const std::string& opcode_name(int op)
{
    static const std::string none;
    if (!opcodes_loaded) {
        opcodes_loaded = true;
        prefab::Json j;
        if (prefab::json_parse(read_file("pc/assets/cutscenes/opcodes.json").c_str(), j))
            if (const prefab::Json* ops = j.get("opcodes"))
                for (const auto& kv : ops->members)
                    opcode_names[(int)strtol(kv.first.c_str(), nullptr, 16)] = kv.second.string;
    }
    auto it = opcode_names.find(op);
    return it == opcode_names.end() ? none : it->second;
}
} // namespace

// ------------------------------------------------------------ the hooks (cutscene.h)

extern "C" void pc_take_vsync(void) { service(true); }
extern "C" void pc_take_idle(void) { service(false); }
extern "C" int pc_take_active(void) { return take.on; }

/* While a photo mode render runs the game from a held moment, live frames are not logged (the render
 * is rewound; the frame runs again for real) and replayed frames give the log back as always. */
extern "C" int pc_take_buttons(unsigned vsync, unsigned* pad)
{
    int f = (int)(vsync - take.start_vsync);
    if (recipe.on && recipe.press_frames > 0)
        *pad |= recipe.press;
    if (!in_take(f))
        return 0;
    Frame& fr = frame_at(f);
    if (f < take.replay_end) {
        *pad = fr.buttons;
        return 1;
    }
    if (cutscene::pad_focus && pc_vsync_count() - cutscene::pad_focus_vsync < 8 && !take.rolling)
        *pad = 0; /* the pad drives the viewer while its window has focus and shows (cutscene_panel.cpp) */
    auto it = take.inject.find(f);
    if (it != take.inject.end())
        *pad |= it->second;
    if (!pc_photo_rendering)
        fr.buttons = *pad;
    return 1;
}
extern "C" int pc_take_stick(unsigned vsync, float stick[2], int* valid)
{
    int f = (int)(vsync - take.start_vsync);
    if (!in_take(f))
        return 0;
    Frame& fr = frame_at(f);
    if (f < take.replay_end) {
        stick[0] = fr.stick[0], stick[1] = fr.stick[1];
        *valid = fr.stick_valid;
        return 1;
    }
    if (!pc_photo_rendering)
        fr.stick[0] = stick[0], fr.stick[1] = stick[1], fr.stick_valid = *valid;
    return 1;
}
extern "C" int pc_take_scanlines(int value)
{
    int f = current_frame();
    if (!in_take(f))
        return value;
    if (take.scan_frame != f)
        take.scan_frame = f, take.scan_k = 0;
    if (f < take.replay_end) {
        size_t at = frame_at(f).scan_start + take.scan_k++;
        if (at < scan_end(f))
            return take.scan[at - take.scan_base];
        ++take.divergences; /* the frame reads more than it did: a host input this log misses */
        return value;
    }
    if (pc_photo_rendering || f != take.head - 1)
        return value;
    take.scan.push_back((uint16_t)value);
    return value;
}
extern "C" int pc_take_pace(void)
{
    if (jump_state.pending || jump_state.landing || (take.on && seek_target >= 0) || (recipe.on && recipe.fast))
        return 0;
    return take.on && playing ? speed : 1;
}
extern "C" void pc_take_hotkey(void)
{
    cutscene::window_open = true;
    pc_rpc_set_paused(1);
    playing = false;
    if (take.on)
        message = "frozen at frame " + std::to_string(current_frame()) + ": go back to the moment, step, capture";
    else
        message = pc_instant_replay ? "no take yet" : "instant replay is off (tick it to keep the last seconds)";
}

// ------------------------------------------------------------ the API (cutscene_internal.h)

namespace cutscene {

const char* dir()
{
    const char* d = getenv("ROOD_CUTSCENES");
    return d && *d ? d : "pc/user/cutscenes";
}

/* pc/assets/cutscenes/story.json (pc/tools/story_scan.py): the story's scenes, in story order. */
void load_story()
{
    const char* path = getenv("ROOD_STORY_CATALOG");
    std::string text = read_file(path && *path ? path : "pc/assets/cutscenes/story.json");
    if (text.empty())
        return;
    prefab::Json j;
    std::string error;
    if (!prefab::json_parse(text.c_str(), j, &error)) {
        message = "story: " + error;
        return;
    }
    const prefab::Json* list = j.get("scenes");
    if (!list)
        return;
    auto str = [](const prefab::Json& o, const char* k) {
        const prefab::Json* v = o.get(k);
        return v && v->type == prefab::Json::String ? v->string : std::string();
    };
    auto num = [](const prefab::Json& o, const char* k, int def) {
        const prefab::Json* v = o.get(k);
        return v && v->type == prefab::Json::Number ? (int)v->number : def;
    };
    auto flag = [](const prefab::Json& o, const char* k) {
        const prefab::Json* v = o.get(k);
        return v && v->type == prefab::Json::Bool && v->boolean;
    };
    for (const prefab::Json& e : list->items) {
        Scene s;
        s.kind = "story";
        s.id = str(e, "id");
        s.name = str(e, "name");
        if (s.name.empty())
            s.name = s.id;
        s.chapter = str(e, "chapter");
        s.notes = str(e, "notes");
        s.zone = num(e, "zone", -1);
        s.room = num(e, "room", -1);
        s.entry = num(e, "entry", 0);
        s.step = num(e, "step", -1);
        s.start_event = num(e, "force", -1);
        s.until = num(e, "until", -1);
        if (const prefab::Json* ur = e.get("until_room"))
            for (size_t i = 0; i < 2 && i < ur->items.size(); i++)
                s.until_room[i] = (int)ur->items[i].number;
        if (const prefab::Json* snd = e.get("sound"))
            for (size_t i = 0; i < 4 && i < snd->items.size(); i++)
                s.sound[i] = (int)snd->items[i].number;
        s.event = num(e, "event", -1);
        s.frames = num(e, "frames", 0);
        s.texts = num(e, "texts", 0);
        s.verified = flag(e, "verified");
        s.reliable = s.verified;
        s.major = flag(e, "major");
        if (const prefab::Json* c = e.get("cell"))
            for (size_t i = 0; i < 4 && i < c->items.size(); i++)
                s.cell[i] = (int)c->items[i].number;
        if (const prefab::Json* f = e.get("flags"))
            for (const auto& kv : f->members)
                s.flags.emplace_back(atoi(kv.first.c_str()), (int)kv.second.number);
        s.from = "story step " + std::to_string(s.step) + ", zone " + std::to_string(s.zone) + " room "
            + std::to_string(s.room)
            + (s.until >= 0 ? ", played on into event " + std::to_string(s.until)
                    : s.start_event >= 0 ? ", its trigger's event" : ", its entry script");
        if (!s.id.empty() && s.zone >= 0 && !flag(e, "skip"))
            catalog.push_back(s);
    }
}

void reload_catalog()
{
    catalog.clear();
    catalog_loaded = true;
    const char* path = getenv("ROOD_CUTSCENE_CATALOG");
    std::string text = read_file(path && *path ? path : "pc/assets/cutscenes/catalog.json");
    prefab::Json j;
    std::string error;
    if (!prefab::json_parse(text.c_str(), j, &error)) {
        message = "catalog: " + error;
        return;
    }
    const prefab::Json* list = j.get("scenes");
    if (!list)
        return;
    for (const prefab::Json& e : list->items) {
        Scene s;
        auto str = [&](const prefab::Json& o, const char* k) {
            const prefab::Json* v = o.get(k);
            return v && v->type == prefab::Json::String ? v->string : std::string();
        };
        auto num = [&](const prefab::Json& o, const char* k, int def) {
            const prefab::Json* v = o.get(k);
            return v && v->type == prefab::Json::Number ? (int)v->number : def;
        };
        s.id = str(e, "id");
        s.name = str(e, "name");
        s.from = str(e, "from");
        s.notes = str(e, "notes");
        s.frames = num(e, "frames", 0);
        s.event = num(e, "event", -1);
        const prefab::Json* r = e.get("reliable");
        s.reliable = r && r->type == prefab::Json::Bool && r->boolean;
        if (const prefab::Json* rc = e.get("recipe")) {
            s.kind = str(*rc, "kind");
            s.zone = num(*rc, "zone", -1);
            s.room = num(*rc, "room", -1);
            s.entry = num(*rc, "entry", 0);
        }
        if (!s.id.empty())
            catalog.push_back(s);
    }
    load_story();
}
const std::vector<Scene>& scenes()
{
    if (!catalog_loaded)
        reload_catalog();
    return catalog;
}

EventInfo event_info()
{
    EventInfo e;
    int state[2], offset[4], opcode[4];
    void* handler[4];
    if (!pc_battle_event_threads)
        return e;
    e.running = pc_battle_event_threads(state, offset, opcode, handler);
    e.state = state[0];
    e.skip = state[1];
    e.id = pc_battle_event_id ? pc_battle_event_id() : -1;
    for (int i = 0; i < 4; i++) {
        e.threads[i].offset = offset[i];
        e.threads[i].opcode = opcode[i];
        if (opcode[i] >= 0)
            e.threads[i].name = opcode_name(opcode[i]);
    }
    return e;
}

Status status()
{
    Status s;
    s.on = take.on;
    s.rolling = take.rolling;
    s.playing = playing;
    s.paused = pc_rpc_is_paused();
    s.scene = take.scene;
    s.name = take.name;
    s.frame = take.on ? current_frame() : 0;
    s.first = take.first;
    s.head = take.head;
    s.replay_end = take.replay_end;
    s.replaying = take.on && s.frame < take.replay_end;
    s.seek_target = seek_target;
    s.speed = speed;
    for (const Mark& m : take.marks)
        s.marks.push_back(m.frame);
    s.mark_bytes = marks_bytes();
    s.mark_ms = mark_ms;
    s.divergences = take.divergences;
    s.start_vsync = take.start_vsync;
    if (recipe.on) {
        if (recipe.scene.kind == "story") {
            int phase = pc_story_phase ? pc_story_phase() : 0;
            if (recipe.phase <= 2)
                s.recipe = "Starting the game";
            else if (recipe.phase == 3 && phase < 3)
                s.recipe = "Loading room " + std::to_string(recipe.scene.zone) + "/" + std::to_string(recipe.scene.room);
            else
                s.recipe = "Reaching the scene's event";
        } else {
            s.recipe = "Reaching " + recipe.scene.kind;
        }
        s.recipe += " - " + std::to_string(recipe.waited) + " frames elapsed";
    }
    s.capturing = capture_left > 0;
    s.capture_left = capture_left;
    s.last_capture = last_capture;
    s.capture_error = capture_error;
    s.repro_path = repro_path;
    s.message = message;
    if (!look_error().empty())
        s.message += "; lighting override error: " + look_error();
    if (*pc_photo_last_error())
        s.message += std::string("; last photo error: ") + pc_photo_last_error();
    s.busy = recipe.on || seek_target >= 0 || capture_left > 0 || repro_frame >= 0 || jump_state.pending
        || jump_state.landing || landing;
    return s;
}

bool start_scene(const std::string& id, std::string* why)
{
    const Scene* found = nullptr;
    for (const Scene& s : scenes())
        if (s.id == id)
            found = &s;
    if (!found) {
        *why = "no scene " + id + " in the catalog";
        return false;
    }
    if (recipe.on) {
        *why = "a scene is being reached already";
        return false;
    }
    if (found->kind == "new_game" && !(&pc_game_phase && pc_game_phase == 1 && &pc_title_input_ready && pc_title_input_ready)) {
        *why = "New Game: go to the title menu first (the cursor on New Game)";
        return false;
    }
    if ((found->kind == "warp" && !pc_room_warp_request) || (found->kind == "ending" && !&pc_ending_request)) {
        *why = "this build has no Room Browser / ending jump";
        return false;
    }
    if (found->kind == "story" && (!pc_story_request || !pc_room_door)) {
        *why = "this build has no story scenes (battle builds only)";
        return false;
    }
    if (found->kind != "new_game" && found->kind != "story" && &pc_game_phase && pc_game_phase != 0) {
        *why = "start a game first: this scene is reached from the field";
        return false;
    }
    end_take();
    recipe = Recipe();
    recipe.on = true;
    recipe.scene = *found;
    message = "reaching " + found->name;
    return true;
}
bool start_here(const std::string& name, std::string* why)
{
    (void)why;
    begin_take(name.empty() ? "here" : name, name.empty() ? "from here" : name, false);
    playing = !pc_rpc_is_paused();
    on_frame(current_frame());
    return true;
}
void stop()
{
    if (recipe.on)
        finish_recipe("stopped");
    if (capture_left > 0) {
        capture_left = 0;
        cap_phase = CAP_IDLE;
        pc_photo_release(CAPTURE_OWNER);
    }
    free_camera(false, nullptr);
    end_take();
    message = "stopped";
}
void play()
{
    free_camera(false, nullptr);
    playing = true;
    seek_target = -1;
    pc_rpc_set_paused(0);
}
void pause()
{
    playing = false;
    pc_rpc_set_paused(1);
}
void step(int frames)
{
    free_camera(false, nullptr);
    playing = false;
    pc_rpc_request_step(std::max(1, frames));
}
void set_speed(int s) { speed = s == 2 || s == 4 ? s : 1; }
bool seek(int frame, std::string* why)
{
    if (!take.on) {
        *why = "no take";
        return false;
    }
    if (seek_target >= 0 || capture_left > 0 || repro_frame >= 0) {
        *why = "busy";
        return false;
    }
    /* Asked mid-vsync (the overlay, an RPC request): the boundary goes there (service ->
     * seek_service: steps forward, or rewinds to a mark first). The free camera's hold would keep the
     * game from stepping: it goes first (the game's camera back). */
    free_camera(false, nullptr);
    seek_target = std::max(frame, take.first);
    pc_rpc_set_paused(1);
    playing = false;
    return true;
}
void cut()
{
    if (!take.on)
        return;
    int f = current_frame();
    truncate_at(f);
    on_frame(f);
    message = "cut at frame " + std::to_string(f) + ": live from here";
}
bool capture(int count, std::string* why)
{
    if (capture_left > 0) {
        *why = "a capture is running";
        return false;
    }
    capture_left = std::max(1, std::min(10000, count));
    cap_phase = CAP_IDLE;
    capture_error.clear();
    pc_rpc_set_paused(1);
    playing = false;
    return true;
}
bool repro(std::string* why)
{
    if (!take.on) {
        *why = "no take";
        return false;
    }
    if (!pc_snapshot_point) {
        *why = "save states are unavailable in this build";
        return false;
    }
    int f = current_frame();
    const Mark* m = mark_at_or_before(f);
    if (!m) {
        *why = "no mark at or before this frame";
        return false;
    }
    repro_frame = f;
    repro_path.clear();
    after_seek = AFTER_REPRO;
    seek_target = m->frame;
    pc_rpc_set_paused(1);
    playing = false;
    return true;
}
bool jump(const std::string& path, int frame, std::string* why)
{
    prefab::Json j;
    std::string error;
    if (!prefab::json_parse(read_file(path).c_str(), j, &error)) {
        *why = "cannot read " + path + (error.empty() ? "" : ": " + error);
        return false;
    }
    auto str = [&](const char* k) {
        const prefab::Json* v = j.get(k);
        return v && v->type == prefab::Json::String ? v->string : std::string();
    };
    auto num = [&](const char* k) {
        const prefab::Json* v = j.get(k);
        return v && v->type == prefab::Json::Number ? (int)v->number : -1;
    };
    Jump p;
    p.snapshot = str("snapshot");
    p.scene = str("scene");
    p.name = str("name");
    p.base = num("base_frame");
    p.frame = frame >= 0 ? frame : num("frame");
    if (p.base < 0 || p.frame < p.base || p.snapshot.empty()) {
        *why = path + ": not a repro (scene, frame, base_frame, snapshot, log)";
        return false;
    }
    if (!read_log(str("log"), p.base, p.frames, p.scan)) {
        *why = "cannot read the repro's log " + str("log");
        return false;
    }
    if (!pc_snapshot_load_now) {
        *why = "save states are unavailable in this build";
        return false;
    }
    p.pending = true;
    jump_state = p;
    pc_rpc_set_paused(1);
    return true;
}
bool free_camera(bool on, std::string* why)
{
    if (!on) {
        // An external photo release can leave our flag out of sync until the
        // next presented frame. Releasing an owner twice is harmless.
        if (pc_photo_camera_finish)
            pc_photo_camera_finish(CAMERA_OWNER);
        pc_photo_resume(CAMERA_OWNER);
        camera_on = false;
        return true;
    }
    if (camera_on) {
        char owner[128] = "";
        if (pc_photo_camera_owner(owner, sizeof owner) && !strcmp(owner, CAMERA_OWNER))
            return true;
        camera_on = false; // the photo safety/state replacement released it
    }
    char w[200] = "";
    if (!pc_photo_camera_begin) {
        if (why)
            *why = "no photo camera in this build";
        return false;
    }
    if (pc_photo_pause(CAMERA_OWNER, w, (int)sizeof w) || pc_photo_camera_begin(CAMERA_OWNER, w, (int)sizeof w)) {
        pc_photo_resume(CAMERA_OWNER);
        if (why)
            *why = w;
        return false;
    }
    camera_on = true;
    return true;
}
bool free_camera_on() { return camera_on; }
bool can_return() { return !return_snapshot.empty() && pc_snapshot_load_now; }
bool return_to_game(std::string* why)
{
    if (!can_return()) {
        *why = "nothing to return to (no story scene was started from a game this session)";
        return false;
    }
    if (recipe.on)
        finish_recipe("stopped");
    free_camera(false, nullptr);
    end_take();
    return_pending = true;
    pc_rpc_set_paused(1);
    return true;
}

} // namespace cutscene

// ------------------------------------------------------------ RPC cutscene (dev_rpc.c)

namespace {
std::string status_json()
{
    cutscene::Status s = cutscene::status();
    std::string marks;
    for (int m : s.marks)
        marks += (marks.empty() ? "" : ",") + std::to_string(m);
    cutscene::EventInfo e = cutscene::event_info();
    std::string ids;
    for (int id : take.event_ids)
        ids += (ids.empty() ? "" : ",") + std::to_string(id);
    std::string threads;
    for (int i = 0; i < 4; i++) {
        if (e.threads[i].offset < 0)
            continue;
        threads += std::string(threads.empty() ? "" : ",") + "{\"thread\":" + std::to_string(i) + ",\"offset\":"
            + std::to_string(e.threads[i].offset) + ",\"opcode\":" + std::to_string(e.threads[i].opcode)
            + ",\"name\":" + quote(e.threads[i].name) + "}";
    }
    char head[1024];
    snprintf(head, sizeof head,
        "{\"on\":%s,\"rolling\":%s,\"scene\":%s,\"name\":%s,\"frame\":%d,\"first\":%d,\"head\":%d,"
        "\"replay_end\":%d,\"replaying\":%s,\"playing\":%s,\"paused\":%s,\"seek_target\":%d,\"speed\":%d,"
        "\"start_vsync\":%u,\"vsync\":%u,\"mark_bytes\":%zu,\"mark_ms\":%.3f,\"divergences\":%ld,\"busy\":%s,",
        s.on ? "true" : "false", s.rolling ? "true" : "false", quote(s.scene).c_str(), quote(s.name).c_str(),
        s.frame, s.first, s.head, s.replay_end, s.replaying ? "true" : "false", s.playing ? "true" : "false",
        s.paused ? "true" : "false", s.seek_target, s.speed, s.start_vsync, pc_vsync_count(), s.mark_bytes,
        s.mark_ms, s.divergences, s.busy ? "true" : "false");
    std::string out = head;
    out += "\"marks\":[" + marks + "],\"recipe\":" + quote(s.recipe) + ",\"capturing\":"
        + (s.capturing ? "true" : "false") + ",\"capture_left\":" + std::to_string(s.capture_left)
        + ",\"last_capture\":" + quote(s.last_capture) + ",\"capture_error\":" + quote(s.capture_error)
        + ",\"repro\":" + quote(s.repro_path) + ",\"message\":" + quote(s.message) + ",\"armed\":"
        + (cutscene::armed ? "true" : "false") + ",\"hide_text\":" + (cutscene::hide_text ? "true" : "false")
        + ",\"camera\":" + (camera_on ? "true" : "false") + ",\"dir\":" + quote(cutscene::dir())
        + ",\"settings\":{\"interval\":" + std::to_string(pc_take_interval) + ",\"marks_max\":"
        + std::to_string(pc_take_marks_max) + ",\"budget_mb\":" + std::to_string(pc_take_budget_mb)
        + ",\"instant_replay\":" + std::to_string(pc_instant_replay) + ",\"replay_seconds\":"
        + std::to_string(pc_instant_replay_seconds) + ",\"replay_interval\":"
        + std::to_string(pc_instant_replay_interval) + "},\"event\":{\"running\":" + (e.running ? "true" : "false")
        + ",\"id\":" + std::to_string(e.id) + ",\"state\":" + std::to_string(e.state) + ",\"skip\":"
        + std::to_string(e.skip) + ",\"threads\":[" + threads + "]},\"take_event\":{\"frames\":"
        + std::to_string(take.event_frames) + ",\"dialogue\":" + std::to_string(take.dialogue_frames) + ",\"places\":" + std::to_string(take.event_places.size())
        + ",\"ids\":[" + ids + "]}}";
    return out;
}
std::string scenes_json()
{
    std::string out = "{\"scenes\":[";
    bool first = true;
    for (const Scene& s : cutscene::scenes()) {
        out += std::string(first ? "" : ",") + "{\"id\":" + quote(s.id) + ",\"name\":" + quote(s.name) + ",\"kind\":"
            + quote(s.kind) + ",\"zone\":" + std::to_string(s.zone) + ",\"room\":" + std::to_string(s.room)
            + ",\"entry\":" + std::to_string(s.entry) + ",\"frames\":" + std::to_string(s.frames) + ",\"event\":"
            + std::to_string(s.event) + ",\"reliable\":" + (s.reliable ? "true" : "false") + ",\"from\":"
            + quote(s.from) + ",\"chapter\":" + quote(s.chapter) + ",\"step\":" + std::to_string(s.step)
            + ",\"start_event\":" + std::to_string(s.start_event) + ",\"texts\":" + std::to_string(s.texts)
            + ",\"verified\":" + (s.verified ? "true" : "false") + ",\"major\":" + (s.major ? "true" : "false") + "}";
        first = false;
    }
    return out + "]}";
}
} // namespace

/* params {action: status (default) | list | start {scene} | here {name} | stop | play | pause | step {frames}
 * | speed {value} | seek {frame} | back {frames} | cut | capture {count, hide_text} | repro | jump {repro,
 * frame} | settings {interval, marks_max, budget_mb, instant_replay, replay_seconds, replay_interval, armed,
 * hide_text} | hotkey | camera {on, position, target, yaw, pitch} | catalog (reload)}. The reply is the
 * status (list: the scenes); an error: -1 and the reason in out. Seeks, captures, repros and jumps finish at later vsyncs: wait for
 * busy false. */
extern "C" int pc_take_rpc(const char* params, char* out, int size)
{
    prefab::Json p;
    std::string why;
    if (!prefab::json_parse(params && *params ? params : "{}", p, &why) || p.type != prefab::Json::Object) {
        snprintf(out, (size_t)size, "params: a JSON object (%s)", why.c_str());
        return -1;
    }
    auto str = [&](const char* k, const char* def) {
        const prefab::Json* v = p.get(k);
        return v && v->type == prefab::Json::String ? v->string : std::string(def);
    };
    auto num = [&](const char* k, int* v) {
        const prefab::Json* j = p.get(k);
        if (j && j->type == prefab::Json::Number) {
            *v = (int)j->number;
            return true;
        }
        if (j && j->type == prefab::Json::Bool) {
            *v = j->boolean;
            return true;
        }
        return false;
    };
    std::string action = str("action", "status");
    if (action == "tracks" || action == "look")
        return cutscene::sequencer_rpc(p, out, size);
    bool ok = true;
    int n;
    if (action == "status") {
    } else if (action == "list") {
        std::string list = scenes_json();
        if ((int)list.size() >= size) {
            snprintf(out, (size_t)size, "the scene list does not fit the reply");
            return -1;
        }
        memcpy(out, list.c_str(), list.size() + 1);
        return 0;
    } else if (action == "catalog") {
        cutscene::reload_catalog();
    } else if (action == "start") {
        ok = cutscene::start_scene(str("scene", ""), &why);
    } else if (action == "here") {
        ok = cutscene::start_here(str("name", ""), &why);
    } else if (action == "stop") {
        cutscene::stop();
    } else if (action == "play") {
        cutscene::play();
    } else if (action == "pause") {
        cutscene::pause();
    } else if (action == "step") {
        cutscene::step(num("frames", &n) ? n : 1);
    } else if (action == "speed") {
        cutscene::set_speed(num("value", &n) ? n : 1);
    } else if (action == "seek") {
        ok = num("frame", &n) ? cutscene::seek(n, &why) : (why = "seek needs frame", false);
    } else if (action == "back") {
        ok = cutscene::seek(current_frame() - (num("frames", &n) ? n : 1), &why);
    } else if (action == "cut") {
        cutscene::cut();
    } else if (action == "capture") {
        if (num("hide_text", &n))
            cutscene::hide_text = n != 0;
        ok = cutscene::capture(num("count", &n) ? n : 1, &why);
    } else if (action == "repro") {
        ok = cutscene::repro(&why);
    } else if (action == "jump") {
        ok = cutscene::jump(str("repro", ""), num("frame", &n) ? n : -1, &why);
    } else if (action == "settings") {
        if (num("interval", &n)) pc_take_interval = std::max(1, std::min(3600, n));
        if (num("marks_max", &n)) pc_take_marks_max = std::max(2, std::min((int)TAKE_SLOTS, n));
        if (num("budget_mb", &n)) pc_take_budget_mb = std::max(64, n);
        if (num("instant_replay", &n)) pc_instant_replay = n != 0;
        if (num("replay_seconds", &n)) pc_instant_replay_seconds = std::max(2, std::min(600, n));
        if (num("replay_interval", &n)) pc_instant_replay_interval = std::max(1, std::min(3600, n));
        if (num("armed", &n)) cutscene::armed = n != 0;
        if (num("hide_text", &n)) cutscene::hide_text = n != 0;
    } else if (action == "hotkey") {
        pc_take_hotkey();
    } else if (action == "open") {
        cutscene::window_open = !num("on", &n) || n != 0;
    } else if (action == "return") {
        ok = cutscene::return_to_game(&why);
    } else if (action == "camera") {
        bool on = num("on", &n) ? n != 0 : true;
        ok = cutscene::free_camera(on, &why);
        /* A pose for the borrowed camera (tests, repros): position / target [x, y, z]
         * (world units, y down), yaw / pitch degrees; as rood.camera.set. */
        auto vec3 = [&](const char* k, float out[3]) {
            const prefab::Json* j = p.get(k);
            if (!j || j->type != prefab::Json::Array || j->items.size() != 3)
                return false;
            for (int i = 0; i < 3; i++)
                out[i] = (float)j->items[i].number;
            return true;
        };
        pc_photo_pose pose = {};
        unsigned mask = 0;
        const prefab::Json* f;
        if (vec3("position", pose.position))
            mask |= PC_POSE_POSITION;
        if (vec3("target", pose.target))
            mask |= PC_POSE_TARGET;
        if ((f = p.get("yaw")) && f->type == prefab::Json::Number)
            pose.yaw = (float)f->number, mask |= PC_POSE_YAW;
        if ((f = p.get("pitch")) && f->type == prefab::Json::Number)
            pose.pitch = (float)f->number, mask |= PC_POSE_PITCH;
        char w[200] = "";
        if (ok && on && mask && pc_photo_camera_set(CAMERA_OWNER, &pose, mask, w, (int)sizeof w)) {
            ok = false;
            why = w;
        }
    } else {
        ok = false;
        why = "action: status, list, catalog, start, here, stop, play, pause, step, speed, seek, back, cut, "
              "capture, repro, jump, settings, hotkey, camera, return or open";
    }
    if (!ok) {
        snprintf(out, (size_t)size, "%s", why.c_str());
        return -1;
    }
    std::string reply = status_json();
    if ((int)reply.size() >= size) {
        snprintf(out, (size_t)size, "the status does not fit the reply");
        return -1;
    }
    memcpy(out, reply.c_str(), reply.size() + 1);
    return 0;
}
