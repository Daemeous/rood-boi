// The cutscene player's parts shared by its logic (cutscene.cpp) and its window (cutscene_panel.cpp).
#ifndef ROOD_CUTSCENE_INTERNAL_H
#define ROOD_CUTSCENE_INTERNAL_H
#include "cutscene.h"
#include <stddef.h>
#include <string>
#include <vector>

namespace cutscene {

struct Scene {
    std::string id, name, kind; // kind: new_game, warp, ending
    int zone = -1, room = -1, entry = 0;
    int frames = 0;             // how long it runs (0: unknown)
    int event = -1;             // the event id a scan saw ((source << 12) | file)
    bool reliable = false;
    std::string from, notes;
};
const std::vector<Scene>& scenes();
void reload_catalog();

struct Thread {
    int offset = -1, opcode = -1;
    std::string name; // the opcode's handler (pc/assets/cutscenes/opcodes.json)
};
struct EventInfo {
    bool running = false;
    int id = -1, state = 0, skip = 0;
    Thread threads[4];
};
EventInfo event_info();

struct Status {
    bool on = false, rolling = false, playing = false, paused = false, replaying = false;
    std::string scene, name;
    int frame = 0, first = 0, head = 0, replay_end = 0;
    int seek_target = -1;
    int speed = 1;
    std::vector<int> marks; // frames
    size_t mark_bytes = 0;
    double mark_ms = 0;     // the last mark's cost
    long divergences = 0;   // replayed reads past what the log holds (should stay 0)
    unsigned start_vsync = 0;
    std::string recipe;     // a scene being reached: its kind and phase
    bool capturing = false;
    int capture_left = 0;
    std::string last_capture, capture_error, repro_path, message;
    bool busy = false;      // a seek, capture, repro, recipe or jump in progress
};
Status status();

bool start_scene(const std::string& id, std::string* why);
bool start_here(const std::string& name, std::string* why);
void stop();
void play();
void pause();
void step(int frames);
void set_speed(int speed); // 1, 2, 4
bool seek(int frame, std::string* why);
void cut();
bool capture(int count, std::string* why);
bool repro(std::string* why);
bool jump(const std::string& path, int frame, std::string* why);
bool free_camera(bool on, std::string* why);
bool free_camera_on();

extern bool armed;     // start a take when an event script starts
extern bool hide_text; // captures hide the game's 2D layer (dialogue) too
extern int capture_count;
extern bool window_open;
const char* dir(); // pc/user/cutscenes (ROOD_CUTSCENES)
std::string replay_key_name();

} // namespace cutscene

extern "C" void pc_cutscene_window(void); // dev_overlay.cpp, its ImGui frame open
extern "C" void pc_cutscene_window_show(int on);
#endif
