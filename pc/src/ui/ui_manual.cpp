// Modern menus: the Quick manual's state and navigation (ui_manual.h). Pure
// state: the game's side is manual_bridge.c (through the events), the
// drawing ui_manual_draw.cpp.
#include "ui/ui_manual.h"
#include "ui/manual_parse.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const int TOPICS = PC_UI_MANUAL_TOPICS;
const int OUTBOX = 64;
enum { SFX_LEAVE, SFX_SELECT, SFX_CHANGE };

struct Manual {
    int mode = PC_UI_MANUAL_OFF;
    int topic = 0;
    int row = 0, page = 0; // the list's window (cursorMemories[4], [5])
    int line[TOPICS] = {};
    // the bridge's fill
    int cursor_memory = 1, current = 0;
    unsigned short game_scroll[TOPICS] = {};
    int line_count[TOPICS] = {}, loaded[TOPICS] = {};
    int square_held = 0, start = 0;
    bool original = false; // Square: ORIGINAL follows the leave's events
    int sfx[3] = {};       // leave, select, change: what the manual asked the bridge to play
    pc_ui_manual_event out[OUTBOX] = {};
    int head = 0, count = 0;
    // published for the JSON (the bridge, the drawing)
    std::string label, desc, page_lines = "[]", hits = "[]";
    int lines = 0, sprites = 0, rules = 0, hidden = 0, unknown = 0, overflow = 0, art = 0, art_prompts = 0;
    float k = 0, size = 0;
} m;

void push(int kind, int a = 0, int b = 0, int c = 0)
{
    // the scroll of every tick: one per topic is enough (the last wins)
    if (kind == PC_UI_MEV_SCROLL && m.count) {
        pc_ui_manual_event& last = m.out[(m.head + m.count - 1) % OUTBOX];
        if (last.kind == PC_UI_MEV_SCROLL && last.a == a) {
            last.b = b;
            return;
        }
    }
    if (m.count == OUTBOX)
        return; // the bridge drains every tick
    m.out[(m.head + m.count++) % OUTBOX] = {kind, a, b, c};
}
void sound(int which)
{
    ++m.sfx[which];
    push(PC_UI_MEV_SOUND, which);
}
int clamp_topic(int t) { return t < 0 ? 0 : t >= TOPICS ? TOPICS - 1 : t; }
void store_list() { push(PC_UI_MEV_LIST_STORE, m.row, m.page, m.row + m.page); }
// _topMenu(1): the list (re)shown on the topic (_setMenuRows writes the window)
void seed()
{
    pc_ui_manual_seed(m.topic, &m.row, &m.page);
    store_list();
}
void read_topic()
{
    m.topic = m.row + m.page;
    store_list(); // the list's Circle stores its window; MENUE then sets currentManual
    sound(SFX_SELECT);
    m.mode = PC_UI_MANUAL_READ;
}
// The page's step of one tick (494.c showSubMenu): Start, then Square + Up / Down (pages) or Up / Down
// (lines); Left / Right page as Square + Up / Down do. The scroll is written every tick.
void step(const pc_ui_input* in)
{
    int& line = m.line[m.topic];
    int lc = m.line_count[m.topic];
    unsigned rep = in->repeat, press = in->pressed;
    int changed = 0;
    if (m.start)
        changed = pc_ui_manual_step(&line, lc, 0, 1, 0, 0, 0, 0);
    else if (rep & (PC_UI_UP | PC_UI_DOWN))
        changed = pc_ui_manual_step(&line, lc, m.square_held, 0, rep & PC_UI_UP, press & PC_UI_UP, rep & PC_UI_DOWN,
            press & PC_UI_DOWN);
    else if (rep & (PC_UI_LEFT | PC_UI_RIGHT))
        changed = pc_ui_manual_step(&line, lc, 1, 0, rep & PC_UI_LEFT, press & PC_UI_LEFT, rep & PC_UI_RIGHT,
            press & PC_UI_RIGHT);
    if (changed)
        sound(SFX_CHANGE);
    push(PC_UI_MEV_SCROLL, m.topic, line);
}
// Reading -> topics (Circle: select, Cross: leave; the pointer's topic switch: none).
void to_list(int sfx)
{
    if (sfx >= 0)
        sound(sfx);
    push(PC_UI_MEV_SCROLL, m.topic, m.line[m.topic]);
    m.mode = PC_UI_MANUAL_LIST;
    seed();
}
// The list's cursor onto topic t as Up / Down repeats would take it (the window keeps its margin).
bool walk_to(int t)
{
    bool moved = false;
    for (int guard = 0; m.row + m.page != t && guard < 32; ++guard) {
        int up = t < m.row + m.page;
        moved |= pc_ui_manual_list_step(&m.row, &m.page, up, 0, !up, 0) != 0;
    }
    m.topic = m.row + m.page;
    return moved;
}
bool content_ready() { return m.loaded[m.topic] == 1; }
void set_line(int line)
{
    int lc = m.line_count[m.topic];
    line = line < 0 ? 0 : line > lc ? lc : line;
    if (line != m.line[m.topic]) {
        m.line[m.topic] = line;
        sound(SFX_CHANGE);
    }
    push(PC_UI_MEV_SCROLL, m.topic, m.line[m.topic]);
}

} // namespace

