// Modern menus: the native Score screen's state and navigation (ui_score.h). Pure state: the game's side
// is score_bridge.c (through the events), the drawing ui_score_draw.cpp.
#include "ui/ui_score.h"
#include "ui/score_nav.h"
#include "ui/ui_theme.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const int OUTBOX = 64;
enum { MENU_LEAVE, MENU_SELECT, MENU_CHANGE };
const int SFX_MOVE = 4, SFX_BACK = 6, SFX_INVALID = 7;
// vs_battle_menu9CursorMemory_t's bytes (146C.h): titlePage, titleRow, encyclopaediaPage, encyclopaediaRow,
// recordTimePage, recordTimeRow, gazettePage, gazetteRow
const int PAGE_AT[PC_SCORE_LISTS] = {0, 4, 6, 2}, ROW_AT[PC_SCORE_LISTS] = {1, 5, 6, 3};
const int GAZETTE_ROW = 7;
const char* const MODE_IDS[] = {"off", "home", "titles", "records", "trials", "bestiary"};

struct Score {
    int mode = PC_UI_SCORE_OFF;
    int home = 0;               // Home's row (cursorMemories[2], gazetteRow at its exits)
    unsigned char cur[8] = {};  // the cursor memory as the bridge last wrote it
    int hint = 0;
    bool original = false;      // Square: ORIGINAL follows the leave's events
    int sfx[16] = {};           // vs_main_playSfxDefault(0x7E, n) asked for
    int menu_sfx[3] = {};       // leave, select, change
    pc_ui_score_event out[OUTBOX] = {};
    int head = 0, count = 0;
    std::string hits = "[]";
} m;
pc_ui_score model;

void push(int kind, int a = 0, int b = 0, int c = 0)
{
    if (m.count == OUTBOX)
        return; // the bridge drains every tick
    m.out[(m.head + m.count++) % OUTBOX] = {kind, a, b, c};
}
void sfx(int n)
{
    if (n >= 0 && n < 16)
        ++m.sfx[n];
    push(PC_UI_SEV_SFX, n);
}
void menu_sfx(int which)
{
    ++m.menu_sfx[which];
    push(PC_UI_SEV_MENU_SFX, which);
}
bool in_section() { return m.mode >= PC_UI_SCORE_TITLES; }
int section() { return in_section() ? m.mode - PC_UI_SCORE_TITLES : m.home; }
int list() { return section(); } // PC_SCORE_LIST_* are in the sections' order
unsigned char& page_of(int l) { return m.cur[PAGE_AT[l]]; }
unsigned char& row_of(int l) { return m.cur[ROW_AT[l]]; }
int focus_of(int l)
{
    int i = pc_score_nav_index(l, page_of(l), l == PC_SCORE_LIST_TRIALS ? 0 : row_of(l));
    int n = pc_score_nav_rows(l);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
void store(int l) { push(PC_UI_SEV_CURSOR, l, page_of(l), l == PC_SCORE_LIST_TRIALS ? 0 : row_of(l)); }
// _setRows(4, row): cursorMemories[2] = row, [3] = 0
void store_home() { push(PC_UI_SEV_HOME, m.home); }
// Home left (a section, Cross, Triangle, Square): the list's memory as MAINMENU's list leaves it (its
// moves write nothing until then), then gazetteRow = cursorMemories[2] + [3]
void leave_home()
{
    m.cur[GAZETTE_ROW] = (unsigned char)m.home;
    store_home();
    push(PC_UI_SEV_GAZETTE);
}
void enter_section(int s)
{
    m.home = s;
    leave_home();
    m.mode = PC_UI_SCORE_TITLES + s;
    m.hint = 0;
}
// L1 / R1: the next section (wrapping); Home's row follows so the original opens on it
void switch_section(int s)
{
    s &= 3;
    if (s == section())
        return;
    m.home = s;
    leave_home();
    m.mode = PC_UI_SCORE_TITLES + s;
    m.hint = 0;
    sfx(SFX_MOVE);
}
void home_step(int d)
{
    m.home = (m.home + d + 4) & 3; // the list's cursor (written at Home's exits: leave_home)
    menu_sfx(MENU_CHANGE);
}
// The Bestiary's Circle: a hint for an unlocked foe (its viewer is the original's), the invalid sound else
void bestiary_confirm()
{
    int i = focus_of(PC_SCORE_LIST_BESTIARY);
    if (model.valid && model.mon_ok && model.mon[i].unlocked)
        m.hint = 1;
    else
        sfx(SFX_INVALID);
}
std::string esc(const char* s)
{
    std::string o;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", *p);
            o += b;
            continue;
        }
        if (*p == '"' || *p == '\\')
            o += '\\';
        o += (char)*p;
    }
    return o;
}

} // namespace

