// Modern menus, the native Container screen (container_frame.h): pure state.
// The model container_bridge.c fills (both layers: MENU7's framed run and the
// MENUD port), the pointer's requests (a click is the pad sequence that does
// the same; hover changes nothing), the soft highlight and the JSON (RPC
// ui_menu "container"). Drawing is ui_container_draw.cpp.
#include "ui/container_frame.h"
#include "ui/ui_shell.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" unsigned pc_vsync_count(void) __attribute__((weak));

namespace {

pc_ui_ctr_model model;
const int QUEUE = 32;
pc_ui_ctr_req queue[QUEUE];
int q_head, q_count;
int hover_kind, hover_index;

struct Hit {
    int kind, index;
    float r[4];
};
std::vector<Hit> hits;

unsigned now() { return pc_vsync_count ? pc_vsync_count() : 0; }

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

const char* const PHASES[PC_UI_CTR_PHASES] = {"off", "prompt_check", "busy", "wait", "prompt_warn", "transfer",
    "prompt_cancel", "save", "closing"};
const char* const MODES[PC_UI_CTR_MODES] = {"top", "list", "pick2", "actions", "discard_confirm", "discard_qty", "sort",
    "detail", "commit_confirm"};

bool prompt_phase(int ph)
{
    return ph == PC_UI_CTR_PH_PROMPT_CHECK || ph == PC_UI_CTR_PH_PROMPT_WARN || ph == PC_UI_CTR_PH_PROMPT_CANCEL;
}

// a menu shows (the cursor of what can be clicked in it)
bool menu_mode(int mode)
{
    return mode == PC_UI_CTR_ACTIONS || mode == PC_UI_CTR_SORT || mode == PC_UI_CTR_DISCARD_CONFIRM
        || mode == PC_UI_CTR_COMMIT_CONFIRM;
}

} // namespace

extern "C" pc_ui_ctr_model* pc_ui_ctr_state(void) { return &model; }

// Latched and not in the save list (the Data screen draws that); a stamp up to 120 vsyncs old (the
// overlay loads between MENU7 and MENUD tick neither: the screen stays up over them; the heartbeat ends a
// frame whose menus the game left).
extern "C" int pc_ui_container_visible(void)
{
    if (!model.latched || model.phase == PC_UI_CTR_PH_SAVE)
        return 0;
    return now() - model.stamp <= 120;
}

extern "C" int pc_ui_ctr_post(int kind, int a, int b)
{
    if (q_count >= QUEUE)
        return 0;
    queue[(q_head + q_count) % QUEUE] = {kind, a, b};
    ++q_count;
    return 1;
}

extern "C" int pc_ui_ctr_pop(pc_ui_ctr_req* out)
{
    if (!q_count)
        return 0;
    *out = queue[q_head];
    q_head = (q_head + 1) % QUEUE;
    --q_count;
    return 1;
}

extern "C" void pc_ui_ctr_reset_mailbox(void)
{
    q_head = q_count = 0;
    hover_kind = hover_index = 0;
}

extern "C" void pc_ui_ctr_hover(int* kind, int* index)
{
    *kind = hover_kind;
    *index = hover_index;
}

extern "C" void pc_ui_ctr_hits_clear(void) { hits.clear(); }
extern "C" void pc_ui_ctr_hit_record(int kind, int index, float x0, float y0, float x1, float y1)
{
    if (hits.size() < 256)
        hits.push_back({kind, index, {x0, y0, x1, y1}});
}

namespace {

void goal(int g)
{
    if (pc_ui_ctr_post(PC_UI_CTR_REQ_GOAL, g, 0))
        ++model.requests;
}

void press(unsigned bits) { pc_ui_ctr_post(PC_UI_CTR_REQ_PRESS, (int)bits, 0); }

void click_transfer(int kind, int index)
{
    const pc_ui_ctr_model& m = model;
    switch (kind) {
    case PC_UI_MOUSE_CTR_TOP:
        if (m.mode == PC_UI_CTR_TOP) {
            pc_ui_ctr_post(PC_UI_CTR_REQ_FOCUS, PC_UI_CTR_F_TOP, index);
            press(PC_UI_CONFIRM);
        }
        break;
    case PC_UI_MOUSE_CTR_ROW:
        if ((m.mode == PC_UI_CTR_LIST || m.mode == PC_UI_CTR_PICK2) && m.list_active) {
            pc_ui_ctr_post(PC_UI_CTR_REQ_FOCUS, PC_UI_CTR_F_ROW, index);
            press(PC_UI_CONFIRM);
        } else if (menu_mode(m.mode)) {
            press(PC_UI_BACK); // a click on the list behind a menu: out of it, as Cross
        }
        break;
    case PC_UI_MOUSE_CTR_TAB:
        if (m.mode == PC_UI_CTR_LIST)
            pc_ui_ctr_post(PC_UI_CTR_REQ_TAB, index, 0);
        break;
    case PC_UI_MOUSE_CTR_MENU:
    case PC_UI_MOUSE_CTR_YES:
    case PC_UI_MOUSE_CTR_NO:
        if (menu_mode(m.mode)) {
            int i = kind == PC_UI_MOUSE_CTR_MENU ? index : kind == PC_UI_MOUSE_CTR_YES ? 0 : 1;
            pc_ui_ctr_post(PC_UI_CTR_REQ_FOCUS, PC_UI_CTR_F_MENU, i);
            press(PC_UI_CONFIRM);
        }
        break;
    case PC_UI_MOUSE_CTR_QTY: // +1 / -1 the amount (Up / Down), +2 / -2 the step (Left / Right), 0 OK
        if (m.mode == PC_UI_CTR_DISCARD_QTY)
            press(index == 1 ? PC_UI_UP : index == -1 ? PC_UI_DOWN : index == 2 ? PC_UI_LEFT : index == -2 ? PC_UI_RIGHT
                                                                                                          : PC_UI_CONFIRM);
        break;
    case PC_UI_MOUSE_CTR_BUTTON:
        press((unsigned)index);
        break;
    default:
        break;
    }
}

} // namespace

