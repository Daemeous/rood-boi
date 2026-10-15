// Modern menus, the Map frame (map_frame.h): pure state. The model the
// bridge (map_bridge.c) fills from MENU5, the pointer's requests for MENU5
// (a mailbox MENU5 empties at its own input point, 4D8.c _pcApply), the
// rail's cosmetic scroll and the JSON (RPC ui_menu "map"). Drawing is
// ui_map_draw.cpp.
#include "ui/map_frame.h"
#include "ui/ui_shell.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" unsigned pc_vsync_count(void) __attribute__((weak));

namespace {

pc_ui_map_model model;

// Requests: one discrete request at a time (the first wins until MENU5
// takes it), rotate / zoom / pan added up (MENU5 takes one kind a frame).
struct Mailbox {
    bool has = false;
    pc_ui_map_req req = {};
    int run = 0;
    float yaw = 0, pitch = 0, zoom = 0, pan_x = 0, pan_y = 0;
    int cont_run = 0;
    unsigned seq = 0, applied = 0, busy = 0;
} box;

struct Hit {
    int kind, index;
    float r[4];
};
std::vector<Hit> hits; // the last drawn frame's pointer targets (JSON "hits")

float rail_scroll = 0; // the rail's wheel (lines; cosmetic, the list's cursor memory is the bridge's)
int zoom_dir = -1;     // the Zoom prompt: in (-1) until the near clamp, then out until the far one
unsigned controls = 0; // _drawUIControls calls (MENU5's own help; JSON "map_controls")

unsigned now() { return pc_vsync_count ? pc_vsync_count() : 0; }

bool is_discrete(int kind)
{
    return kind != PC_UI_MAP_REQ_ORBIT && kind != PC_UI_MAP_REQ_ZOOM && kind != PC_UI_MAP_REQ_PAN;
}

// Where MENU5 takes requests: processInput (1) of a published, unfinished
// run, or the area list (displayMapMenu 2 with the list open: its own
// requests only).
bool input_ready() { return model.framed && model.published && !model.finished && model.menu_state == 1; }
bool list_ready() { return model.framed && model.published && !model.finished && model.menu_state == 2 && model.list.open; }

bool list_focus() { return model.list.open && model.menu_state == 2; }

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

const char* state_id()
{
    const pc_ui_map_model& m = model;
    if (!m.framed)
        return m.armed ? "loading" : "off";
    if (m.finished || m.exec_state >= 8 || (m.published && m.menu_state == 3))
        return "closing";
    if (!m.published || m.exec_state < 7 || m.menu_state == 0)
        return "loading";
    if (m.menu_state == 2)
        return "list";
    if (m.menu_state == 4 || m.menu_state == 5)
        return "switching";
    return "ready";
}

} // namespace

extern "C" pc_ui_map_model* pc_ui_map_state(void) { return &model; }

extern "C" int pc_ui_map_framed(int run) { return model.framed && model.run == run; }

extern "C" void pc_ui_map_note_controls(void) { ++controls; }

// The pan a request may carry: whole steps of 4 game pixels (MENU5's own
// step), the rest kept for the next one.
static int pan_steps(float v) { return (int)v / 4 * 4; }

extern "C" int pc_ui_map_visible(void)
{
    if (model.framed)
        return model.stamped && now() - model.stamp <= 8;
    return model.armed && now() - model.armed_vsync <= 30;
}

extern "C" void pc_ui_map_post(int kind, int a, int b)
{
    bool ok;
    if (kind == PC_UI_MAP_REQ_AREA || kind == PC_UI_MAP_REQ_PAD || kind == PC_UI_MAP_REQ_TAB)
        ok = input_ready() || list_ready();
    else if (kind == PC_UI_MAP_REQ_ORBIT || kind == PC_UI_MAP_REQ_ZOOM || kind == PC_UI_MAP_REQ_PAN)
        ok = input_ready() && !model.paled;
    else
        ok = input_ready();
    if (kind == PC_UI_MAP_REQ_PAD && !list_ready())
        ok = false; // the pad's own buttons are MENU5's outside the list
    if (!ok || (is_discrete(kind) && box.has)) {
        ++box.busy;
        return;
    }
    ++box.seq;
    if (is_discrete(kind)) {
        box.has = true;
        box.req.kind = kind, box.req.a = a, box.req.b = b;
        box.run = model.run;
        return;
    }
    if (box.cont_run != model.run)
        box.yaw = box.pitch = box.zoom = box.pan_x = box.pan_y = 0;
    box.cont_run = model.run;
    if (kind == PC_UI_MAP_REQ_ORBIT)
        box.yaw += (float)a, box.pitch += (float)b;
    else if (kind == PC_UI_MAP_REQ_ZOOM)
        box.zoom += (float)a;
}

