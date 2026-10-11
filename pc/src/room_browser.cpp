// The editor's Room Browser (Window > Room Browser, toolbar "Rooms"): every
// zone and room of the game, searchable, with the current one highlighted,
// Previous/Next, and Go (the game loads the room: room_warp.c).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "imgui.h"

#include "room_warp.h"

extern "C" {
int pc_lights_room(int* z, int* r);
int pc_editor_active(void);
void pc_editor_toggle(void);
int pc_rpc_is_paused(void);
void pc_rpc_set_paused(int paused);
}

bool pc_room_browser_open;
extern "C" void pc_room_browser_show(int shown) { pc_room_browser_open = shown != 0; }
static bool restore_pending, restore_free_camera, restore_paused;

static std::string room_label(const pc_room_info* r)
{
    char text[200];
    if (r->name[0])
        snprintf(text, sizeof text, "%02d/%02d  %s", r->zone, r->room, r->name);
    else
        snprintf(text, sizeof text, "%02d/%02d  (zone %d room %d)", r->zone, r->room, r->zone, r->room);
    return text;
}

/* Ask the game to go there: running (a paused game cannot change rooms),
 * on the game camera; the free camera and the pause come back on arrival. */
extern "C" int pc_room_browser_go(int zone, int room, int entry, char* why, int size)
{
    if (!pc_room_warp_request) {
        snprintf(why, size, "rooms need the battle build");
        return 0;
    }
    if (!pc_room_warp_request(zone, room, entry, why, size))
        return 0;
    if (!restore_pending) {
        restore_free_camera = pc_editor_active() != 0;
        restore_paused = pc_rpc_is_paused() != 0;
    }
    restore_pending = true;
    if (pc_editor_active())
        pc_editor_toggle();
    pc_rpc_set_paused(0);
    return 1;
}

/* Every overlay frame (shown or not): finish a warp. */
extern "C" void pc_room_browser_frame(void)
{
    if (!pc_room_warp_phase || pc_room_warp_phase() != 4)
        return;
    pc_room_warp_acknowledge();
    if (!restore_pending)
        return;
    restore_pending = false;
    if (restore_free_camera && !pc_editor_active())
        pc_editor_toggle(); /* from the new room's game camera */
    if (restore_paused)
        pc_rpc_set_paused(1);
}

