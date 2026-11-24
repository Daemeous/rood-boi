// The editor shell: see editor_shell.h.
#include "editor_shell.h"
#include "editor_commands.h"
#include "imgui.h"
#include "imgui_internal.h" /* DockBuilder, SetWindowHitTestHole, the settings handler */
#include "light_prefabs.h"  /* prefab::Json: the RPC's params */

#include <map>
#include <string>
#include <cstring>
#include <cmath>

extern "C" int pc_gl_inset[4]; /* gl_present.c: margins (left, top, right, bottom) the picture keeps clear */
extern "C" int pc_gl_view[4];  /* the picture's rectangle in the window (top-left origin) */

extern "C" {
float pc_editor_shell_left = 0.22f, pc_editor_shell_right = 0.26f, pc_editor_shell_bottom = 0.30f;
float pc_editor_palette_width = 36.0f;
float pc_editor_palette_rows = 10;
}

namespace rood_shell {

static float& left_fraction = pc_editor_shell_left;
static float& right_fraction = pc_editor_shell_right;
static float& bottom_fraction = pc_editor_shell_bottom;
static float& palette_width_em = pc_editor_palette_width;
static float& palette_rows = pc_editor_palette_rows;

namespace {
/* Bumped when the default layout changes shape: an older ini's layout is rebuilt once. */
const int LAYOUT_VERSION = 1;
const char* const SHELL_INI_TYPE = "ShellLayout";

ImGuiID dockspace_id; /* ImHashStr("rood editor shell") */
ImGuiID node_left, node_right, node_bottom, node_centre;
int ini_version = -1; /* from the ini (-1: none read) */
bool layout_due;      /* reset_layout(): rebuilt in the next frame() */
unsigned reset_count; /* layouts built by reset_layout() this run (dock_tool_window re-docks once each) */
bool built_this_run;
std::map<std::string, unsigned>& tool_generation()
{
    static auto* m = new std::map<std::string, unsigned>();
    return *m;
}

/* What the Game panel reported last frame (RPC editor_shell). */
struct GameState {
    bool shown;   /* the editor showed: the panel was submitted */
    bool visible; /* its contents drawn (not behind another tab) */
    bool docked;
    ImGuiID node;
    float rect[4]; /* the content rectangle (the inset's area) */
} game;

/* ---- the layout's ini section ---- */

void* ini_read_open(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
    return !strcmp(name, "Data") ? (void*)1 : nullptr;
}
void ini_read_line(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    int v;
    unsigned a, b, c, d;
    if (sscanf(line, "Version=%d", &v) == 1)
        ini_version = v;
    else if (sscanf(line, "Nodes=%x,%x,%x,%x", &a, &b, &c, &d) == 4)
        node_left = a, node_right = b, node_bottom = c, node_centre = d;
}
void ini_write_all(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    if (!built_this_run && ini_version < 0)
        return; /* nothing built, nothing read: leave the file without the section */
    buf->appendf("[%s][Data]\n", handler->TypeName);
    buf->appendf("Version=%d\n", LAYOUT_VERSION);
    buf->appendf("Nodes=%08x,%08x,%08x,%08x\n\n", node_left, node_right, node_bottom, node_centre);
}

/* The tool windows the default layout tabs into its bottom node (closed ones keep the node alive in
 * the ini through their settings' DockId). Plugin windows join through dock_tool_window. */
const char* const TOOL_WINDOWS[] = { "Cutscene", "Studio", "Room Browser", "Material review", "Memory cards",
    "Keyboard shortcuts", "Light all fixtures in room", "Lua console" };

void build_layout(ImVec2 size)
{
    ImGui::DockBuilderRemoveNodeDockedWindows(dockspace_id, true);
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);
    ImGuiID centre = dockspace_id;
    float l = left_fraction < 0.05f ? 0.05f : left_fraction > 0.6f ? 0.6f : left_fraction;
    float r = right_fraction < 0.05f ? 0.05f : right_fraction > 0.6f ? 0.6f : right_fraction;
    float b = bottom_fraction < 0.05f ? 0.05f : bottom_fraction > 0.8f ? 0.8f : bottom_fraction;
    if (l + r > 0.8f)
        r = 0.8f - l;
    node_left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, l, nullptr, &centre);
    node_right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, r / (1.0f - l), nullptr, &centre);
    node_bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, b, nullptr, &centre);
    node_centre = centre;
    ImGui::DockBuilderDockWindow("Outliner", node_left);
    ImGui::DockBuilderDockWindow("Details", node_right);
    ImGui::DockBuilderDockWindow("Game", node_centre);
    for (const char* name : TOOL_WINDOWS)
        ImGui::DockBuilderDockWindow(name, node_bottom);
    ImGui::DockBuilderFinish(dockspace_id);
    built_this_run = true;
    ini_version = LAYOUT_VERSION;
    layout_due = false;
    ImGui::MarkIniSettingsDirty();
}