extern "C" void pc_ui_ctr_mouse(int kind, int index, int click, int right, float wheel)
{
    if (!model.latched)
        return;
    int ph = model.phase;
    if (wheel != wheel) // NaN
        wheel = 0;
    if (!click && !right) {
        if (kind >= PC_UI_MOUSE_CTR_TOP && kind <= PC_UI_MOUSE_CTR_PANEL)
            hover_kind = kind, hover_index = index;
        else if (kind == PC_UI_MOUSE_NONE && wheel == 0)
            hover_kind = hover_index = 0;
        if (wheel != 0 && ph == PC_UI_CTR_PH_TRANSFER) // the wheel: Up / Down a notch at a time
            for (int n = (int)(wheel < 0 ? -wheel : wheel); n > 0 && n < 8; --n)
                press(wheel < 0 ? PC_UI_DOWN : PC_UI_UP);
        return;
    }
    if (ph == PC_UI_CTR_PH_BUSY || ph == PC_UI_CTR_PH_CLOSING || ph == PC_UI_CTR_PH_OFF) {
        ++model.busy_drops; // nothing takes a click now (a double click, a click while reading the card)
        return;
    }
    if (prompt_phase(ph)) {
        if (right || kind == PC_UI_MOUSE_CTR_NO)
            goal(PC_UI_CTR_G_NO);
        else if (kind == PC_UI_MOUSE_CTR_YES)
            goal(PC_UI_CTR_G_YES);
        else if (kind == PC_UI_MOUSE_CTR_BUTTON && (index & PC_UI_BACK))
            goal(PC_UI_CTR_G_NO);
        else if (kind == PC_UI_MOUSE_CTR_BUTTON && (index & PC_UI_CONFIRM))
            goal(model.prompt_yes ? PC_UI_CTR_G_YES : PC_UI_CTR_G_NO);
        else
            ++model.rejected;
        return;
    }
    if (ph == PC_UI_CTR_PH_WAIT) {
        goal(PC_UI_CTR_G_DISMISS); // any click: MENU7 takes any button
        return;
    }
    if (ph != PC_UI_CTR_PH_TRANSFER || !model.session) {
        ++model.busy_drops;
        return;
    }
    if (right) {
        press(PC_UI_BACK);
        return;
    }
    click_transfer(kind, index);
}

