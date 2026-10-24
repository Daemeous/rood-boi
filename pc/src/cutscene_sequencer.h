// Host-only cutscene timeline and non-destructive lighting override layer.
#ifndef ROOD_CUTSCENE_SEQUENCER_H
#define ROOD_CUTSCENE_SEQUENCER_H
#include <string>
#include <vector>
namespace prefab { struct Json; }
namespace cutscene {
struct EventInfo;
struct SequencerKey {
    int frame = 0, event = -1, thread = 0, offset = -1, opcode = -1;
    std::string name, group;
    std::vector<int> arguments; // Empty when unavailable; never guessed from an unknown instruction.
    int actor = -1, line = -1;
};
struct LookKey {
    int id = 0, frame = 0;
    std::string target, ease;
    float value = 0;
};
struct LookTarget { std::string name; float value, minimum, maximum; };
// Vsync interpreter-place samples, not a complete instruction dispatch trace.
const std::vector<SequencerKey>& sequencer_tracks();
const std::vector<LookKey>& look_keys();
const std::string& look_error();
std::vector<LookTarget> look_targets();
bool look_set(int id, int frame, const std::string& target, float value,
    const std::string& ease, std::string* why);
bool look_delete(int id);
bool look_save(std::string* why);
bool look_reload(std::string* why);
// Player lifecycle; all run on the game thread. Lighting also evaluates while paused/rewinding.
void sequencer_begin(const std::string& scene);
void sequencer_end();
void sequencer_sample(int frame, const EventInfo& event);
void sequencer_evaluate(int frame);
void sequencer_truncate(int frame);
void sequencer_prune(int first);
// Returns JSON or an error string, -1 on failure, 0 on success.
int sequencer_rpc(const prefab::Json& params, char* out, int size);
}
// room_settings.cpp's transient renderer-only layer: never marks room overrides or writes files.
std::vector<cutscene::LookTarget> pc_room_look_targets();
void pc_room_look_clear();
void pc_room_look_set(const char* target, float value);
#endif