/* ---- the palette ---- */

bool palette_on;
bool palette_focus_due;
char palette_query[128];
int palette_selected;
int palette_frames; /* frames shown (the first keeps focus) */

void palette_run(int index)
{
    const auto& list = rood_commands::list();
    if (index < 0 || index >= (int)list.size())
        return;
    std::string id = list[index].id;
    palette_on = false;
    rood_commands::run(id);
}

/* The dock node a window sits in (0 when floating or unknown), the window itself. */
ImGuiWindow* window_named(const char* name) { return ImGui::FindWindowByName(name); }

} // namespace

void init()
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = false;
    dockspace_id = ImHashStr("rood editor shell");
    ImGuiSettingsHandler h;
    h.TypeName = SHELL_INI_TYPE;
    h.TypeHash = ImHashStr(SHELL_INI_TYPE);
    h.ReadOpenFn = ini_read_open;
    h.ReadLineFn = ini_read_line;
    h.WriteAllFn = ini_write_all;
    ImGui::AddSettingsHandler(&h);
    rood_commands::add({ "shell.reset_layout", "Reset layout", "Window", "", [] { reset_layout(); }, nullptr, nullptr, nullptr });
    rood_commands::add({ "shell.palette", "Command palette", "Window", "Ctrl+Shift+P", [] { palette_open(true); }, nullptr, nullptr, nullptr });
    rood_commands::add({ "window.game", "Game", "Window", "", [] { focus_window("Game"); }, nullptr, nullptr, nullptr });
}

void reset_layout() { layout_due = true; }

