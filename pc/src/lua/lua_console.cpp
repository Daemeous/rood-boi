// The Lua console (the dev overlay: Window > Lua console; pc/docs/Extensions.md
// "Developer tools"): every plugin's log, errors with their tracebacks in
// red; an input line evaluated in a sandbox (the console's own, or a loaded
// plugin's) with Up / Down history, through rl::console_submit like RPC lua
// {action "console"}; the plugins (state, memory, faults, slowest call,
// elevated) with on/off and reload; the budgets (tunables(), settings
// plugin_*) and the master switch for elevated permissions. Game side.
#include "imgui.h"
#include "lua_runtime.h"
#include "rl_internal.h"

#include <algorithm>

extern "C" void pc_settings_save(void) __attribute__((weak));

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
