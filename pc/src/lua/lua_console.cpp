// The Lua console (the dev overlay: Window > Lua console; pc/docs/Extensions.md
// "Developer tools"): every plugin's log, errors with their tracebacks in
// red; an input line evaluated in a sandbox (the console's own, or a loaded
// plugin's) with Up / Down history, through rl::console_submit like RPC lua
// {action "console"}; the plugins (state, memory, faults, slowest call,
// elevated) with on/off and reload; the budgets (tunables(), settings
// plugin_*) and the master switch for elevated permissions; the HUD bench
// (dev mode: ROOD_RPC set or ROOD_DEV=1): summon each kind (rood.dev.show's
// defaults), clear, the classic flavour's override, and the gallery
// (rood.dev.bench: save the moment, load an entry). Game side.
#include "imgui.h"
#include "lua_bench.h"
#include "lua_runtime.h"
#include "rl_internal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

extern "C" void pc_settings_save(void) __attribute__((weak));
extern "C" int pc_hud_classic_override;   // hud_elements.cpp (settings hud_classic_override)
extern "C" int pc_ui_menus __attribute__((weak)); // project_settings.cpp

namespace {
bool console_open;
char input[2048];
std::string target = "@console";
unsigned long long cleared;
int history_pos = -1;
bool scroll_down;

int history(ImGuiInputTextCallbackData* data)
{
    const std::vector<std::string>& h = rl::console_history();
    if (h.empty())
        return 0;
    if (data->EventKey == ImGuiKey_UpArrow)
        history_pos = history_pos < 0 ? (int)h.size() - 1 : std::max(0, history_pos - 1);
    else if (data->EventKey == ImGuiKey_DownArrow)
        history_pos = history_pos < 0 || history_pos + 1 >= (int)h.size() ? -1 : history_pos + 1;
    data->DeleteChars(0, data->BufTextLen);
    if (history_pos >= 0)
        data->InsertChars(0, h[(size_t)history_pos].c_str());
    return 0;
}

void plugins_table()
{
    if (!ImGui::BeginTable("##lua plugins", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Plugin");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("Memory");
    ImGui::TableSetupColumn("Faults");
    ImGui::TableSetupColumn("Slowest");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (auto& up : rl::all()) {
        rl::Plugin& p = *up;
        if (p.console)
            continue;
        ImGui::PushID(p.id().c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(p.id().c_str());
        if (!p.granted.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "[elevated]");
            if (ImGui::IsItemHovered()) {
                std::string granted;
                for (const std::string& g : p.granted)
                    granted += (granted.empty() ? "" : ", ") + g;
                ImGui::SetTooltip("Granted: %s", granted.c_str());
            }
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(rl::state_name(p.state));
        if (ImGui::IsItemHovered() && !p.status.empty())
            ImGui::SetTooltip("%s", p.status.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.0f / %.0f KB", p.mem.used / 1024.0, p.mem.cap / 1024.0);
        ImGui::TableNextColumn();
        ImGui::Text("%u (%u budget)", (unsigned)p.stats.faults, (unsigned)p.stats.kills);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f ms", p.stats.max_ms);
        ImGui::TableNextColumn();
        bool enabled = rl::plugin_enabled(&p);
        if (ImGui::Checkbox("##on", &enabled))
            rl::set_enabled(p.id(), enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload"))
            p.reload_requested = true;
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void budgets()
{
    float width = ImGui::GetFontSize() * 9;
    for (const rl::Tunable& t : rl::tunables()) {
        double value = rl::get_tunable(t);
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputDouble(t.label, &value, 0, 0, "%.10g", ImGuiInputTextFlags_EnterReturnsTrue))
            rl::set_tunable(t, value);
    }
    bool elevated = rl::config.elevated;
    if (ImGui::Checkbox("Allow plugins to request elevated permissions", &elevated)) {
        rl::config.elevated = elevated;
        rl::config_changed();
    }
    if (ImGui::Button("Save settings") && pc_settings_save)
        pc_settings_save();
}
bool dev_mode()
{
    const char* rpc = std::getenv("ROOD_RPC");
    const char* dev = std::getenv("ROOD_DEV");
    return (rpc && *rpc) || (dev && !std::strcmp(dev, "1"));
}

// The bench: summon, clear, the classic override, the gallery (read again on demand: entries are files).
char bench_name[49] = "moment";
std::vector<rl::BenchEntry> entries;
bool entries_read;
std::string bench_message;
void bench()
{
    ImGui::TextUnformatted("Summon (held until cleared):");
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        ImGui::SameLine();
        if (ImGui::SmallButton(hud::kind_name(k))) {
            std::string why;
            if (!hud::show(k, hud::ShowArgs(), &why))
                bench_message = why;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        hud::clear(0);
    bool classic = pc_hud_classic_override != 0;
    if (ImGui::Checkbox("Claims in the classic look (hud_classic_override)", &classic))
        pc_hud_classic_override = classic;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("With the modern UI off, claims draw in the classic flavour: the game's own printers,\n"
                          "sprites and windows, at the game's hooks. Off: the game draws the HUD (typography only).");
    if (&pc_ui_menus) {
        ImGui::SameLine();
        ImGui::TextDisabled(pc_ui_menus ? "(the modern look now)" : "(the classic look now)");
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    ImGui::InputText("##bench name", bench_name, sizeof bench_name);
    ImGui::SameLine();
    if (ImGui::Button("Save this moment")) {
        std::string path, why;
        bench_message = rl::bench_save(bench_name, &path, &why) ? "saving " + path : why;
        entries_read = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh") || !entries_read) {
        entries = rl::bench_list();
        entries_read = true;
    }
    if (!bench_message.empty())
        ImGui::TextWrapped("%s", bench_message.c_str());
    if (entries.empty()) {
        ImGui::TextDisabled("No saved moments in %s.", rl::bench_dir().c_str());
        return;
    }
    if (!ImGui::BeginTable("##bench", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Moment");
    ImGui::TableSetupColumn("Room");
    ImGui::TableSetupColumn("Held");
    ImGui::TableSetupColumn("Saved");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (const rl::BenchEntry& e : entries) {
        ImGui::PushID(e.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e.name.c_str());
        if (e.name == rl::bench_current()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(this run)");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%s (%d/%d)", e.room_name.c_str(), e.zone, e.room);
        ImGui::TableNextColumn();
        std::string held;
        for (const hud::HeldMoment& m : e.held)
            held += (held.empty() ? "" : ", ") + std::string(hud::kind_name(m.kind));
        ImGui::TextUnformatted(held.empty() ? "-" : held.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e.saved.c_str());
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!e.valid);
        if (ImGui::SmallButton("Load")) {
            std::string why;
            bench_message = rl::bench_load(e.name, &why) ? "restarting into " + e.name : why;
        }
        ImGui::EndDisabled();
        if (!e.valid && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", e.why.c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}
} // namespace

extern "C" void pc_lua_console_menu_item(void) { ImGui::MenuItem("Lua console", nullptr, &console_open); }

extern "C" void pc_lua_console_window(void)
{
    if (!console_open)
        return;
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 46, em * 28), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lua console", &console_open)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(em * 14);
    if (ImGui::BeginCombo("##lua target", target.c_str())) {
        if (ImGui::Selectable("@console", target == "@console"))
            target = "@console";
        for (auto& p : rl::all())
            if (!p->console && p->state == rl::PLUGIN_LOADED && ImGui::Selectable(p->id().c_str(), target == p->id()))
                target = p->id();
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        cleared = rl::log_last_seq();
    ImGui::SameLine();
    if (ImGui::Button("Reload all"))
        rl::request_reload_all();
    if (ImGui::CollapsingHeader("Plugins"))
        plugins_table();
    if (ImGui::CollapsingHeader("Budgets"))
        budgets();
    if (dev_mode() && ImGui::CollapsingHeader("Bench"))
        bench();
    float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##lua log", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    for (const rl::LogLine& line : rl::log_lines()) {
        if (line.seq <= cleared)
            continue;
        ImVec4 colour = line.level == rl::LOG_ERROR ? ImVec4(1, 0.45f, 0.4f, 1)
            : line.level == rl::LOG_WARN            ? ImVec4(1, 0.8f, 0.3f, 1)
                                                    : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextWrapped("%s%s%s", line.plugin.c_str(), line.plugin.empty() ? "" : ": ", line.text.c_str());
        ImGui::PopStyleColor();
    }
    if (scroll_down || ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    scroll_down = false;
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##lua input", input, sizeof input,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, history)) {
        if (input[0])
            rl::console_submit(target, input, nullptr, nullptr);
        input[0] = 0;
        history_pos = -1;
        scroll_down = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}