extern "C" {

void pc_ui_manual_fill(int cursor_memory, int current_manual, const unsigned short scroll[14],
    const int line_count[14], const int loaded[14])
{
    m.cursor_memory = cursor_memory;
    m.current = current_manual;
    for (int i = 0; i < TOPICS; ++i) {
        m.game_scroll[i] = scroll[i];
        m.line_count[i] = line_count[i];
        m.loaded[i] = loaded[i];
        if (m.mode == PC_UI_MANUAL_OFF)
            m.line[i] = scroll[i];
    }
}
void pc_ui_manual_buttons(int square_held, int start_pressed)
{
    m.square_held = square_held != 0;
    m.start = start_pressed != 0;
}
int pc_ui_manual_take(pc_ui_manual_event* e)
{
    if (!m.count)
        return 0;
    *e = m.out[m.head];
    m.head = (m.head + 1) % OUTBOX;
    --m.count;
    return 1;
}
void pc_ui_manual_forget(void)
{
    m.mode = PC_UI_MANUAL_OFF;
    m.count = m.head = 0;
    m.original = false;
    m.square_held = m.start = 0;
}
int pc_ui_manual_focus_topic(void) { return m.mode == PC_UI_MANUAL_OFF ? -1 : m.topic; }
void pc_ui_manual_set_text(const char* label, const char* desc, int lines, const char* page_lines_json,
    int sprites, int rules, int hidden, int unknown)
{
    m.label = label ? label : "";
    m.desc = desc ? desc : "";
    m.lines = lines;
    m.page_lines = page_lines_json && *page_lines_json ? page_lines_json : "[]";
    m.sprites = sprites, m.rules = rules, m.hidden = hidden, m.unknown = unknown;
}
void pc_ui_manual_set_draw(float k, float size, int overflow, int art, int art_prompts, const char* hits_json)
{
    m.k = k, m.size = size, m.overflow = overflow;
    m.art = art, m.art_prompts = art_prompts;
    m.hits = hits_json && *hits_json ? hits_json : "[]";
}
void pc_ui_manual_view_get(pc_ui_manual_view* v)
{
    v->mode = m.mode;
    v->topic = m.topic;
    v->row = m.row, v->page = m.page;
    v->line = m.line[m.topic];
    v->line_count = m.line_count[m.topic];
    v->loaded = m.loaded[m.topic];
    v->square_held = m.square_held;
}
static const pc_ui_manual_source* source;
void pc_ui_manual_set_source(const pc_ui_manual_source* s) { source = s; }
const pc_ui_manual_source* pc_ui_manual_source_get(void) { return source; }
int pc_ui_manual_line_of(int t) { return t >= 0 && t < TOPICS ? m.line[t] : 0; }

int pc_ui_manual_mode(void) { return m.mode; }

void pc_ui_manual_enter(void)
{
    // MENUE's _initMenuState: no cursor memory, the display state starts over
    int zero = m.cursor_memory == 0;
    for (int i = 0; i < TOPICS; ++i)
        m.line[i] = zero ? 0 : m.game_scroll[i];
    m.topic = zero ? 0 : clamp_topic(m.current);
    m.mode = PC_UI_MANUAL_LIST;
    m.original = false;
    push(PC_UI_MEV_OPEN, zero);
    ++m.sfx[SFX_SELECT]; // OPEN plays it (as deliver did for the original)
    seed();
}

void pc_ui_manual_back(const pc_ui_input* in)
{
    // Circle wins a Circle + Cross chord, as in the original: the page leaves with the select sound when
    // Circle was pressed (494.c showSubMenu), the list opens the topic (58EC.c case 17 checks Circle first)
    bool circle = (in->pressed & PC_UI_CONFIRM) != 0;
    if (m.mode == PC_UI_MANUAL_READ) {
        step(in); // the exit frame's step runs first (494.c)
        to_list(circle ? SFX_SELECT : SFX_LEAVE);
        return;
    }
    if (m.mode == PC_UI_MANUAL_LIST && circle) {
        read_topic();
        return;
    }
    if (m.mode == PC_UI_MANUAL_LIST) {
        store_list();
        sound(SFX_LEAVE);
        push(PC_UI_MEV_CLOSE);
        m.mode = PC_UI_MANUAL_OFF;
    }
}

void pc_ui_manual_leave(void)
{
    if (m.mode == PC_UI_MANUAL_OFF)
        return;
    if (m.mode == PC_UI_MANUAL_LIST)
        store_list();
    else
        push(PC_UI_MEV_SCROLL, m.topic, m.line[m.topic]);
    push(PC_UI_MEV_CLOSE);
    m.mode = PC_UI_MANUAL_OFF;
    if (m.original)
        push(PC_UI_MEV_ORIGINAL, m.topic);
    m.original = false;
}

void pc_ui_manual_exit_step(const pc_ui_input* in)
{
    if (m.mode == PC_UI_MANUAL_READ)
        step(in);
}

int pc_ui_manual_tick(const pc_ui_input* in)
{
    if (m.mode == PC_UI_MANUAL_LIST) {
        // the list's buttons first (58EC.c case 17: they return before the cursor moves)
        if (in->pressed & (PC_UI_CONFIRM | PC_UI_RIGHT)) {
            read_topic();
            return 0;
        }
        if (in->pressed & PC_UI_ALT) { // the original screen, on this topic
            m.original = true;
            return 10;
        }
        if (pc_ui_manual_list_step(&m.row, &m.page, (in->repeat & PC_UI_UP) != 0, (in->pressed & PC_UI_UP) != 0,
                (in->repeat & PC_UI_DOWN) != 0, (in->pressed & PC_UI_DOWN) != 0)) {
            m.topic = m.row + m.page;
            sound(SFX_CHANGE);
        }
        return 0;
    }
    if (m.mode == PC_UI_MANUAL_READ) {
        step(in);
        if (in->pressed & PC_UI_CONFIRM)
            to_list(SFX_SELECT);
    }
    return 0;
}

void pc_ui_manual_original(void)
{
    push(PC_UI_MEV_ORIGINAL, clamp_topic(m.current));
}

int pc_ui_manual_mouse_focus(int kind, int index)
{
    if (kind != PC_UI_MOUSE_MANUAL_TOPIC || m.mode != PC_UI_MANUAL_LIST || index < 0 || index >= TOPICS)
        return 0;
    if (walk_to(index))
        sound(SFX_CHANGE);
    return 1;
}

int pc_ui_manual_mouse_click(int kind, int index)
{
    switch (kind) {
    case PC_UI_MOUSE_MANUAL_TOPIC:
        if (index < 0 || index >= TOPICS || m.mode == PC_UI_MANUAL_OFF)
            return 0;
        if (m.mode == PC_UI_MANUAL_READ) {
            if (index == m.topic)
                return 1;
            to_list(-1); // back to the topics (the select below is the click's one sound)
        }
        walk_to(index);
        read_topic();
        return 1;
    case PC_UI_MOUSE_MANUAL_CANVAS:
        if (m.mode != PC_UI_MANUAL_LIST || !content_ready())
            return 0;
        read_topic();
        return 1;
    case PC_UI_MOUSE_MANUAL_PAGE: {
        if (m.mode != PC_UI_MANUAL_READ || !content_ready() || (index != -1 && index != 1))
            return 0;
        int page = m.line[m.topic] / 10 + index, last = m.line_count[m.topic] / 10;
        page = page < 0 ? 0 : page > last ? last : page;
        set_line(page * 10);
        return 1;
    }
    case PC_UI_MOUSE_MANUAL_SCROLL:
        if (m.mode != PC_UI_MANUAL_READ || !content_ready())
            return 0;
        set_line(index);
        return 1;
    default:
        return 0;
    }
}

} // extern "C"

