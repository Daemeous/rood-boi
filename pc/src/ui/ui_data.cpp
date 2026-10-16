// Modern menus, the native Data screen (data_frame.h): pure state. The model
// the bridge (data_bridge.c) fills from MENU7, the goal mailbox the planner
// (data_plan.c) walks at MENU7's own input points, the pointer's goals and
// soft highlight, and the JSON (RPC ui_menu "data"). Drawing is
// ui_data_draw.cpp.
#include "ui/data_frame.h"
#include "ui/container_frame.h"
#include "ui/data_plan.h"
#include "ui/ui_shell.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" unsigned pc_vsync_count(void) __attribute__((weak));

namespace {

pc_ui_data_model model;
pc_ui_data_goal box;
unsigned seq; // goals posted (accepted)
int hover_kind, hover_index;

struct Hit {
    int kind, index;
    float r[4];
};
std::vector<Hit> hits; // the last drawn frame's pointer targets (JSON "hits")

unsigned now() { return pc_vsync_count ? pc_vsync_count() : 0; }

int phase_now()
{
    if (!model.framed || model.finished)
        return model.armed ? PC_UI_DATA_PH_BUSY : PC_UI_DATA_PH_OFF;
    return model.published ? model.phase : PC_UI_DATA_PH_BUSY;
}

std::string escape(const char* text)
{
    std::string out;
    for (const char* p = text ? text : ""; *p; ++p) {
        if ((unsigned char)*p < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)*p);
            out += buf;
            continue;
        }
        if (*p == '"' || *p == '\\')
            out += '\\';
        out += *p;
    }
    return out;
}

const char* const PHASES[PC_UI_DATA_PHASES] = {"off", "busy", "ops", "card", "files", "prompt", "wait"};
const char* const GOALS[PC_UI_DATA_GOALS] = {"", "op", "card", "file", "answer", "dismiss", "back", "close", "tab", "move"};
const char* const MSGS[PC_UI_DATA_MSGS] = {"", "select_slot", "checking", "insert_error", "empty_card", "save_failed",
    "load_failed", "select_file", "file_corrupt", "saving", "loading", "saved", "loaded", "removed", "accessing0",
    "accessing1", "container_warn", "format_prompt", "formatting", "format_failed", "format_cancelled",
    "overwrite_prompt", "no_data", "card_full", "container_loss", "other"};
const char* const PROMPTS[] = {"", "overwrite", "format", "container", "container_loss"};

} // namespace

extern "C" pc_ui_data_model* pc_ui_data_state(void) { return &model; }
extern "C" pc_ui_data_goal* pc_ui_data_goal_slot(void) { return &box; }

namespace {
bool data_visible()
{
    if (model.framed)
        return !model.finished && model.stamped && now() - model.stamp <= 8;
    return model.armed && now() - model.armed_vsync <= 30;
}
} // namespace

// The Data frame, or the Container screen (MENU7's other framed run, ui_container.cpp): a native MENU7
// screen shows (the overlay's mouse routing, dev_overlay.cpp, asks this).
extern "C" int pc_ui_data_visible(void) { return data_visible() || pc_ui_container_visible(); }

extern "C" void pc_ui_data_post(int kind, int a)
{
    int phase = phase_now();
    if (phase == PC_UI_DATA_PH_OFF)
        return;
    if (phase == PC_UI_DATA_PH_BUSY) { // MENU7 is not taking input: a double click, a click while saving
        ++model.busy_drops;
        return;
    }
    if (!pc_ui_data_accepts(phase, kind)
        || (model.container && (kind == PC_UI_DATA_GOAL_TAB || kind == PC_UI_DATA_GOAL_CLOSE))) {
        ++model.rejected; // the Container's save list: no tabs, Triangle is MENU7's (invalid there)
        return;
    }
    ++seq;
    if (box.kind == kind && box.a == a && !box.automatic)
        return; // the same goal again (a double click): it goes on where it is
    int cool = box.cool; // a press went out last frame: the next one still waits a frame
    box = pc_ui_data_goal();
    box.kind = kind, box.a = a, box.cool = cool;
}

extern "C" void pc_ui_data_reset_mailbox(void)
{
    box = pc_ui_data_goal();
    hover_kind = hover_index = 0;
}

extern "C" void pc_ui_data_hover(int* kind, int* index)
{
    *kind = hover_kind;
    *index = hover_index;
}