extern "C" void pc_ui_container_json(char* out, unsigned size)
{
    const pc_ui_ctr_model& m = model;
    if (!m.latched) {
        std::snprintf(out, size, ",\"container\":null");
        return;
    }
    char buf[768];
    int ph = m.phase < 0 || m.phase >= PC_UI_CTR_PHASES ? 0 : m.phase;
    int mode = m.mode < 0 || m.mode >= PC_UI_CTR_MODES ? 0 : m.mode;
    std::string j = ",\"container\":{";
    std::snprintf(buf, sizeof buf,
        "\"run\":%d,\"visible\":%s,\"phase\":\"%s\",\"m7_state\":%d,\"session\":%d,\"settling\":%d,\"handed_back\":%d,"
        "\"mode\":\"%s\",\"dir\":%d,\"top\":%d,\"tab\":%d,\"rows_n\":%d,\"cursor\":%d,\"sel\":%d,\"page\":%d,"
        "\"list_active\":%d,\"picked\":%d,\"staged\":%d,\"prompt_yes\":%d,\"prompt_ready\":%d,\"location\":%d,"
        "\"gated\":%u,\"presses\":%u,\"busy_drops\":%u,\"rejected\":%u,\"requests\":%u,\"hide_saved\":%d,"
        "\"misc_after\":%d,\"misc_cap\":%d,\"discard\":{\"type\":%d,\"count\":%d,\"max\":%d,\"step\":%d},"
        "\"detail\":{\"category\":%d,\"index\":%d,\"count\":%d,\"slot\":%d},\"ticks\":%u,",
        m.run, pc_ui_container_visible() ? "true" : "false", PHASES[ph], m.m7_state, m.session, m.settling,
        m.handed_back,
        MODES[mode], m.dir, m.top, m.tab, m.rows, m.cursor, m.sel, m.page, m.list_active, m.picked, m.staged,
        m.prompt_yes, m.prompt_ready, m.location, m.gated, m.presses, m.busy_drops, m.rejected, m.requests,
        m.hide_saved, m.misc_after, m.misc_cap, m.discard_type, m.discard_count, m.discard_max, m.discard_step,
        m.detail_category, m.detail_index, m.detail_count, m.detail_slot, m.ticks);
    j += buf;
    j += "\"prompt_text\":\"" + escape(m.prompt_text) + "\",\"wait_text\":\"" + escape(m.wait_text)
        + "\",\"location_name\":\"" + escape(m.location_name) + "\",\"info\":\"" + escape(m.info)
        + "\",\"empty_text\":\"" + escape(m.empty_text) + "\",\"confirm_text\":\"" + escape(m.confirm_text) + "\",";
    auto ints = [&](const char* name, const int* v, int n) {
        j += "\"";
        j += name;
        j += "\":[";
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof buf, "%s%d", i ? "," : "", v[i]);
            j += buf;
        }
        j += "],";
    };
    ints("tab_count", m.tab_count, PC_UI_CTR_TABS);
    ints("tab_cap", m.tab_cap, PC_UI_CTR_TABS);
    ints("bag_count", m.bag_count, 7);
    ints("box_count", m.box_count, 7);
    ints("dest_count", m.dest_count, 7);
    ints("dest_staged", m.dest_staged, 7);
    ints("dest_cap", m.dest_cap, 7);
    j += "\"menu\":{\"count\":" + std::to_string(m.menu.count) + ",\"focus\":" + std::to_string(m.menu.focus)
        + ",\"entries\":[";
    for (int i = 0; i < m.menu.count && i < PC_UI_CTR_SORTS; ++i) {
        std::snprintf(buf, sizeof buf, "%s{\"kind\":%d,\"enabled\":%d,\"label\":\"", i ? "," : "", m.menu.kind[i],
            m.menu.enabled[i]);
        j += buf;
        j += escape(m.menu.label[i]) + "\",\"desc\":\"" + escape(m.menu.desc[i]) + "\"}";
    }
    j += "]},\"card\":{";
    std::snprintf(buf, sizeof buf,
        "\"valid\":%d,\"category\":%d,\"dp\":[%d,%d],\"pp\":[%d,%d],\"str\":%d,\"int\":%d,\"agl\":%d,"
        "\"gem_slots\":%d,\"gems\":%d,\"parent\":\"",
        m.card.valid, m.card.category, m.card.dp, m.card.dp_max, m.card.pp, m.card.pp_max, m.card.str, m.card.intel,
        m.card.agl, m.card.gem_slots, m.card.gems);
    j += buf;
    j += escape(m.card.parent) + "\"},\"rows\":[";
    for (int i = 0; i < m.rows && i < PC_UI_CTR_ROWS; ++i) {
        const pc_ui_ctr_row& r = m.row[i];
        std::snprintf(buf, sizeof buf, "%s{\"c\":%d,\"slot\":%d,\"flags\":%d,\"id\":%d,\"count\":%d,\"name\":\"",
            i ? "," : "", r.category, r.slot, r.flags, r.id, r.count);
        j += buf;
        j += escape(r.name) + "\"}";
    }
    std::snprintf(buf, sizeof buf, "],\"hover\":[%d,%d],\"hits\":[", hover_kind, hover_index);
    j += buf;
    static const char* const KINDS[] = {"top", "row", "tab", "menu", "yes", "no", "qty", "status", "button", "panel"};
    for (size_t i = 0; i < hits.size(); ++i) {
        const Hit& h = hits[i];
        int k = h.kind - PC_UI_MOUSE_CTR_TOP;
        std::snprintf(buf, sizeof buf, "%s{\"k\":\"%s\",\"i\":%d,\"r\":[%g,%g,%g,%g]}", i ? "," : "",
            k >= 0 && k < 10 ? KINDS[k] : "?", h.index, h.r[0], h.r[1], h.r[2], h.r[3]);
        j += buf;
    }
    j += "]}";
    std::snprintf(out, size, "%s", j.c_str());
}
