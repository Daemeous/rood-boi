// Copy, paste and duplicate in the editor (editor_scene.h): the selection
// as lighting entries (lighting_layers.h, the room files' own format: text
// that can be kept, sent or pasted into a room file), and entries back as
// new objects with new ids, selected, one undo step.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_scene.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lighting_layers.h"
#include "lights_internal.h"

using rood_selection::Ref;
using namespace rood_lights;

namespace {
std::string held; /* the editor's own clipboard (the system's may hold anything) */
int copied_count;
} // namespace

std::string rood_scene::copy_text()
{
    std::string text = "rood-lighting " + std::to_string(lighting::FORMAT_VERSION) + "\n# copied in the editor\n";
    copied_count = 0;
    for (const Ref& r : rood_selection::items()) {
        int i = index_of(r);
        if (i < 0 || r.kind == rood_selection::KIND_GLOW)
            continue;
        lighting::Entry e;
        e.item = r.kind == rood_selection::KIND_LIGHT ? lighting::item_from_light(lights[i])
            : r.kind == rood_selection::KIND_ACTOR    ? lighting::item_from_actor(actors[i])
                                                       : rood_atmo::volume_item(i);
        e.id = e.item.id();
        text += lighting::format_entry(e) + "\n";
        ++copied_count;
    }
    return copied_count ? text : std::string();
}
int rood_scene::copy_selection()
{
    std::string text = copy_text();
    if (text.empty()) {
        snprintf(status, sizeof status, "Copy: select lights, light actors or fog volumes (glows come from the game)");
        return 0;
    }
    held = text;
    if (ImGui::GetCurrentContext())
        ImGui::SetClipboardText(text.c_str());
    snprintf(status, sizeof status, "Copied %d object%s (Ctrl+V pastes them where they were)", copied_count,
        copied_count == 1 ? "" : "s");
    return copied_count;
}
const std::string& rood_scene::clipboard() { return held; }
int rood_scene::paste_text(const std::string& text, const float offset[3], const char* verb)
{
    lighting::File file;
    if (!lighting::parse_text(text, file) || file.version == 0) {
        snprintf(status, sizeof status, "%s: no lighting items in it", verb);
        return 0;
    }
    track_room();
    std::vector<Ref> added;
    for (const lighting::Entry& e : file.entries) {
        if (e.verb != lighting::DEFINE)
            continue;
        if (e.item.kind == "light") {
            Light l;
            if (!lighting::light_from_item(e.item, l))
                continue;
            l.id.clear(); /* a new one (ensure_ids) */
            for (int k = 0; k < 3; k++)
                l.position[k] += offset[k];
            lights.push_back(l);
            added.push_back(light_ref((int)lights.size() - 1));
        } else if (e.item.kind == "actor") {
            prefab::Actor a;
            if (!lighting::actor_from_item(e.item, a))
                continue;
            a.id.clear();
            a.glow_linked = false; /* one actor drives a glow */
            a.glow_key.clear();
            a.seed = prefab::hash32(a.seed + (uint32_t)actors.size() * 977u) % 1000000u + 1;
            for (int k = 0; k < 3; k++)
                a.position[k] += offset[k];
            actors.push_back(a);
            added.push_back(actor_ref((int)actors.size() - 1));
        } else if (e.item.kind == "fog") {
            int i = rood_atmo::add_volume_item(e.item, offset);
            if (i >= 0)
                added.push_back(volume_ref(i));
        }
    }
    if (added.empty()) {
        snprintf(status, sizeof status, "%s: no lights, light actors or fog volumes in it", verb);
        return 0;
    }
    rood_selection::set_all(added, rood_selection::FROM_CODE);
    publish();
    dirty = true;
    int n = (int)added.size();
    std::string name = std::string(verb) + " " + (n == 1 ? label(added[0]) : std::to_string(n) + " objects");
    pc_undo_hint(name.c_str());
    snprintf(status, sizeof status, "%s (unsaved; Ctrl+Z undoes)", name.c_str());
    return n;
}
int rood_scene::paste_clipboard()
{
    const char* system = ImGui::GetCurrentContext() ? ImGui::GetClipboardText() : nullptr;
    std::string text = system && !strncmp(system, "rood-lighting ", 14) ? std::string(system) : held;
    const float here[3] = { 0, 0, 0 };
    return paste_text(text, here, "Paste");
}
int rood_scene::duplicate_selection()
{
    std::string text = copy_text();
    const float along_x[3] = { 64, 0, 0 };
    return text.empty() ? 0 : paste_text(text, along_x, "Duplicate");
}