extern "C" {

pc_ui_score* pc_ui_score_model_edit(void) { return &model; }
const pc_ui_score* pc_ui_score_model(void) { return &model; }
void pc_ui_score_model_ready(void)
{
    // the focus from the cursor memory _calculateProgress left (zeroed without cursor memory)
    std::memcpy(m.cur, model.cursor, sizeof m.cur);
    m.home = m.cur[GAZETTE_ROW] & 3;
    if (m.mode == PC_UI_SCORE_HOME)
        store_home();
}
int pc_ui_score_take(pc_ui_score_event* e)
{
    if (!m.count)
        return 0;
    *e = m.out[m.head];
    m.head = (m.head + 1) % OUTBOX;
    --m.count;
    return 1;
}
void pc_ui_score_forget(void)
{
    m.mode = PC_UI_SCORE_OFF;
    m.count = m.head = 0;
    m.original = false;
    m.hint = 0;
    model.valid = 0;
}
int pc_ui_score_on(void) { return m.mode != PC_UI_SCORE_OFF; }
int pc_ui_score_mode(void) { return m.mode; }

void pc_ui_score_enter(void)
{
    model.valid = 0; // the bridge fills it at OPEN
    m.mode = PC_UI_SCORE_HOME;
    m.original = false;
    m.hint = 0;
    push(PC_UI_SEV_OPEN);
    ++m.menu_sfx[MENU_SELECT]; // OPEN plays it (as deliver did for the original)
}

void pc_ui_score_back(const pc_ui_input*)
{
    if (in_section()) {
        sfx(SFX_BACK);
        m.mode = PC_UI_SCORE_HOME;
        m.hint = 0;
        store_home(); // the top menu again: _topMenu(1)
        return;
    }
    if (m.mode == PC_UI_SCORE_HOME) {
        leave_home();
        menu_sfx(MENU_LEAVE);
        push(PC_UI_SEV_CLOSE);
        m.mode = PC_UI_SCORE_OFF;
    }
}

void pc_ui_score_leave(void)
{
    if (m.mode == PC_UI_SCORE_OFF)
        return;
    if (m.mode == PC_UI_SCORE_HOME)
        leave_home();
    push(PC_UI_SEV_CLOSE);
    m.mode = PC_UI_SCORE_OFF;
    if (m.original)
        push(PC_UI_SEV_ORIGINAL);
    m.original = false;
    m.hint = 0;
}

void pc_ui_score_exit_step(const pc_ui_input* in)
{
    if (in_section() && (in->pressed & PC_UI_EXIT))
        sfx(SFX_BACK);
}

int pc_ui_score_tick(const pc_ui_input* in)
{
    if (m.mode == PC_UI_SCORE_OFF)
        return 0;
    if (in->pressed & PC_UI_ALT) { // the original screen (MENU9), on this section's row
        m.original = true;
        return 9;
    }
    if (m.mode == PC_UI_SCORE_HOME) {
        if (in->pressed & (PC_UI_CONFIRM | PC_UI_RIGHT)) {
            menu_sfx(MENU_SELECT);
            enter_section(m.home);
            return 0;
        }
        if (in->repeat & PC_UI_UP)
            home_step(-1);
        else if (in->repeat & PC_UI_DOWN)
            home_step(1);
        return 0;
    }
    int l = list();
    if (in->pressed & (PC_UI_TAB_PREV | PC_UI_TAB_NEXT)) {
        switch_section(section() + ((in->pressed & PC_UI_TAB_NEXT) ? 1 : -1));
        return 0;
    }
    if (l == PC_SCORE_LIST_BESTIARY && (in->pressed & (PC_UI_SUB_PREV | PC_UI_SUB_NEXT))) {
        int up = (in->pressed & PC_UI_SUB_PREV) != 0, moved = 0;
        int n = (int)(ui::theme().score.page_rows + 0.5f);
        for (int i = 0; i < n; ++i)
            moved |= pc_score_nav_step(l, &page_of(l), &row_of(l), up, 0, !up, 0);
        if (moved) {
            m.hint = 0;
            sfx(SFX_MOVE);
            store(l);
        }
        return 0;
    }
    if (l == PC_SCORE_LIST_BESTIARY && (in->pressed & PC_UI_CONFIRM)) {
        bestiary_confirm();
        return 0;
    }
    unsigned char row = l == PC_SCORE_LIST_TRIALS ? 0 : row_of(l);
    if (pc_score_nav_step(l, &page_of(l), &row, (in->repeat & PC_UI_UP) != 0, (in->pressed & PC_UI_UP) != 0,
            (in->repeat & PC_UI_DOWN) != 0, (in->pressed & PC_UI_DOWN) != 0)) {
        if (l != PC_SCORE_LIST_TRIALS)
            row_of(l) = row;
        m.hint = 0;
        sfx(SFX_MOVE);
        store(l);
    }
    return 0;
}

int pc_ui_score_mouse_focus(int, int) { return 0; }

int pc_ui_score_mouse_click(int kind, int index)
{
    if (m.mode == PC_UI_SCORE_OFF)
        return 0;
    switch (kind) {
    case PC_UI_MOUSE_SCORE_SECTION:
        if (index < 0 || index > 3)
            return 0;
        if (m.mode == PC_UI_SCORE_HOME) {
            m.home = index;
            menu_sfx(MENU_SELECT);
            enter_section(index);
        } else {
            switch_section(index);
        }
        return 1;
    case PC_UI_MOUSE_SCORE_ROW: {
        if (!in_section())
            return 0;
        int l = list();
        if (index < 0 || index >= pc_score_nav_rows(l))
            return 0;
        if (index == focus_of(l)) { // the focused row again: its Circle
            if (l == PC_SCORE_LIST_BESTIARY)
                bestiary_confirm();
            return 1;
        }
        unsigned char page, row;
        pc_score_nav_canon(l, index, &page, &row);
        page_of(l) = page;
        if (l != PC_SCORE_LIST_TRIALS)
            row_of(l) = row;
        m.hint = 0;
        sfx(SFX_MOVE);
        store(l);
        return 1;
    }
    default:
        return 0;
    }
}

void pc_ui_score_view_get(pc_ui_score_view* v)
{
    v->mode = m.mode;
    v->home_row = m.home;
    v->section = section();
    int l = list();
    v->focus = in_section() ? focus_of(l) : m.home;
    v->page = in_section() ? page_of(l) : 0;
    v->row = in_section() && l != PC_SCORE_LIST_TRIALS ? row_of(l) : 0;
    v->hint = m.hint;
}

void pc_ui_score_hits(const char* json) { m.hits = json && *json ? json : "[]"; }

const char* pc_ui_score_title_text(const pc_ui_score* s, int i, int desc)
{
    if (i < 0 || i >= PC_UI_SCORE_N_TITLES)
        return "";
    if ((s->titles >> i) & 1u)
        return desc ? s->title_desc[i] : s->title_name[i];
    return s->misc[desc ? 1 : 0];
}

} // extern "C"