extern "C" void pc_ui_data_mouse(int kind, int index, int click, float wheel, int right)
{
    if (!model.framed && !model.armed)
        return;
    int phase = phase_now();
    if (right) { // back, as Cross
        pc_ui_data_post(PC_UI_DATA_GOAL_BACK, 0);
        return;
    }
    if (wheel != wheel) // NaN
        wheel = 0;
    if (!click) {
        if (kind >= PC_UI_MOUSE_DATA_OP && kind <= PC_UI_MOUSE_DATA_PANEL)
            hover_kind = kind, hover_index = index;
        else if (kind == PC_UI_MOUSE_NONE && wheel == 0)
            hover_kind = hover_index = 0;
        if (wheel != 0 && phase == PC_UI_DATA_PH_FILES && model.file >= 0) {
            // the wheel walks MENU7's own cursor (MOVE: Up / Down, nothing confirmed), so the focused save
            // is always the one Circle and Confirm act on; notches add up while a walk is under way
            int at = box.kind == PC_UI_DATA_GOAL_MOVE ? box.a : model.file;
            at += wheel < 0 ? 1 : -1;
            at = at < 0 ? 0 : at >= PC_UI_DATA_FILES ? PC_UI_DATA_FILES - 1 : at;
            if (at != model.file || box.kind == PC_UI_DATA_GOAL_MOVE)
                pc_ui_data_post(PC_UI_DATA_GOAL_MOVE, at);
        }
        return;
    }
    switch (kind) {
    case PC_UI_MOUSE_DATA_OP: pc_ui_data_post(PC_UI_DATA_GOAL_OP, index); break;
    case PC_UI_MOUSE_DATA_CARD: pc_ui_data_post(PC_UI_DATA_GOAL_CARD, index); break;
    case PC_UI_MOUSE_DATA_FILE: pc_ui_data_post(PC_UI_DATA_GOAL_FILE, index); break;
    case PC_UI_MOUSE_DATA_YES: pc_ui_data_post(PC_UI_DATA_GOAL_ANSWER, 1); break;
    case PC_UI_MOUSE_DATA_NO: pc_ui_data_post(PC_UI_DATA_GOAL_ANSWER, 0); break;
    case PC_UI_MOUSE_DATA_STATUS: pc_ui_data_post(PC_UI_DATA_GOAL_DISMISS, 0); break;
    case PC_UI_MOUSE_DATA_TAB: pc_ui_data_post(PC_UI_DATA_GOAL_TAB, index); break;
    case PC_UI_MOUSE_DATA_BUTTON: // a footer hint: the button's verb
        if (index & PC_UI_BACK)
            pc_ui_data_post(PC_UI_DATA_GOAL_BACK, 0);
        else if (index & PC_UI_EXIT)
            pc_ui_data_post(PC_UI_DATA_GOAL_CLOSE, 0);
        else if (index & (PC_UI_TAB_PREV | PC_UI_TAB_NEXT))
            pc_ui_data_post(PC_UI_DATA_GOAL_TAB, pc_ui_shell_step_tab(index & PC_UI_TAB_NEXT ? 1 : -1));
        else if (index & PC_UI_CONFIRM) {
            if (phase == PC_UI_DATA_PH_OPS)
                pc_ui_data_post(PC_UI_DATA_GOAL_OP, model.ops_row + 1);
            else if (phase == PC_UI_DATA_PH_CARD)
                pc_ui_data_post(PC_UI_DATA_GOAL_CARD, model.card);
            else if (phase == PC_UI_DATA_PH_FILES)
                pc_ui_data_post(PC_UI_DATA_GOAL_FILE, model.file);
            else if (phase == PC_UI_DATA_PH_PROMPT)
                pc_ui_data_post(PC_UI_DATA_GOAL_ANSWER, model.cancelled == 0);
            else
                pc_ui_data_post(PC_UI_DATA_GOAL_DISMISS, 0);
        }
        break;
    default: // outside the panel: nothing
        break;
    }
}

extern "C" void pc_ui_data_hits_clear(void) { hits.clear(); }
extern "C" void pc_ui_data_hit_record(int kind, int index, float x0, float y0, float x1, float y1)
{
    if (hits.size() < 128)
        hits.push_back({kind, index, {x0, y0, x1, y1}});
}

