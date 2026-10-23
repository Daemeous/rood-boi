// The Cutscene window (Window > Cutscene; the instant replay key opens it): the story scene viewer (pick a
// scene by chapter, Play, a video player's controls) over the cutscene player's engine (cutscene.cpp), whose
// expert controls are folded under Advanced (pc/docs/Cutscene_Player.md).
#include "cutscene_internal.h"
#include "imgui.h"
#include "photo.h"

#include <SDL3/SDL.h> /* before boot_controller.h: SDL keeps its own linkage */
extern "C" {
#include "boot_controller.h" /* C declarations without extern "C" */
}
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
unsigned pc_vsync_count(void);
extern int pc_hw_interpolate;
int pc_overlay_set_details_tab(const char* name);
}

namespace {
const ImVec4 WARN(1, 0.6f, 0.4f, 1), DIM(0.6f, 0.6f, 0.6f, 1), LIVE(0.5f, 0.9f, 0.5f, 1), REPLAY(0.5f, 0.75f, 1, 1);

void tip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

/* The take's timeline: a slider over its frames with the marks drawn under it. */
void timeline(const cutscene::Status& s)
{
    static int dragging = -1;
    int frame = dragging >= 0 ? dragging : (s.seek_target >= 0 ? s.seek_target : s.frame);
    int lo = s.first, hi = std::max(s.first + 1, s.head - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##timeline", &frame, lo, hi, "frame %d"))
        dragging = frame;
    if (ImGui::IsItemDeactivated() && dragging >= 0) {
        std::string why;
        if (!cutscene::seek(dragging, &why))
            fprintf(stderr, "[cutscene] %s\n", why.c_str());
        dragging = -1;
    }
    tip("Drag to any frame of the take: it goes back to the nearest mark and replays to it exactly.");
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = b.x - a.x;
    for (int m : s.marks) {
        float x = a.x + w * (float)(m - lo) / (float)std::max(1, hi - lo);
        dl->AddLine(ImVec2(x, b.y), ImVec2(x, b.y + 5), IM_COL32(255, 200, 80, 255), 2);
    }
    ImGui::Dummy(ImVec2(1, 6));
}
} // namespace

namespace cutscene {
std::string replay_key_name()
{
    SDL_Keycode k = pc_bind_keys[PC_BIND_REPLAY][0];
    const char* n = k ? pc_key_name(k) : nullptr;
    std::string name = n ? n : "";
    for (char& c : name)
        c = (char)toupper((unsigned char)c);
    return name.empty() ? "(unbound)" : name;
}
} // namespace cutscene

extern "C" void pc_cutscene_window_show(int on) { cutscene::window_open = on != 0; }

namespace {
/* The player's expert controls (takes, marks, recipes, the event script, captures, repros): the engine under
 * the viewer, folded under "Advanced". */
void advanced(cutscene::Status& s)
{
    using namespace cutscene;
    std::string why;
    /* ---- reach a scene ---- */
    ImGui::SeparatorText("Scene");
    const auto& list = scenes();
    static int chosen = 0;
    if (chosen >= (int)list.size())
        chosen = 0;
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 9);
    if (ImGui::BeginCombo("##scene", list.empty() ? "(no catalog)" : list[(size_t)chosen].name.c_str())) {
        for (int i = 0; i < (int)list.size(); i++) {
            const Scene& sc = list[(size_t)i];
            std::string label = sc.name + (sc.reliable ? "" : "  (unverified)") + "##" + sc.id;
            if (ImGui::Selectable(label.c_str(), i == chosen))
                chosen = i;
            if (ImGui::IsItemHovered() && (!sc.from.empty() || !sc.notes.empty()))
                ImGui::SetTooltip("%s%s%s", sc.from.c_str(), sc.from.empty() || sc.notes.empty() ? "" : "\n",
                    sc.notes.c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(list.empty() || !s.recipe.empty());
    if (ImGui::Button("Start") && !start_scene(list[(size_t)chosen].id, &why))
        fprintf(stderr, "[cutscene] %s\n", why.c_str());
    ImGui::EndDisabled();
    tip("Reach the scene by its recipe (New Game from the title menu, a warp with the room's scripts on, the "
        "ending) and start a take at its first frame.");
    ImGui::SameLine();
    if (ImGui::Button("From here"))
        start_here("", &why);
    tip("Start a take now, whatever runs.");
    if (!list.empty()) {
        const Scene& sc = list[(size_t)chosen];
        ImGui::TextColored(DIM, "%s%s", sc.from.c_str(), sc.frames ? (" - about " + std::to_string(sc.frames) + " frames").c_str() : "");
    }
    ImGui::Checkbox("Start a take when an event script starts", &armed);
    bool replay = pc_instant_replay != 0;
    if (ImGui::Checkbox("Instant replay", &replay))
        pc_instant_replay = replay;
    tip("Keep the last seconds of play as a take that always runs (marks and the input log): after a glitch, "
        "press the instant replay key to freeze, then go back to it.");
    ImGui::SameLine();
    ImGui::TextColored(DIM, "key %s", replay_key_name().c_str());

    /* ---- the take ---- */
    ImGui::SeparatorText("Take");
    if (!s.recipe.empty())
        ImGui::TextColored(WARN, "Reaching the scene (%s)...", s.recipe.c_str());
    if (!s.on) {
        ImGui::TextDisabled("No take. Start a scene, take from here, or turn on the instant replay.");
    } else {
        ImGui::Text("%s  frame %d", s.name.c_str(), s.frame);
        ImGui::SameLine();
        if (s.replaying)
            ImGui::TextColored(REPLAY, "REPLAY (to %d)", s.replay_end);
        else
            ImGui::TextColored(LIVE, "LIVE");
        tip("REPLAY: the frame's input comes from the take's log (exactly as before). LIVE: played now and "
            "added to the take.");
        timeline(s);
        /* Transport. */
        auto go = [&](int f) {
            if (!seek(f, &why))
                fprintf(stderr, "[cutscene] %s\n", why.c_str());
        };
        ImGui::BeginDisabled(s.busy);
        if (ImGui::Button("|<"))
            go(s.first);
        tip("The take's first frame");
        ImGui::SameLine();
        if (ImGui::Button("<M")) {
            int target = s.first;
            for (int m : s.marks)
                if (m < s.frame)
                    target = m;
            go(target);
        }
        tip("The previous mark");
        ImGui::SameLine();
        if (ImGui::Button("<1"))
            go(s.frame - 1);
        tip("One frame back (to the mark before it, then replayed)");
        ImGui::SameLine();
        if (ImGui::Button(s.paused ? "Play" : "Pause", ImVec2(ImGui::GetFontSize() * 4, 0)))
            s.paused ? play() : pause();
        ImGui::SameLine();
        if (ImGui::Button("1>"))
            step(1);
        tip("One frame forward");
        ImGui::SameLine();
        if (ImGui::Button("M>")) {
            int target = -1;
            for (int m : s.marks)
                if (m > s.frame && target < 0)
                    target = m;
            if (target >= 0)
                go(target);
        }
        tip("The next mark");
        ImGui::SameLine();
        if (ImGui::Button(">|"))
            go(s.head - 1);
        tip("The newest frame of the take");
        ImGui::EndDisabled();
        int sp = s.speed;
        ImGui::Text("Speed");
        ImGui::SameLine();
        if (ImGui::RadioButton("1", sp == 1))
            set_speed(1);
        ImGui::SameLine();
        if (ImGui::RadioButton("1/2", sp == 2))
            set_speed(2);
        ImGui::SameLine();
        if (ImGui::RadioButton("1/4", sp == 4))
            set_speed(4);
        ImGui::SameLine();
        bool interp = pc_hw_interpolate != 0;
        if (ImGui::Checkbox("Interpolation", &interp))
            pc_hw_interpolate = interp;
        tip("Frame interpolation draws in-between pictures: off, every picture is a game frame.");
        static int go_frame = 0;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputInt("##goto", &go_frame, 0);
        ImGui::SameLine();
        ImGui::BeginDisabled(s.busy);
        if (ImGui::Button("Go to frame"))
            go(go_frame);
        ImGui::SameLine();
        if (ImGui::Button("Cut here"))
            cut();
        tip("Drop the take after this frame: what plays from here is live (another branch).");
        ImGui::SameLine();
        ImGui::EndDisabled();
        if (ImGui::Button("Stop"))
            stop();
        ImGui::TextColored(DIM, "%d marks, %.0f MB, last mark %.1f ms; frames %d-%d%s", (int)s.marks.size(),
            s.mark_bytes / 1048576.0, s.mark_ms, s.first, s.head - 1,
            s.divergences ? "  (replay read past its log!)" : "");
        if (s.divergences)
            ImGui::TextColored(WARN, "%ld replayed reads were not in the log: this part may not replay exactly",
                s.divergences);
    }
    if (ImGui::TreeNode("Marks")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputInt("Every N frames", &pc_take_interval);
        pc_take_interval = std::max(1, std::min(3600, pc_take_interval));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputInt("At most", &pc_take_marks_max);
        pc_take_marks_max = std::max(2, std::min(512, pc_take_marks_max));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputInt("Memory (MB)", &pc_take_budget_mb, 64);
        pc_take_budget_mb = std::max(64, pc_take_budget_mb);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputInt("Instant replay seconds", &pc_instant_replay_seconds);
        pc_instant_replay_seconds = std::max(2, std::min(600, pc_instant_replay_seconds));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputInt("Instant replay mark every", &pc_instant_replay_interval);
        pc_instant_replay_interval = std::max(1, std::min(3600, pc_instant_replay_interval));
        ImGui::TextColored(DIM, "Saved with File > Save settings.");
        ImGui::TreePop();
    }

    /* ---- the event script ---- */
    ImGui::SeparatorText("Event script");
    EventInfo e = event_info();
    if (!e.running) {
        ImGui::TextDisabled("none running");
    } else {
        static const char* const sources[] = { "zone EVT", "room entry", "door" };
        int source = (e.id >> 12) & 0xF;
        ImGui::Text("event 0x%04x  (%s %d)  state %d%s", e.id, source < 3 ? sources[source] : "?", e.id & 0xFFF,
            e.state, e.skip == 1 ? "  skippable" : e.skip == 2 ? "  skipping" : "");
        for (int i = 0; i < 4; i++)
            if (e.threads[i].offset >= 0)
                ImGui::Text("thread %d  +0x%04x  op 0x%02x  %s", i, e.threads[i].offset, e.threads[i].opcode,
                    e.threads[i].name.c_str());
    }

    /* ---- capture ---- */
    ImGui::SeparatorText("Capture");
    ImGui::BeginDisabled(s.capturing || s.busy);
    if (ImGui::Button("Capture frame") && !capture(1, &why))
        fprintf(stderr, "[cutscene] %s\n", why.c_str());
    tip("A clean PNG (HUD and this overlay hidden) of the frozen moment, through the photo mode's capture.");
    ImGui::SameLine();
    if (ImGui::Button("Capture next") && !capture(capture_count, &why))
        fprintf(stderr, "[cutscene] %s\n", why.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    ImGui::InputInt("frames", &capture_count, 0);
    capture_count = std::max(1, std::min(10000, capture_count));
    ImGui::EndDisabled();
    ImGui::Checkbox("Hide dialogue too", &hide_text);
    tip("Captures hide the game's 2D layer (talk bubbles, narration, menus) as well.");
    bool cam = free_camera_on();
    if (ImGui::Checkbox("Free camera", &cam) && !free_camera(cam, &why))
        fprintf(stderr, "[cutscene] %s\n", why.c_str());
    tip("Hold the frame and look around with the editor's free camera (the photo mode's); off gives the game's "
        "camera back exactly.");
    if (s.capturing)
        ImGui::TextColored(WARN, "capturing, %d left...", s.capture_left);
    if (!s.last_capture.empty())
        ImGui::TextColored(DIM, "%s", s.last_capture.c_str());
    if (!s.capture_error.empty())
        ImGui::TextColored(WARN, "%s", s.capture_error.c_str());

    /* ---- repro ---- */
    ImGui::SeparatorText("Repro");
    ImGui::BeginDisabled(!s.on || s.busy);
    static bool copy_when_written = false;
    if (ImGui::Button("Copy repro")) {
        if (repro(&why))
            copy_when_written = true;
        else
            fprintf(stderr, "[cutscene] %s\n", why.c_str());
    }
    tip("Save this frame for someone else: the state at the mark before it, the take's log and the settings; "
        "the .json path is put on the clipboard. RPC cutscene {action: \"jump\", repro: path} opens it.");
    ImGui::EndDisabled();
    if (copy_when_written && !s.busy) {
        copy_when_written = false;
        if (!s.repro_path.empty())
            SDL_SetClipboardText(s.repro_path.c_str());
    }
    static char open_path[512];
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 5);
    ImGui::InputTextWithHint("##repro", "a repro .json to open", open_path, sizeof open_path);
    ImGui::SameLine();
    ImGui::BeginDisabled(!open_path[0] || s.busy);
    if (ImGui::Button("Open") && !jump(open_path, -1, &why))
        fprintf(stderr, "[cutscene] %s\n", why.c_str());
    ImGui::EndDisabled();
    if (!s.repro_path.empty())
        ImGui::TextColored(DIM, "%s", s.repro_path.c_str());
}
} // namespace

namespace {
const char* const VIEW_OWNER = "cutscene.view"; /* the viewer's Hide HUD / Hide dialogue (photo.h) */
int view_hide = 0;
std::string chosen_id;
bool scroll_to_chosen = false; // the list shows the chosen scene next frame

std::string clock_text(int frames)
{
    int s = std::max(0, frames) / 60;
    char b[32];
    snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

/* The scenes in the viewer's order: the opening (the player's New Game recipe: the prologue from its first
 * frame), the story's (story.json, by chapter), the ending, then the rest of the player's catalog (the
 * scanned room scenes) as "More". */
std::vector<const cutscene::Scene*> ordered()
{
    std::vector<const cutscene::Scene*> out;
    const auto& all = cutscene::scenes();
    for (const cutscene::Scene& sc : all)
        if (sc.kind == "new_game")
            out.push_back(&sc);
    for (const cutscene::Scene& sc : all)
        if (sc.kind == "story")
            out.push_back(&sc);
    for (const cutscene::Scene& sc : all)
        if (sc.kind == "ending")
            out.push_back(&sc);
    for (const cutscene::Scene& sc : all)
        if (sc.kind != "story" && sc.kind != "new_game" && sc.kind != "ending")
            out.push_back(&sc);
    return out;
}
std::string chapter_of(const cutscene::Scene& sc)
{
    if (sc.kind == "story")
        return sc.chapter;
    if (sc.kind == "new_game")
        return "Prologue";
    if (sc.kind == "ending")
        return "Ending";
    return "More (rooms and recipes)";
}

bool matches(const cutscene::Scene& sc, const char* filter)
{
    if (!filter[0])
        return true;
    std::string hay = sc.name + " " + chapter_of(sc) + " " + sc.id + " " + sc.notes;
    std::string needle = filter;
    for (char& c : hay)
        c = (char)tolower((unsigned char)c);
    for (char& c : needle)
        c = (char)tolower((unsigned char)c);
    return hay.find(needle) != std::string::npos;
}

void play_scene(const std::string& id)
{
    std::string why;
    chosen_id = id;
    if (!cutscene::start_scene(id, &why))
        fprintf(stderr, "[cutscene] %s\n", why.c_str());
}

/* Next / previous story scene from the one playing (or chosen). */
void neighbour(int dir)
{
    auto list = ordered();
    std::string at = cutscene::status().scene;
    if (at.empty())
        at = chosen_id;
    for (int i = 0; i < (int)list.size(); i++)
        if (list[(size_t)i]->id == at) {
            int j = i + dir;
            if (j >= 0 && j < (int)list.size())
                play_scene(list[(size_t)j]->id);
            return;
        }
    if (!list.empty())
        play_scene(list[0]->id);
}

/* The scrub bar over the scene: frames played so far replay exactly (from the marks); dragging past them
 * plays on live to the frame. */
void scrub(const cutscene::Status& s, int length)
{
    static int dragging = -1;
    int frame = dragging >= 0 ? dragging : (s.seek_target >= 0 ? s.seek_target : s.frame);
    int lo = s.first, hi = std::max({ s.first + 1, s.head - 1, s.first + length });
    ImGui::SetNextItemWidth(-1);
    std::string label = clock_text(frame - lo) + " / " + clock_text(hi - lo);
    if (ImGui::SliderInt("##scrub", &frame, lo, hi, label.c_str()))
        dragging = frame;
    if (ImGui::IsItemDeactivated() && dragging >= 0) {
        std::string why;
        if (!cutscene::seek(dragging, &why))
            fprintf(stderr, "[cutscene] %s\n", why.c_str());
        dragging = -1;
    }
    tip("Drag anywhere in the scene. Back: it replays exactly from the nearest mark. Past what has played: it "
        "plays on to there.");
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = b.x - a.x, span = (float)std::max(1, hi - lo);
    float played = a.x + w * (float)(std::max(s.head - 1, lo) - lo) / span;
    dl->AddRectFilled(ImVec2(a.x, b.y + 1), ImVec2(played, b.y + 4), IM_COL32(110, 170, 255, 200));
    for (int m : s.marks) {
        float x = a.x + w * (float)(m - lo) / span;
        dl->AddLine(ImVec2(x, b.y + 1), ImVec2(x, b.y + 6), IM_COL32(255, 200, 80, 255), 1.5f);
    }
    ImGui::Dummy(ImVec2(1, 6));
}

/* Keys and the pad while the window has focus (the pad is the viewer's then, not the game's: the overlay
 * never opens gamepads, so it is read here): Space / Triangle play-pause, Left / Right and L2 / R2 one frame,
 * Shift+Left / Right five seconds, Page Up / Down and L1 / R1 the previous / next scene, Home restart, the
 * d-pad up / down choose a scene and Cross plays it, Square captures the frame. */
enum : unsigned { PAD_L2 = 1u << 0, PAD_R2 = 1u << 1, PAD_L1 = 1u << 2, PAD_R1 = 1u << 3, PAD_TRI = 1u << 4,
    PAD_CROSS = 1u << 6, PAD_SQUARE = 1u << 7, PAD_UP = 1u << 12, PAD_DOWN = 1u << 14 };
void choose(int dir)
{
    auto list = ordered();
    for (int i = 0; i < (int)list.size(); i++)
        if (list[(size_t)i]->id == chosen_id) {
            int j = std::max(0, std::min((int)list.size() - 1, i + dir));
            chosen_id = list[(size_t)j]->id;
            scroll_to_chosen = true;
            return;
        }
    if (!list.empty())
        chosen_id = list[0]->id;
}
void shortcuts(const cutscene::Status& s)
{
    static unsigned held;
    bool focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    cutscene::pad_focus = focus;
    cutscene::pad_focus_vsync = pc_vsync_count();
    unsigned pad = focus ? pc_controller_buttons() : 0;
    unsigned pressed = pad & ~held;
    held = pad;
    if (!focus)
        return;
    std::string why;
    bool shift = ImGui::GetIO().KeyShift;
    if (pressed & PAD_UP)
        choose(-1);
    if (pressed & PAD_DOWN)
        choose(1);
    if ((pressed & PAD_CROSS) && !chosen_id.empty())
        play_scene(chosen_id);
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false) || (pressed & PAD_TRI))
        s.paused ? cutscene::play() : cutscene::pause();
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false) || (pressed & PAD_L1))
        neighbour(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false) || (pressed & PAD_R1))
        neighbour(1);
    if (!s.on || s.busy)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || (pressed & PAD_R2))
        shift ? (void)cutscene::seek(s.frame + 300, &why) : cutscene::step(1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || (pressed & PAD_L2))
        (void)cutscene::seek(s.frame - (shift ? 300 : 1), &why);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        (void)cutscene::seek(s.first, &why);
    if (pressed & PAD_SQUARE)
        (void)cutscene::capture(1, &why);
}