// Pan in fractions of a game pixel (a drag's layout units / 3).
static void post_pan(float dx, float dy)
{
    if (!input_ready() || model.paled) {
        ++box.busy;
        return;
    }
    ++box.seq;
    if (box.cont_run != model.run)
        box.yaw = box.pitch = box.zoom = box.pan_x = box.pan_y = 0;
    box.cont_run = model.run;
    box.pan_x += dx, box.pan_y += dy;
}

extern "C" int pc_ui_map_take(pc_ui_map_req* out, int discrete_ok)
{
    *out = pc_ui_map_req();
    if (discrete_ok) {
        if (!box.has)
            return 0;
        box.has = false;
        if (box.run != model.run) { // a request of an earlier run
            ++box.busy;
            return 0;
        }
        *out = box.req;
        box.applied = box.seq;
        return 1;
    }
    if (box.cont_run != model.run) {
        box.yaw = box.pitch = box.zoom = box.pan_x = box.pan_y = 0;
        return 0;
    }
    int yaw = (int)box.yaw, pitch = (int)box.pitch, zoom = (int)box.zoom;
    int px = pan_steps(box.pan_x), py = pan_steps(box.pan_y);
    if (yaw || pitch) {
        box.yaw -= (float)yaw, box.pitch -= (float)pitch;
        out->kind = PC_UI_MAP_REQ_ORBIT, out->a = yaw, out->b = pitch;
    } else if (zoom) {
        box.zoom -= (float)zoom;
        out->kind = PC_UI_MAP_REQ_ZOOM, out->a = zoom;
    } else if (px || py) {
        box.pan_x -= (float)px, box.pan_y -= (float)py;
        out->kind = PC_UI_MAP_REQ_PAN, out->a = px, out->b = py;
    } else {
        return 0;
    }
    box.applied = box.seq;
    return 1;
}

extern "C" void pc_ui_map_drop(void)
{
    bool any = box.has || (int)box.yaw || (int)box.pitch || (int)box.zoom || pan_steps(box.pan_x) || pan_steps(box.pan_y);
    if (any)
        ++box.busy;
    box.has = false;
    box.yaw = box.pitch = box.zoom = box.pan_x = box.pan_y = 0;
}