void pc_ui_score_json(std::string& j)
{
    char buf[512];
    // the sounds asked for since the process started (on or off: a test compares them across a close)
    std::snprintf(buf, sizeof buf,
        ",\"score_sfx\":{\"move\":%d,\"back\":%d,\"invalid\":%d,\"leave\":%d,\"select\":%d,\"change\":%d}",
        m.sfx[SFX_MOVE], m.sfx[SFX_BACK], m.sfx[SFX_INVALID], m.menu_sfx[MENU_LEAVE], m.menu_sfx[MENU_SELECT],
        m.menu_sfx[MENU_CHANGE]);
    j += buf;
    if (m.mode == PC_UI_SCORE_OFF) {
        j += ",\"score\":null";
        return;
    }
    pc_ui_score_view v;
    pc_ui_score_view_get(&v);
    const pc_ui_score& s = model;
    std::snprintf(buf, sizeof buf,
        ",\"score\":{\"mode\":\"%s\",\"home\":%d,\"section\":%d,\"focus\":%d,\"page\":%d,\"row\":%d,\"hint\":%s,"
        "\"cursor\":[%d,%d,%d,%d,%d,%d,%d,%d],\"valid\":%s,\"tables_ok\":%s,\"rank\":%d,\"score\":%u,"
        "\"titles_mask\":%u,\"clears\":%u,\"map\":[%u,%u],\"chests\":[%u,%u],\"max_chain\":%u,\"unlocked\":%d,"
        "\"mon_ok\":%s,",
        MODE_IDS[m.mode], m.home, v.section, v.focus, v.page, v.row, v.hint ? "true" : "false", m.cur[0], m.cur[1],
        m.cur[2], m.cur[3], m.cur[4], m.cur[5], m.cur[6], m.cur[7], s.valid ? "true" : "false",
        s.tables_ok ? "true" : "false", s.rank, s.score, s.titles, s.clears, s.map_now, s.map_best, s.chests_now,
        s.chests_best, s.max_chain, s.unlocked, s.mon_ok ? "true" : "false");
    j += buf;
    j += "\"mon_why\":\"" + esc(s.mon_why) + "\",\"rank_text\":\"" + esc(s.rank_text) + "\"";
    ui::score_json(j);
    j += ",\"hits\":" + m.hits;
    // the text of this open (decoded once at OPEN): every string the screen can show
    j += ",\"text\":{\"sections\":[";
    for (int i = 0; i < 4; ++i)
        j += (i ? ",\"" : "\"") + esc(s.section[i]) + "\"";
    j += "],\"misc\":[";
    for (int i = 0; i < PC_UI_SCORE_N_MISC; ++i)
        j += (i ? ",\"" : "\"") + esc(s.misc[i]) + "\"";
    j += "],\"titles\":[";
    for (int i = 0; i < PC_UI_SCORE_N_TITLES; ++i)
        j += std::string(i ? "," : "") + "[\"" + esc(s.title_name[i]) + "\",\"" + esc(s.title_desc[i]) + "\"]";
    j += "],\"titles_shown\":["; // what each Titles row draws (locked: miscInfo)
    for (int i = 0; i < PC_UI_SCORE_N_TITLES; ++i)
        j += std::string(i ? "," : "") + "[\"" + esc(pc_ui_score_title_text(&s, i, 0)) + "\",\""
            + esc(pc_ui_score_title_text(&s, i, 1)) + "\"]";
    j += "],\"records\":[";
    for (int i = 0; i < PC_UI_SCORE_N_STATS; ++i)
        j += std::string(i ? "," : "") + "[\"" + esc(s.stat_name[i]) + "\",\"" + esc(s.stat_desc[i]) + "\",\""
            + esc(s.stat_value[i]) + "\"," + (s.stat_none[i] ? "true" : "false") + "]";
    j += "],\"trials\":[";
    for (int i = 0; i < PC_UI_SCORE_N_TRIALS; ++i) {
        std::snprintf(buf, sizeof buf, "%s{\"has\":%s,\"times\":[\"%s\",\"%s\",\"%s\"],\"rounds\":[%d,%d,%d],\"ref\":\"%s\",",
            i ? "," : "", s.trial_has[i] ? "true" : "false", s.trial_time[i][0], s.trial_time[i][1], s.trial_time[i][2],
            s.trial_round[i][0], s.trial_round[i][1], s.trial_round[i][2], s.trial_ref[i]);
        j += buf;
        j += "\"name\":\"" + esc(s.trial_name[i]) + "\",\"desc\":\"" + esc(s.trial_desc[i]) + "\"}";
    }
    j += "],\"bestiary\":[";
    for (int i = 0; i < PC_MON_ENEMIES; ++i) {
        std::snprintf(buf, sizeof buf, "%s{\"zud\":%d,\"cls\":%d,\"unlocked\":%d,\"prev\":%d,\"next\":%d,", i ? "," : "",
            s.mon[i].zud, s.mon[i].cls, s.mon[i].unlocked, s.mon[i].prev, s.mon[i].next);
        j += buf;
        j += "\"name\":\"" + esc(s.mon[i].name) + "\",\"desc\":\"" + esc(s.mon[i].desc) + "\"}";
    }
    j += "]}}";
}