void pc_ui_manual_json(std::string& j)
{
    // what the manual asked the bridge to play, since the process started (on or off: the smoke compares
    // it with the sounds played across a close)
    char sfx[96];
    std::snprintf(sfx, sizeof sfx, ",\"manual_sfx\":{\"change\":%d,\"select\":%d,\"leave\":%d}", m.sfx[SFX_CHANGE],
        m.sfx[SFX_SELECT], m.sfx[SFX_LEAVE]);
    j += sfx;
    if (m.mode == PC_UI_MANUAL_OFF) {
        j += ",\"manual\":null";
        return;
    }
    auto esc = [](const std::string& s) {
        std::string o;
        for (unsigned char c : s) {
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
                continue;
            }
            if (c == '"' || c == '\\')
                o += '\\';
            o += (char)c;
        }
        return o;
    };
    int line = m.line[m.topic], lc = m.line_count[m.topic];
    char buf[768];
    std::snprintf(buf, sizeof buf,
        ",\"manual\":{\"mode\":\"%s\",\"topic\":%d,\"row\":%d,\"page_top\":%d,\"line\":%d,\"lines\":%d,"
        "\"line_count\":%d,\"page\":%d,\"pages\":%d,\"loaded\":%d,\"square\":%s,\"k\":%.3f,\"size\":%.3f,"
        "\"overflow\":%d,\"unknown\":%d,\"sprites\":%d,\"rules\":%d,\"hidden\":%d,\"art\":%d,\"art_prompts\":%s,"
        "\"sfx\":{\"change\":%d,\"select\":%d,\"leave\":%d},",
        m.mode == PC_UI_MANUAL_READ ? "reading" : "topics", m.topic, m.row, m.page, line, m.lines, lc, line / 10 + 1,
        lc / 10 + 1, m.loaded[m.topic], m.square_held ? "true" : "false", m.k, m.size, m.overflow, m.unknown,
        m.sprites, m.rules, m.hidden, m.art, m.art_prompts ? "true" : "false", m.sfx[SFX_CHANGE], m.sfx[SFX_SELECT], m.sfx[SFX_LEAVE]);
    j += buf;
    j += "\"label\":\"" + esc(m.label) + "\",\"desc\":\"" + esc(m.desc) + "\",\"page_lines\":" + m.page_lines
        + ",\"hits\":" + m.hits + "}";
}
