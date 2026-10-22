// The Cutscene window (Window > Cutscene; the instant replay key opens it): the cutscene
// player's controls over cutscene.cpp (pc/docs/Cutscene_Player.md).
#include "cutscene_internal.h"
#include "imgui.h"

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
    if (!s.message.empty())
        ImGui::TextWrapped("%s", s.message.c_str());
    ImGui::End();
}