extern "C" void pc_ui_data_json(char* out, unsigned size)
{
    const pc_ui_data_model& m = model;
    if (!m.armed && !m.framed) {
        std::snprintf(out, size, ",\"data\":null");
        return;
    }
    char buf[640];
    int ph = phase_now();
    std::string j = ",\"data\":{";
    std::snprintf(buf, sizeof buf,
        "\"container\":%d,\"run\":%d,\"armed\":%d,\"framed\":%d,\"finished\":%d,\"finish\":%d,\"visible\":%s,\"phase\":\"%s\","
        "\"state\":%d,\"op\":%d,\"save_ok\":%d,\"at_point\":%d,\"in_battle\":%d,\"warp_blocked\":%d,"
        "\"data_not_saved\":%d,\"container_empty\":%d,\"cursor\":[%d,%d],\"card\":%d,\"file\":%d,\"page\":%d,"
        "\"cancelled\":%d,\"ops_open\":%d,\"ops_row\":%d,\"msg\":\"%s\",\"msg_off\":%d,\"prompt\":\"%s\",",
        m.container, m.run, m.armed, m.framed, m.finished, m.finish_code, data_visible() ? "true" : "false",
        PHASES[ph < 0 || ph >= PC_UI_DATA_PHASES ? 0 : ph], m.state, m.op, m.save_ok, m.at_point, m.in_battle,
        m.warp_blocked, m.data_not_saved, m.container_empty, m.cursor_xy & 0xFFFF, (m.cursor_xy >> 16) & 0xFFFF,
        m.card, m.file, m.page, m.cancelled, m.ops_open, m.ops_row,
        MSGS[m.msg < 0 || m.msg >= PC_UI_DATA_MSGS ? 0 : m.msg], m.msg_off, PROMPTS[m.prompt < 0 || m.prompt > 4 ? 0 : m.prompt]);
    j += buf;
    j += "\"msg_text\":\"" + escape(m.msg_text) + "\",";
    std::snprintf(buf, sizeof buf,
        "\"progress\":{\"counter\":%d,\"x\":%d,\"target\":%d},\"fade\":%d,\"files_valid\":%d,\"current\":%d,"
        "\"newest\":%d,\"card_sel\":%d,\"cards\":[%d,%d],\"play\":\"%d:%02d:%02d\",\"location\":%d,",
        m.progress_counter, m.progress_x, m.progress_target, m.fade, m.files_valid, m.current, m.newest, m.card_sel,
        m.card_status[0], m.card_status[1], m.play_h, m.play_m, m.play_s, m.location);
    j += buf;
    j += "\"location_name\":\"" + escape(m.location_name) + "\",";
    std::snprintf(buf, sizeof buf,
        "\"journey\":{\"hp\":[%d,%d],\"mp\":[%d,%d],\"map\":%d,\"saves\":%d},\"files\":[", m.hp, m.hp_max, m.mp,
        m.mp_max, m.map, m.saves);
    j += buf;
    for (int i = 0; m.files_valid && i < PC_UI_DATA_FILES; ++i) {
        const pc_ui_data_file& f = m.files[i];
        std::snprintf(buf, sizeof buf,
            "%s{\"i\":%d,\"state\":%d,\"generation\":%u,\"time\":\"%d:%02d:%02d\",\"saves\":%d,\"hp\":[%d,%d],"
            "\"mp\":[%d,%d],\"location\":%d,\"clear\":%d,\"map\":%d,\"current\":%s,\"newest\":%s,\"name\":\"",
            i ? "," : "", i, f.state, f.generation, f.h, f.m, f.s, f.saves, f.hp, f.hp_max, f.mp, f.mp_max,
            f.location, f.clear, f.map, f.current ? "true" : "false", f.newest ? "true" : "false");
        j += buf;
        j += escape(f.name) + "\"}";
    }
    std::snprintf(buf, sizeof buf,
        "],\"goal\":{\"kind\":\"%s\",\"a\":%d,\"auto\":%d},\"seq\":%u,\"presses\":%u,\"busy_drops\":%u,"
        "\"rejected\":%u,\"expired\":%u,\"menu7_gated\":%u,\"settling\":%d,\"hover\":[%d,%d],\"hits\":[",
        GOALS[box.kind < 0 || box.kind >= PC_UI_DATA_GOALS ? 0 : box.kind], box.a, box.automatic, seq, m.presses,
        m.busy_drops, m.rejected, m.expired, m.gated, m.settling, hover_kind, hover_index);
    j += buf;
    for (size_t i = 0; i < hits.size(); ++i) {
        static const char* const KINDS[] = {"op", "card", "file", "yes", "no", "status", "tab", "button", "panel"};
        const Hit& h = hits[i];
        int k = h.kind - PC_UI_MOUSE_DATA_OP;
        std::snprintf(buf, sizeof buf, "%s{\"k\":\"%s\",\"i\":%d,\"r\":[%g,%g,%g,%g]}", i ? "," : "",
            k >= 0 && k < 9 ? KINDS[k] : "?", h.index, h.r[0], h.r[1], h.r[2], h.r[3]);
        j += buf;
    }
    j += "]}";
    std::snprintf(out, size, "%s", j.c_str());
}