extern "C" void pc_ui_map_mouse(int kind, int index, int click, float wheel, float dx, float dy, int button,
    int right)
{
    if (!model.framed && !model.armed)
        return;
    bool list = list_focus();
    if (right) { // back, as on the other tabs: the list's Cross, else Cross
        if (list)
            pc_ui_map_post(PC_UI_MAP_REQ_PAD, PC_UI_BACK, 0);
        else
            pc_ui_map_post(PC_UI_MAP_REQ_QUIT, 1, 0);
        return;
    }
    if (wheel != wheel) // NaN
        wheel = 0;
    switch (kind) {
    case PC_UI_MOUSE_MAP_VIEW:
        if (list)
            return;
        if (button == 1 && (dx != 0 || dy != 0))
            pc_ui_map_post(PC_UI_MAP_REQ_ORBIT, (int)std::lround(dx * 8), (int)std::lround(dy * 8));
        else if (button == 2 && (dx != 0 || dy != 0))
            post_pan(dx / 3.0f, dy / 3.0f);
        if (wheel != 0)
            pc_ui_map_post(PC_UI_MAP_REQ_ZOOM, (int)std::lround(-64 * wheel), 0);
        return;
    case PC_UI_MOUSE_MAP_AREA:
        // the area already shown: nothing to switch to (the list's own row
        // confirms, and reloads it, as the original list does)
        if (click && (list || !model.published || index != model.scene))
            pc_ui_map_post(PC_UI_MAP_REQ_AREA, index, 0);
        break;
    case PC_UI_MOUSE_MAP_ROOM:
        if (click && !list)
            pc_ui_map_post(PC_UI_MAP_REQ_ROOM, index, 0);
        break;
    case PC_UI_MOUSE_MAP_STEP:
        if (click && !list)
            pc_ui_map_post(PC_UI_MAP_REQ_STEP, index > 0 ? 1 : -1, 0);
        break;
    case PC_UI_MOUSE_MAP_TAB:
        if (click)
            pc_ui_map_post(PC_UI_MAP_REQ_TAB, index, 0);
        break;
    case PC_UI_MOUSE_MAP_PROMPT:
        if (!click)
            break;
        switch (index) {
        case PC_UI_MAP_P_ROOM_PREV: pc_ui_map_post(PC_UI_MAP_REQ_STEP, -1, 0); break;
        case PC_UI_MAP_P_ROOM_NEXT: pc_ui_map_post(PC_UI_MAP_REQ_STEP, 1, 0); break;
        case PC_UI_MAP_P_ROTATE: pc_ui_map_post(PC_UI_MAP_REQ_ORBIT, 256, 0); break;
        case PC_UI_MAP_P_ZOOM: // in to the near clamp, then out to the far one (as R2 + Up / Down)
            if (model.distance <= 512)
                zoom_dir = 1;
            else if (model.distance >= 1024)
                zoom_dir = -1;
            pc_ui_map_post(PC_UI_MAP_REQ_ZOOM, 64 * zoom_dir, 0);
            break;
        case PC_UI_MAP_P_RESET: pc_ui_map_post(PC_UI_MAP_REQ_RESET, 0, 0); break;
        case PC_UI_MAP_P_AREAS: pc_ui_map_post(PC_UI_MAP_REQ_LIST, 0, 0); break;
        case PC_UI_MAP_P_HIDE: pc_ui_map_post(PC_UI_MAP_REQ_HIDE, 0, 0); break;
        case PC_UI_MAP_P_BACK: pc_ui_map_post(PC_UI_MAP_REQ_QUIT, 1, 0); break;
        case PC_UI_MAP_P_CLOSE: pc_ui_map_post(PC_UI_MAP_REQ_QUIT, 2, 0); break;
        case PC_UI_MAP_P_LIST_UP: pc_ui_map_post(PC_UI_MAP_REQ_PAD, PC_UI_UP, 0); break;
        case PC_UI_MAP_P_LIST_DOWN: pc_ui_map_post(PC_UI_MAP_REQ_PAD, PC_UI_DOWN, 0); break;
        case PC_UI_MAP_P_LIST_TAB: pc_ui_map_post(PC_UI_MAP_REQ_PAD, PC_UI_TAB_NEXT, 0); break;
        case PC_UI_MAP_P_LIST_SHOW: pc_ui_map_post(PC_UI_MAP_REQ_PAD, PC_UI_CONFIRM, 0); break;
        case PC_UI_MAP_P_LIST_MAP: pc_ui_map_post(PC_UI_MAP_REQ_PAD, PC_UI_BACK, 0); break;
        case PC_UI_MAP_P_LIST_CLOSE: pc_ui_map_post(PC_UI_MAP_REQ_PAD, PC_UI_EXIT, 0); break;
        }
        break;
    default:
        break;
    }
    if (wheel != 0 && (kind == PC_UI_MOUSE_MAP_AREA || kind == PC_UI_MOUSE_MAP_ROOM || kind == PC_UI_MOUSE_MAP_RAIL))
        rail_scroll -= wheel;
}

extern "C" float pc_ui_map_rail_scroll(float lo, float hi)
{
    if (rail_scroll > hi)
        rail_scroll = hi;
    if (rail_scroll < lo)
        rail_scroll = lo;
    return rail_scroll;
}
extern "C" void pc_ui_map_rail_scroll_reset(void) { rail_scroll = 0; }

extern "C" void pc_ui_map_hits_clear(void) { hits.clear(); }
extern "C" void pc_ui_map_hit_record(int kind, int index, float x0, float y0, float x1, float y1)
{
    if (hits.size() < 256)
        hits.push_back({kind, index, {x0, y0, x1, y1}});
}

extern "C" void pc_ui_map_reset_mailbox(void)
{
    box = Mailbox();
    rail_scroll = 0;
    zoom_dir = -1;
}

extern "C" int pc_ui_map_heap_free(void) __attribute__((weak)); // map_bridge.c (battle builds)