/* The viewer: the story's scenes by chapter, Play, and a video player's controls. */
void viewer(cutscene::Status& s)
{
    using namespace cutscene;
    std::string why;
    auto list = ordered();
    static std::string last_playing;
    if (s.scene != last_playing) { /* the list follows the scene playing (Play, next / previous, RPC) */
        last_playing = s.scene;
        if (!s.scene.empty() && s.scene != "replay")
            chosen_id = s.scene, scroll_to_chosen = true;
    }
    const Scene* playing_scene = nullptr;
    const Scene* chosen = nullptr;
    for (const Scene* sc : list) {
        if (sc->id == s.scene)
            playing_scene = sc;
        if (sc->id == chosen_id)
            chosen = sc;
    }
    if (!chosen && !list.empty())
        chosen = list[0], chosen_id = chosen->id;

    /* ---- the list ---- */
    static char filter[64];
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 10.5f);
    ImGui::InputTextWithHint("##find", "Search scenes (name, chapter, place)", filter, sizeof filter);
    ImGui::SameLine();
    static bool verified_only = false, key_only = false;
    ImGui::Checkbox("Key", &key_only);
    tip("Only the scenes the story turns on (marked *).");
    ImGui::SameLine();
    ImGui::Checkbox("Verified", &verified_only);
    tip("Only scenes seen to start and play (pc/tools/story_verify.py).");
    float list_h = ImGui::GetFontSize() * 14;
    if (ImGui::BeginChild("##scenes", ImVec2(-1, list_h), ImGuiChildFlags_Borders)) {
        std::string chapter = "\x01";
        bool open = true;
        int shown = 0;
        for (const Scene* sc : list) {
            if (!matches(*sc, filter) || (verified_only && !sc->reliable)
                || (key_only && sc->kind == "story" && !sc->major) || (key_only && sc->kind == "warp"))
                continue;
            std::string ch = chapter_of(*sc);
            if (ch != chapter) {
                if (chapter != "\x01" && open)
                    ImGui::TreePop();
                chapter = ch;
                ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_SpanAvailWidth
                    | (sc->kind != "warp" || filter[0] ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                open = ImGui::TreeNodeEx(ch.empty() ? "Story" : ch.c_str(), fl);
            }
            if (!open)
                continue;
            ++shown;
            bool sel = sc->id == chosen_id;
            bool now = sc->id == s.scene;
            std::string label = std::string(now ? "> " : "") + (sc->major ? "* " : "") + sc->name + "##" + sc->id;
            if (!sc->reliable)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                chosen_id = sc->id;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    play_scene(sc->id);
            }
            if (!sc->reliable)
                ImGui::PopStyleColor();
            if (sel && scroll_to_chosen) {
                ImGui::SetScrollHereY(0.3f);
                scroll_to_chosen = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s%s%s", sc->from.c_str(), sc->notes.empty() ? "" : "\n", sc->notes.c_str(),
                    sc->reliable ? "" : "\n(not verified to play)");
            if (sel && ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                    || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)))
                play_scene(sc->id);
        }
        if (chapter != "\x01" && open)
            ImGui::TreePop();
        if (!shown)
            ImGui::TextDisabled(list.empty() ? "No scenes (pc/assets/cutscenes/story.json)" : "No scene matches");
    }
    ImGui::EndChild();

    /* ---- the chosen scene ---- */
    if (chosen) {
        ImGui::TextWrapped("%s", chosen->name.c_str());
        ImGui::TextColored(DIM, "%s%s%s", chapter_of(*chosen).c_str(),
            chosen->texts ? ("  -  " + std::to_string(chosen->texts) + " lines").c_str() : "",
            chosen->frames ? ("  -  about " + clock_text(chosen->frames)).c_str() : "");
    }
    ImGui::BeginDisabled(!chosen || !s.recipe.empty());
    if (ImGui::Button("Play", ImVec2(ImGui::GetFontSize() * 6, 0)) && chosen)
        play_scene(chosen->id);
    ImGui::EndDisabled();
    tip("Start the scene as it plays in the story: its story flags, its room, its actors. The game is replaced "
        "(Back to the game returns to it). Double-click or Enter in the list does the same; on the pad, the "
        "d-pad chooses and Cross plays while this window has focus.");
    if (can_return()) {
        ImGui::SameLine();
        if (ImGui::Button("Back to the game") && !return_to_game(&why))
            fprintf(stderr, "[cutscene] %s\n", why.c_str());
        tip("Return to the game as it was before the first scene was played (a save state taken then).");
    }
    if (!s.recipe.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(WARN, "Loading the scene...");
    }

    /* ---- the player ---- */
    ImGui::SeparatorText(s.on ? (playing_scene ? playing_scene->name.c_str() : s.name.c_str()) : "Player");
    int length = playing_scene && playing_scene->frames ? playing_scene->frames + 120 : 1800;
    shortcuts(s);
    if (!s.on) {
        ImGui::TextDisabled("Pick a scene and Play.");
        return;
    }
    scrub(s, length);
    auto go = [&](int f) {
        if (!seek(f, &why))
            fprintf(stderr, "[cutscene] %s\n", why.c_str());
    };
    float em = ImGui::GetFontSize();
    ImGui::BeginDisabled(s.busy);
    if (ImGui::Button("<<|"))
        neighbour(-1);
    tip("The previous scene (Page Up, L1)");
    ImGui::SameLine();
    if (ImGui::Button("|<"))
        go(s.first);
    tip("Restart the scene (Home)");
    ImGui::SameLine();
    if (ImGui::Button("-5s"))
        go(s.frame - 300);
    tip("Five seconds back (Shift+Left)");
    ImGui::SameLine();
    if (ImGui::Button("<1"))
        go(s.frame - 1);
    tip("One frame back (Left, L2)");
    ImGui::SameLine();
    ImGui::EndDisabled();
    if (ImGui::Button(s.paused ? "Play##transport" : "Pause##transport", ImVec2(em * 4.5f, 0)))
        s.paused ? play() : pause();
    tip("Space, Triangle");
    ImGui::SameLine();
    ImGui::BeginDisabled(s.busy);
    if (ImGui::Button("1>"))
        step(1);
    tip("One frame forward (Right, R2)");
    ImGui::SameLine();
    if (ImGui::Button("+5s"))
        go(s.frame + 300);
    tip("Five seconds on (Shift+Right)");
    ImGui::SameLine();
    if (ImGui::Button("|>>"))
        neighbour(1);
    tip("The next scene (Page Down, R1)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (s.replaying)
        ImGui::TextColored(REPLAY, "replay");
    else
        ImGui::TextColored(LIVE, "live");
    tip("replay: this part has played before and plays again exactly; live: playing for the first time");

    int sp = s.speed;
    ImGui::Text("Speed");
    ImGui::SameLine();
    if (ImGui::RadioButton("1x", sp == 1))
        set_speed(1);
    ImGui::SameLine();
    if (ImGui::RadioButton("1/2", sp == 2))
        set_speed(2);
    ImGui::SameLine();
    if (ImGui::RadioButton("1/4", sp == 4))
        set_speed(4);
    ImGui::SameLine();
    bool adv = pc_cutscene_auto_advance != 0;
    if (ImGui::Checkbox("Auto-advance dialogue", &adv))
        pc_cutscene_auto_advance = adv;
    tip("Dialogue goes on by itself after the hold time, as a viewer would press the button (a logged input: "
        "replays stay exact). Off: press it yourself.");
    if (adv) {
        float hold = pc_cutscene_dialogue_hold / 60.0f;
        ImGui::SetNextItemWidth(em * 8);
        if (ImGui::SliderFloat("Hold", &hold, 0.5f, 10.0f, "%.1f s"))
            pc_cutscene_dialogue_hold = (int)(hold * 60 + 0.5f);
        tip("How long a dialogue box stays (from when it opens). Saved with File > Save settings "
            "(cutscene_dialogue_hold).");
    }

    bool cam = free_camera_on();
    if (ImGui::Checkbox("Free camera", &cam) && !free_camera(cam, &why))
        fprintf(stderr, "[cutscene] %s\n", why.c_str());
    tip("Hold the frame and look around (the photo mode's camera); off gives the game's camera back exactly.");
    ImGui::SameLine();
    bool hud = view_hide & PC_PHOTO_HIDE_HUD, text = view_hide & PC_PHOTO_HIDE_GAME2D;
    bool changed = ImGui::Checkbox("Hide HUD", &hud);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Hide dialogue", &text);
    tip("Hide the game's 2D layer while watching: talk bubbles, narration, menus.");
    if (changed) {
        view_hide = (hud ? PC_PHOTO_HIDE_HUD : 0) | (text ? PC_PHOTO_HIDE_GAME2D : 0);
        pc_photo_ui_hide(VIEW_OWNER, view_hide);
    }
    ImGui::BeginDisabled(s.capturing || s.busy);
    if (ImGui::Button("Capture frame")) {
        hide_text = text;
        if (!capture(1, &why))
            fprintf(stderr, "[cutscene] %s\n", why.c_str());
    }
    ImGui::EndDisabled();
    tip("A clean PNG of this moment (HUD and the overlay hidden; dialogue too with Hide dialogue), in "
        "pc/user/cutscenes/<scene>/");
    ImGui::SameLine();
    if (ImGui::Button("Lighting")) {
        pause();
        pc_overlay_set_details_tab("Lighting");
    }
    tip("Pause here and open the Lighting tab for this room (the room the scene plays in). Per-scene lighting "
        "is planned: pc/docs/plans/2026-10-22-cutscene-viewer.md.");
    if (s.capturing)
        ImGui::TextColored(WARN, "capturing...");
    else if (!s.last_capture.empty())
        ImGui::TextColored(DIM, "%s", s.last_capture.c_str());
}
} // namespace

extern "C" void pc_cutscene_window(void)
{
    using namespace cutscene;
    if (!window_open)
        return;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 32, ImGui::GetFontSize() * 46), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cutscene", &window_open)) {
        ImGui::End();
        return;
    }
    Status s = status();
    viewer(s);
    if (ImGui::CollapsingHeader("Advanced")) {
        ImGui::Indent();
        advanced(s);
        ImGui::Unindent();
    }
    if (!s.message.empty())
        ImGui::TextWrapped("%s", s.message.c_str());
    ImGui::End();
}