extern "C" void pc_room_browser_window(void)
{
    if (!pc_room_browser_open)
        return;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, ImGui::GetFontSize() * 34), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Room Browser", &pc_room_browser_open)) {
        ImGui::End();
        return;
    }
    if (!pc_rooms_count) {
        ImGui::TextDisabled("The Room Browser needs the battle build.");
        ImGui::End();
        return;
    }
    int count = pc_rooms_count();
    if (!count) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "No rooms read from the disc: %s", pc_rooms_error());
        ImGui::End();
        return;
    }
    int z = -1, r = -1;
    pc_lights_room(&z, &r);
    int current = -1;
    for (int i = 0; i < count; i++)
        if (pc_rooms_get(i)->zone == z && pc_rooms_get(i)->room == r)
            current = i;
    static int chosen = -1;
    static char filter[64];
    static bool follow = true;
    char why[200] = "";
    auto go = [&](int i) {
        const pc_room_info* t = pc_rooms_get(i);
        if (t && !pc_room_browser_go(t->zone, t->room, 0, why, sizeof why))
            fprintf(stderr, "[rooms] %s\n", why);
        chosen = i;
    };
    /* Previous / Next: the neighbours in the list that have a door in. */
    auto step = [&](int from, int direction) {
        for (int i = from + direction; i >= 0 && i < count; i += direction)
            if (pc_rooms_get(i)->entries)
                return i;
        return -1;
    };
    int base = current >= 0 ? current : chosen;
    int previous = step(base < 0 ? count : base, -1), next = step(base, 1);
    ImGui::BeginDisabled(previous < 0);
    if (ImGui::ArrowButton("##previous", ImGuiDir_Left))
        go(previous);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && previous >= 0)
        ImGui::SetTooltip("Previous: %s", room_label(pc_rooms_get(previous)).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(next < 0);
    if (ImGui::ArrowButton("##next", ImGuiDir_Right))
        go(next);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && next >= 0)
        ImGui::SetTooltip("Next: %s", room_label(pc_rooms_get(next)).c_str());
    ImGui::SameLine();
    const pc_room_info* pick = chosen >= 0 && chosen < count ? pc_rooms_get(chosen) : nullptr;
    ImGui::BeginDisabled(!pick || !pick->entries);
    if (ImGui::Button("Go"))
        go(chosen);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(pick ? "Load %s" : "Choose a room in the list (double-click goes at once)",
            pick ? room_label(pick).c_str() : "");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##roomsearch", "Search rooms (name, area, zone/room)", filter, sizeof filter);
    if (current >= 0)
        ImGui::Text("Here: %s", room_label(pc_rooms_get(current)).c_str());
    else
        ImGui::TextDisabled("Here: zone %d room %d", z, r);
    int phase = pc_room_warp_phase();
    if (phase == 1 || phase == 2 || phase == 3) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), phase == 1 ? "  waiting for the field..." : "  loading...");
        if (phase == 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel"))
                pc_room_warp_cancel();
        }
    }
    int oz, orr;
    if (pc_room_warp_warped(&oz, &orr)) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.25f, 1), "Warped (from zone %d room %d).%s", oz, orr,
            pc_room_warp_block_saves ? " Saving is refused." : " Saving here records this room!");
        if (ImGui::SmallButton("Return"))
            pc_room_browser_go(oz, orr, 0, why, sizeof why); /* arriving there ends the warped state */
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Back to the room you warped from (through one of its doors)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Stay (allow saving)"))
            pc_room_warp_clear();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Treat this room as reached normally: saving is allowed and records it.\n"
                              "Warps skip events and story flags: a save here may resume somewhere the\n"
                              "story never brought you.");
    }
    bool enemies = pc_room_warp_enemies != 0, scripts = pc_room_warp_scripts != 0,
         block = pc_room_warp_block_saves != 0;
    if (ImGui::Checkbox("Enemies", &enemies))
        pc_room_warp_enemies = enemies;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Warped-to rooms get their enemies (off: an empty room to light)");
    ImGui::SameLine();
    if (ImGui::Checkbox("Room scripts", &scripts))
        pc_room_warp_scripts = scripts;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run the room's entry scripts and triggers (cutscenes, which may set story flags).\n"
                          "Off: none run in a warped-to room (some rooms may then look unfinished).");
    ImGui::SameLine();
    if (ImGui::Checkbox("Refuse saves", &block))
        pc_room_warp_block_saves = block;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("While warped, the memory card refuses writes (a save would resume in the warped room)");
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &follow);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scroll to the current room");
    if (why[0])
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", why);
    else if (pc_room_warp_message()[0])
        ImGui::TextDisabled("%s", pc_room_warp_message());
    ImGui::Separator();
    ImGui::BeginChild("##rooms");
    int last_scene = -2, last_zone = -1;
    static int scrolled_to = -1;
    for (int i = 0; i < count; i++) {
        const pc_room_info* t = pc_rooms_get(i);
        std::string label = room_label(t);
        if (filter[0] && !strcasestr(label.c_str(), filter) && !strcasestr(t->area, filter))
            continue;
        if (t->zone != last_zone && !filter[0]) {
            const char* area = t->area[0] ? t->area : "";
            if (t->scene != last_scene || t->zone != last_zone)
                ImGui::SeparatorText((std::string("Zone ") + std::to_string(t->zone) + (area[0] ? "  " : "") + area).c_str());
            last_zone = t->zone, last_scene = t->scene;
        }
        ImGui::PushID(i);
        bool here = i == current;
        if (!t->entries)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(label.c_str(), here || i == chosen, ImGuiSelectableFlags_AllowDoubleClick)) {
            chosen = i;
            if (ImGui::IsMouseDoubleClicked(0) && t->entries)
                go(i);
        }
        if (!t->entries)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%szone %d room %d (MAP%03d.MPD)\n%s", t->area, t->area[0] ? "\n" : "", t->zone, t->room,
                t->location,
                t->entries ? "Double-click (or Go) to load it" : "No door leads in (reached by an event or a fall): cannot warp");
        if (here && follow && scrolled_to != current) {
            ImGui::SetScrollHereY(0.4f);
            scrolled_to = current;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    (void)base;
}
