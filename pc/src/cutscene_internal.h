// The cutscene player's parts shared by its logic (cutscene.cpp) and its window (cutscene_panel.cpp).
#ifndef ROOD_CUTSCENE_INTERNAL_H
#define ROOD_CUTSCENE_INTERNAL_H
#include "cutscene.h"
#include <stddef.h>
#include <string>
#include <vector>

namespace cutscene {

struct Scene {
    std::string id, name, kind; // kind: new_game, warp, ending, story
    int zone = -1, room = -1, entry = 0;
    int frames = 0;             // how long it runs (0: unknown)
    int event = -1;             // the event id a scan saw ((source << 12) | file)
    bool reliable = false;
    std::string from, notes;
    // story scenes (pc/assets/cutscenes/story.json, pc/tools/story_scan.py)
    std::string chapter;        // the part of the story it belongs to (a heading in the viewer's list)
    int step = -1;              // the story step flag (130) when it starts
    int start_event = -1;       // an event started once the room's entry script is done (a trigger's; -1 none)
    int until = -1;             // the event to play on to (a script chained or handed over to; -1: at once)
    int until_room[2] = {-1, -1}; // ... in this room (zone, room; -1: any)
    int sound[4] = {0, 0, 0, 0}; // sound effect sets loaded before it (SE files in slots 1-3, the current slot)
    int cell[4] = {-1, 0, 0, 0}; // a floor cell to arrive on instead of a door (x, level, z, facing)
    std::vector<std::pair<int, int>> flags; // story flags just before it (index, value; the rest 0)
    int texts = 0;              // dialogue boxes it opens (the scan's count)
    bool verified = false;      // seen to start and run its event (story_scan.py --verify)
    bool major = false;         // a scene the story turns on (named; shown first)
};
const std::vector<Scene>& scenes();
void reload_catalog();
// The viewer's own: back to the game as it was before the first story scene of this session.
bool can_return();
bool return_to_game(std::string* why);

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
extern bool pad_focus; // the viewer has focus: the pad is its (not the game's) in a take
extern unsigned pad_focus_vsync; // when the window last said so (it is drawn only while the overlay shows)
const char* dir(); // pc/user/cutscenes (ROOD_CUTSCENES)
std::string replay_key_name();

} // namespace cutscene

extern "C" void pc_cutscene_window(void); // dev_overlay.cpp, its ImGui frame open
extern "C" void pc_cutscene_window_show(int on);
#endif