void frame(float top, float width, float height)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 pos(vp->Pos.x, vp->Pos.y + top), size(width, height - top);
    if (size.y < 1)
        size.y = 1;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##shell", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(3);
    bool have = ImGui::DockBuilderGetNode(dockspace_id) != nullptr;
    if (layout_due || !have || ini_version != LAYOUT_VERSION) {
        if (layout_due)
            reset_count++;
        build_layout(size);
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void game_panel()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowClass cls;
    /* The picture is drawn under the UI: the Game panel stays where it was docked (undocking it would
     * float a transparent window over the panels) and nothing docks over it as a tab. */
    cls.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingOverMe;
    ImGui::SetNextWindowClass(&cls);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool visible = ImGui::Begin("Game", nullptr,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar();
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImRect rect = w->InnerRect;
    if (!visible && w->DockNode)
        rect = ImRect(w->DockNode->Pos, ImVec2(w->DockNode->Pos.x + w->DockNode->Size.x, w->DockNode->Pos.y + w->DockNode->Size.y));
    game.shown = true;
    game.visible = visible;
    game.docked = w->DockIsActive && w->DockNode;
    game.node = game.docked ? w->DockNode->ID : 0;
    game.rect[0] = rect.Min.x, game.rect[1] = rect.Min.y, game.rect[2] = rect.GetWidth(), game.rect[3] = rect.GetHeight();
    if (rect.GetWidth() >= 64 && rect.GetHeight() >= 48) {
        pc_gl_inset[0] = (int)rect.Min.x;
        pc_gl_inset[1] = (int)rect.Min.y;
        pc_gl_inset[2] = (int)(io.DisplaySize.x - rect.Max.x);
        pc_gl_inset[3] = (int)(io.DisplaySize.y - rect.Max.y);
        for (int i = 0; i < 4; i++)
            if (pc_gl_inset[i] < 0)
                pc_gl_inset[i] = 0;
    }
    if (visible) {
        /* Over the picture no window is hovered (as over the bare picture): a hole in this window
         * and in its dock node's host (the shell, or a floating node's). */
        ImVec2 hole_pos = rect.Min, hole_size = rect.GetSize();
        ImGui::SetWindowHitTestHole(w, hole_pos, hole_size);
        /* The dock space's host is a child window of the shell's ("##shell/DockSpace_..."): both
         * (as DockNodeUpdate does for a passthru central node). */
        if (w->DockIsActive && w->DockNode)
            for (ImGuiWindow* host = w->DockNode->HostWindow; host && host != w; host = host->ParentWindow)
                if (host->HitTestHoleSize.x == 0)
                    ImGui::SetWindowHitTestHole(host, hole_pos, hole_size);
    }
    ImGui::End();
}

void closed()
{
    memset(pc_gl_inset, 0, sizeof pc_gl_inset);
    game.shown = false;
}

void dock_tool_window(const char* name)
{
    if (!node_bottom || !ImGui::DockBuilderGetNode(node_bottom))
        return;
    unsigned& gen = tool_generation()[name];
    if (reset_count && gen != reset_count) {
        gen = reset_count;
        ImGui::SetNextWindowDockID(node_bottom, ImGuiCond_Always);
    } else
        ImGui::SetNextWindowDockID(node_bottom, ImGuiCond_FirstUseEver);
}

void focus_window(const char* name)
{
    if (ImGuiWindow* w = window_named(name)) {
        ImGui::FocusWindow(w);
        if (w->DockNode && w->DockNode->TabBar)
            w->DockNode->TabBar->NextSelectedTabId = w->TabId;
    }
}

/* ---- the palette ---- */

void palette_open(bool open)
{
    if (open && !palette_on) {
        palette_query[0] = 0;
        palette_selected = 0;
        palette_focus_due = true;
        palette_frames = 0;
    }
    palette_on = open;
}
bool palette_shown() { return palette_on; }
void palette_toggle() { palette_open(!palette_on); }

void palette_frame()
{
    if (!palette_on)
        return;
    ImGuiIO& io = ImGui::GetIO();
    float em = ImGui::GetFontSize();
    float width = em * (palette_width_em < 16 ? 16 : palette_width_em);
    /* Over the Game panel's top third, else the window's. */
    float cx = game.shown ? game.rect[0] + game.rect[2] * 0.5f : io.DisplaySize.x * 0.5f;
    float cy = game.shown ? game.rect[1] + game.rect[3] * 0.18f : io.DisplaySize.y * 0.18f;
    ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    if (palette_focus_due)
        ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Command palette##palette", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    palette_frames++;
    if (palette_focus_due) {
        ImGui::SetKeyboardFocusHere();
        palette_focus_due = false;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##query", "Type a command (Enter runs it, Esc closes)", palette_query, sizeof palette_query))
        palette_selected = 0;
    bool field_active = ImGui::IsItemActive();
    std::vector<rood_commands::Match> matches = rood_commands::match(palette_query);
    int rows = palette_rows < 3 ? 3 : (int)palette_rows;
    if (palette_selected >= (int)matches.size())
        palette_selected = matches.empty() ? 0 : (int)matches.size() - 1;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && !matches.empty())
        palette_selected = (palette_selected + 1) % (int)matches.size();
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && !matches.empty())
        palette_selected = (palette_selected + (int)matches.size() - 1) % (int)matches.size();
    bool run_now = (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        && !matches.empty();
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false)
        || (palette_frames > 1 && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !field_active);
    float row_h = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##rows", ImVec2(0, row_h * (float)rows + ImGui::GetStyle().WindowPadding.y), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoNav);
    const auto& list = rood_commands::list();
    int clicked = -1;
    for (int i = 0; i < (int)matches.size(); i++) {
        const rood_commands::Command& c = list[matches[i].index];
        bool enabled = !c.enabled || c.enabled();
        ImGui::PushID(i);
        float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::BeginDisabled(!enabled);
        char label[256];
        snprintf(label, sizeof label, "%s%s", c.checked ? (c.checked() ? "[x] " : "[ ] ") : "", c.title.c_str());
        if (ImGui::Selectable(label, i == palette_selected, ImGuiSelectableFlags_AllowOverlap))
            clicked = i;
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            clicked = i;
        if (i == palette_selected && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            ImGui::SetScrollHereY();
        char tail[160];
        snprintf(tail, sizeof tail, "%s%s%s", c.category.c_str(), c.shortcut.empty() ? "" : "  ", c.shortcut.c_str());
        ImVec2 ts = ImGui::CalcTextSize(tail);
        ImGui::SameLine(right - ts.x, 0);
        ImGui::TextDisabled("%s", tail);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (matches.empty())
        ImGui::TextDisabled("No command matches");
    ImGui::EndChild();
    ImGui::End();
    if (clicked >= 0)
        palette_run(matches[clicked].index);
    else if (run_now)
        palette_run(matches[palette_selected].index);
    else if (close)
        palette_on = false;
}

/* ---- settings ---- */

bool parse_setting(const char* key, const char* rest)
{
    float f;
    if (!strcmp(key, "editor_shell_left") && sscanf(rest, "%f", &f) == 1)
        return left_fraction = f, true;
    if (!strcmp(key, "editor_shell_right") && sscanf(rest, "%f", &f) == 1)
        return right_fraction = f, true;
    if (!strcmp(key, "editor_shell_bottom") && sscanf(rest, "%f", &f) == 1)
        return bottom_fraction = f, true;
    if (!strcmp(key, "editor_palette_width") && sscanf(rest, "%f", &f) == 1)
        return palette_width_em = f, true;
    if (!strcmp(key, "editor_palette_rows") && sscanf(rest, "%f", &f) == 1)
        return palette_rows = (float)(int)f, true;
    return false;
}

void write_settings(FILE* f)
{
    fprintf(f, "editor_shell_left %.3f\n", left_fraction);
    fprintf(f, "editor_shell_right %.3f\n", right_fraction);
    fprintf(f, "editor_shell_bottom %.3f\n", bottom_fraction);
    fprintf(f, "editor_palette_width %.1f\n", palette_width_em);
    fprintf(f, "editor_palette_rows %d\n", (int)palette_rows);
}

void settings_panel()
{
    ImGui::TextDisabled("The default layout (Window > Reset layout applies it); settings keys editor_shell_*");
    ImGui::SliderFloat("Left column", &left_fraction, 0.1f, 0.5f, "%.2f of the width");
    ImGui::SliderFloat("Right column", &right_fraction, 0.1f, 0.5f, "%.2f of the width");
    ImGui::SliderFloat("Bottom panels", &bottom_fraction, 0.1f, 0.7f, "%.2f of the height");
    if (ImGui::Button("Reset layout"))
        reset_layout();
    ImGui::SeparatorText("Command palette (Ctrl+Shift+P)");
    ImGui::SliderFloat("Palette width", &palette_width_em, 16, 80, "%.0f em");
    ImGui::SliderFloat("Palette rows", &palette_rows, 3, 30, "%.0f");
}

} // namespace rood_shell

/* ---- RPC editor_shell ---- */

extern "C" int pc_editor_shell_rpc(const char* params_text, char* out, int size)
{
    using prefab::Json;
    using namespace rood_shell;
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "state";
    std::string reply;
    auto quoted = [](const std::string& s) {
        std::string q = "\"";
        for (char c : s)
            if (c == '"' || c == '\\')
                q += '\\', q += c;
            else if ((unsigned char)c >= 0x20)
                q += c;
        return q + "\"";
    };
    auto window_json = [](const char* name) {
        ImGuiWindow* w = ImGui::FindWindowByName(name);
        if (!w)
            return std::string("{\"exists\":false}");
        bool docked = w->DockIsActive && w->DockNode;
        char buf[256];
        snprintf(buf, sizeof buf,
            "{\"exists\":true,\"docked\":%s,\"node\":\"%08x\",\"active\":%s,\"hidden\":%s,\"rect\":[%.0f,%.0f,%.0f,%.0f]}",
            docked ? "true" : "false", docked ? w->DockNode->ID : (w->DockId ? w->DockId : 0u),
            w->WasActive ? "true" : "false", w->Hidden ? "true" : "false", w->Pos.x, w->Pos.y, w->Size.x, w->Size.y);
        return std::string(buf);
    };
    if (action == "reset") {
        reset_layout();
    } else if (action == "palette") {
        int open = -1;
        if (const Json* o = params.get("open"))
            open = o->type == Json::Bool ? o->boolean : o->type == Json::Number ? o->number != 0 : -1;
        if (open >= 0)
            palette_open(open != 0);
        if (const Json* q = params.get("query"); q && q->type == Json::String)
            snprintf(palette_query, sizeof palette_query, "%s", q->string.c_str());
    } else if (action == "window") {
        const Json* n = params.get("name");
        if (!n || n->type != Json::String) {
            snprintf(out, size, "editor_shell: window needs a name");
            return -1;
        }
        reply = window_json(n->string.c_str());
    } else if (action == "save_ini") { /* tests: the layout written now (else 5 s after a change) */
        if (ImGui::GetIO().IniFilename)
            ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
        reply = std::string("{\"saved\":") + (ImGui::GetIO().IniFilename ? "true" : "false") + "}";
    } else if (action != "state") {
        snprintf(out, size, "editor_shell: action is state, reset, palette or window");
        return -1;
    }
    if (reply.empty()) {
        char buf[1024];
        ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace_id);
        std::vector<rood_commands::Match> m = rood_commands::match(palette_query);
        snprintf(buf, sizeof buf,
            "{\"docking\":%s,\"dockspace\":%s,\"layout_version\":%d,\"reset_count\":%u,"
            "\"nodes\":{\"left\":\"%08x\",\"right\":\"%08x\",\"bottom\":\"%08x\",\"centre\":\"%08x\"},"
            "\"game\":{\"shown\":%s,\"visible\":%s,\"docked\":%s,\"node\":\"%08x\",\"rect\":[%.0f,%.0f,%.0f,%.0f]},"
            "\"inset\":[%d,%d,%d,%d],\"view\":[%d,%d,%d,%d],"
            "\"palette\":{\"open\":%s,\"query\":%s,\"selected\":%d,\"matches\":%d,\"first\":\"%s\"},"
            "\"settings\":{\"left\":%.3f,\"right\":%.3f,\"bottom\":%.3f,\"palette_width\":%.1f,\"palette_rows\":%d}",
            (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) ? "true" : "false", root ? "true" : "false",
            ini_version, reset_count, node_left, node_right, node_bottom, node_centre, game.shown ? "true" : "false",
            game.visible ? "true" : "false", game.docked ? "true" : "false", game.node, game.rect[0], game.rect[1],
            game.rect[2], game.rect[3], pc_gl_inset[0], pc_gl_inset[1], pc_gl_inset[2], pc_gl_inset[3], pc_gl_view[0],
            pc_gl_view[1], pc_gl_view[2], pc_gl_view[3], palette_on ? "true" : "false", quoted(palette_query).c_str(), palette_selected,
            (int)m.size(), m.empty() ? "" : rood_commands::list()[m[0].index].id.c_str(), left_fraction, right_fraction,
            bottom_fraction, palette_width_em, (int)palette_rows);
        reply = buf;
        /* The pointer as Dear ImGui sees it (the Game panel is a hit-test hole: over the picture no window). */
        ImGuiContext& g = *ImGui::GetCurrentContext();
        snprintf(buf, sizeof buf, ",\"mouse\":[%.0f,%.0f],\"hovered\":%s,\"capture_mouse\":%s", g.IO.MousePos.x, g.IO.MousePos.y,
            g.HoveredWindow ? quoted(g.HoveredWindow->Name).c_str() : "null", g.IO.WantCaptureMouse ? "true" : "false");
        reply += buf;
        reply += ",\"windows\":{";
        const char* names[] = { "Game", "Outliner", "Details", "Cutscene", "Room Browser", "Material review", "Memory cards",
            "Keyboard shortcuts", "Lua console", "Library##floating" };
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            reply += std::string(i ? "," : "") + "\"" + names[i] + "\":" + window_json(names[i]);
        reply += "}}";
    }
    if ((int)reply.size() >= size) {
        snprintf(out, size, "editor_shell: the reply is too long");
        return -1;
    }
    memcpy(out, reply.c_str(), reply.size() + 1);
    return 0;
}