extern "C" void pc_ui_map_json(char* out, unsigned size)
{
    const pc_ui_map_model& m = model;
    // the game heap's largest free block (tests: the map screen gives back what it took)
    char heap[80];
    std::snprintf(heap, sizeof heap, ",\"map_controls\":%u", controls);
    if (pc_ui_map_heap_free)
        std::snprintf(heap + std::strlen(heap), sizeof heap - std::strlen(heap), ",\"map_heap_free\":%d",
            pc_ui_map_heap_free());
    if (!m.armed && !m.framed) {
        std::snprintf(out, size, ",\"map\":null%s", heap);
        return;
    }
    char buf[512];
    std::string j = ",\"map\":{";
    std::snprintf(buf, sizeof buf,
        "\"run\":%d,\"framed\":%d,\"visible\":%s,\"state\":\"%s\",\"exec_state\":%d,\"menu_state\":%d,"
        "\"focus\":\"%s\",\"cursor\":[%d,%d],\"list_open\":%s,\"scene\":%d,\"scene_row\":%d,\"current_scene\":%d,"
        "\"room\":%d,\"is_current_scene\":%d,",
        m.run, m.framed, pc_ui_map_visible() ? "true" : "false", state_id(), m.exec_state, m.menu_state,
        list_focus() ? "list" : "map", m.list.cursor - m.list.scroll, m.list.scroll, m.list.open ? "true" : "false",
        m.scene, m.scene_row, m.current_scene, m.room, m.is_current_scene);
    j += buf;
    j += "\"rooms\":[";
    for (int i = 0; i < m.room_count && i < PC_UI_MAP_ROOMS; ++i) {
        std::snprintf(buf, sizeof buf, "%s{\"i\":%d,\"visited\":%s,\"name\":\"", i ? "," : "", i,
            m.rooms[i].visited ? "true" : "false");
        j += buf;
        j += escape(m.rooms[i].name) + "\"}";
    }
    j += "],\"ring\":[";
    for (int i = 0; i < m.ring_count; ++i) {
        std::snprintf(buf, sizeof buf, "%s%d", i ? "," : "", m.ring[i]);
        j += buf;
    }
    std::snprintf(buf, sizeof buf, "],\"rooms_seen\":[%d,%d],\"areas\":[", m.rooms_seen, m.room_count);
    j += buf;
    for (int i = 0; i < m.area_count; ++i) {
        const pc_ui_map_area& a = m.areas[i];
        std::snprintf(buf, sizeof buf, "%s{\"id\":%d,\"current\":%s,\"shown\":%s,\"paling\":%s,\"name\":\"",
            i ? "," : "", a.id, a.current ? "true" : "false", a.shown ? "true" : "false", a.paling ? "true" : "false");
        j += buf;
        j += escape(a.name) + "\"}";
    }
    j += "],\"list\":[";
    for (int i = 0; i < m.list.count; ++i) {
        std::snprintf(buf, sizeof buf, "%s%d", i ? "," : "", m.list.ids[i]);
        j += buf;
    }
    std::snprintf(buf, sizeof buf,
        "],\"list_current_row\":%d,\"completion\":%d,\"yaw\":%d,\"pitch\":%d,\"yaw0\":%d,\"distance\":%d,"
        "\"geom\":[%d,%d],\"paling\":%s,\"is_paling\":%d,\"hide_ui\":%d,\"quit_state\":%d,\"cmd_state\":%d,"
        "\"busy\":%u,\"req_seq\":%u,\"applied_seq\":%u,\"paling_text\":\"",
        m.list.current_row, m.completion, m.yaw, m.pitch, m.yaw0, m.distance, m.gx, m.gy,
        m.paled ? "true" : "false", m.is_paling, m.hide_ui, m.quit_state, m.cmd_state, box.busy, box.seq, box.applied);
    j += buf;
    j += escape(m.paled ? m.paling_text : "") + "\"";
    j += ",\"hits\":[";
    for (size_t i = 0; i < hits.size(); ++i) {
        static const char* const KINDS[] = {"area", "room", "step", "view", "prompt", "tab", "rail"};
        const Hit& h = hits[i];
        int k = h.kind - PC_UI_MOUSE_MAP_AREA;
        std::snprintf(buf, sizeof buf, "%s{\"k\":\"%s\",\"i\":%d,\"r\":[%g,%g,%g,%g]}", i ? "," : "",
            k >= 0 && k < 7 ? KINDS[k] : "?", h.index, h.r[0], h.r[1], h.r[2], h.r[3]);
        j += buf;
    }
    j += "]";
    float col[4];
    pc_ui_map_column(col);
    std::snprintf(buf, sizeof buf, ",\"column\":[%g,%g,%g,%g]}%s", col[0], col[1], col[2], col[3], heap);
    j += buf;
    std::snprintf(out, size, "%s", j.c_str());
}

// The picture's cut-out: the game's 96..312 x 52..216 (MENU5's map, its
// labels and markers; title row 5, the area name's row 10 with its shadow
// (to y 51) and text box 0 stay under the chrome), in layout units
// (x = W/2 + 3 (sx - 160), y = 3 sy).
extern "C" int pc_gte_widescreen __attribute__((weak));
extern "C" void pc_ui_map_column_for(float W, float out[4])
{
    out[0] = W * 0.5f - 192, out[1] = 156, out[2] = 648, out[3] = 492;
}
extern "C" void pc_ui_map_column(float out[4])
{
    pc_ui_map_column_for(&pc_gte_widescreen && pc_gte_widescreen ? 1280.0f : 960.0f, out);
}
