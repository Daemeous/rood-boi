/* The Option pages in the game's own Option screen (classic_options.h; battle
 * builds). Drawn the way the game's own nested lists are:
 *
 *   - MENU8 hoists the picked entry into the title stack (under OPTIONS, as a
 *     row's flyout does); the page's rows slide in below it one a frame, at
 *     rowInfo 2 (menu items 20-27, eight visible, the game's paging and
 *     scroll arrows), each a game menu window wide enough for a label and a
 *     value: vs_battle_setMenuItem (its gradient, its label in the game's
 *     font, greyed when unavailable, headers inverted), the cursor hand
 *     (vs_mainMenu_renderCursor) and the information box
 *     (vs_mainmenu_setInformationMessage) with the row's help, or why it is
 *     greyed;
 *   - the value column is drawn with the same printers: ON / OFF with the
 *     current one lit, a choice's name (with arrows on the focused row), a
 *     slider as the HP/MP/RISK gauge (vs_battle_renderStatBar) and its value
 *     in the info capitals, an arrow for a link;
 *   - Circle on a choice opens the game's own flyout (vs_mainmenu_setMenuRows
 *     at rowInfo 3: flag 4 on the current choice, flag 1 on unavailable ones)
 *     with the row hoisted as its title; a risky display change asks "Keep
 *     these settings?" the same way (KEEP / REVERT, the seconds left in the
 *     information box; display_confirm.h).
 *
 * Text goes through vs_encode (the game's charset; capitals as the game's
 * own rows, classic_uppercase), so ui_text restyles it like the game's.
 * Labels wider than their column are cut, with a warning. Look and feel
 * values are option_style.h's (rood.options' Lua table).
 *
 * Input is the game's: Up/Down with its button repeat and paging
 * (options_nav.c, the list's own rules), Left/Right step a value, Circle
 * picks, Cross goes back (a sub-page to its parent, a page to the list),
 * Triangle closes the menu. Every change goes through the settings catalog
 * (ui_settings.h), as the modern System pages' do: settings.cfg only, saved
 * debounced and when the page closes; the game's own options (the memory
 * card's vs_main_settings) are MENU8's rows above, never written here. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "../../../src/MENU/MAINMENU.PRG/C48.h"
#include "../../../src/MENU/MAINMENU.PRG/58EC.h"
#include "classic_options.h"
#include "option_actions.h"
#include "option_pages.h"
#include "option_style.h"
#include "options_nav.h"
#include "ui_settings.h"
#include "vs_encode.h"
#include "../display_confirm.h"
#include "../host_format.h" /* pc_format, pc_log_line, pc_host_seconds */
#include <libetc.h>
#include <string.h>

#define ROW_BASE 20  /* menu items of the page's rows (rowInfo 2) */
#define ROW_INFO 2
#define ROWS 8       /* visible: 10 - rowInfo */
#define FLY_BASE 30  /* the flyouts' (rowInfo 3) */
#define FLY_ID 0x46  /* the flyout's cursor memory: MENU8's map flyout's slot, saved and put back */
#define MAX_ROWS 64  /* a page's rows (rood.menu's limit) */
#define DEPTH 4      /* sub-pages deep */
#define TEXT 96      /* encoded text */
#define GLYPH_SPACE 0x8F
#define GLYPH_END 0xE7
#define GLYPH_SPACING 0xFA

enum { S_NONE, S_ENTER, S_PAGE, S_SWAP, S_FLY_WAIT, S_FLY, S_CONFIRM_WAIT, S_CONFIRM, S_BACK, S_LEAVE };
/* (RPC options' screen: a list takes input in "page", "flyout", "confirm" and MENU8's "list") */
static const char* const STATE_NAMES[] = { "none", "entering", "page", "entering", "entering", "flyout", "entering",
    "confirm", "entering", "leaving" };

typedef struct {
    char id[64];
    int form, kind, catalog, link, enabled, selectable;
} Row;

static struct {
    int state;
    int entry;                 /* the entry opened (pc_option_pages_children(-1) index) */
    int title_item;            /* the hoisted entry's menu item (10-18) */
    char page_id[80];
    struct {
        char id[80], focus[64];
        int top;
    } stack[DEPTH];
    int depth;
    unsigned generation, style_generation;
    Row rows[MAX_ROWS];
    unsigned char selectable[MAX_ROWS];
    int count, cursor, top;
    int placed;                /* rows slid in so far (S_ENTER: one a frame) */
    int anim;                  /* the cursor hand's brightness step */
    char focus_id[64];
    int result;                /* S_LEAVE: what exec returns once the menu rests */
    /* the flyout */
    int fly_row, fly_count, fly_saved[2];
    unsigned short fly_names[16][16], fly_help[16][48];
    /* the confirm */
    int confirm_focus, confirm_placed, confirm_shown;
    int info_seconds;
    char info_key[512];        /* the help the information box shows, whole (its lines take turns) */
    int entries_built;         /* MENU8's list has our entries */
} S;

/* (the game's string.h takes char*) */
static int same(const char* a, const char* b) { return strcmp((char*)a, (char*)b) == 0; }

/* Look and feel (option_style.h). */
static int style(int i) { return pc_option_style_int(i); }

/* ---- text ---- */

static int glyph_width(int g) { return g < 0xE5 ? (u_char)vs_battle_characterWidths[g] : 0; }

/* UTF-8 to the game's encoding (capitals when the style says so), without its
 * terminator; out holds cap bytes and is terminated. Returns the length. */
static int encode(const char* utf8, u_char* out, int cap, int upper)
{
    char buf[TEXT * 2];
    int i, n;
    for (i = 0; utf8 && utf8[i] && i < (int)sizeof buf - 1; i++)
        buf[i] = upper && utf8[i] >= 'a' && utf8[i] <= 'z' ? utf8[i] - 32 : utf8[i];
    buf[i] = 0;
    n = vs_encode(buf, out, cap, NULL) - 1;
    return n < 0 ? 0 : n;
}
static int width_of(const u_char* g, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++)
        w += glyph_width(g[i]);
    return w;
}
/* Cut to fit `room` pixels with "..." (the game's '.'), terminated; returns the
 * new length. A label that had to be cut is reported once (by its text). */
static int fit(u_char* g, int n, int room, const char* what)
{
    static u_char dot;
    static char warned[32][48];
    static int warned_n;
    int w = width_of(g, n), i, dots;
    if (w <= room)
        return n;
    if (!dot) {
        u_char d[4];
        encode(".", d, sizeof d, 0);
        dot = d[0];
    }
    dots = 2 * glyph_width(dot);
    for (i = n; i > 0 && width_of(g, i) + dots > room; i--) {
    }
    while (i > 0 && g[i - 1] == GLYPH_SPACE)
        i--;
    g[i] = dot;
    g[i + 1] = dot;
    g[i + 2] = GLYPH_END;
    if (what) {
        int known = 0;
        for (int k = 0; k < warned_n; k++)
            known |= !strncmp(warned[k], (char*)what, sizeof warned[0] - 1);
        if (!known) {
            char line[160];
            pc_format(line, sizeof line, "[options] \"%s\" is wider than its row in the classic Option screen (%d > %d px): cut",
                what, w, room);
            pc_log_line(line);
            if (warned_n < 32)
                pc_format(warned[warned_n++], sizeof warned[0], "%s", what);
        }
    }
    return i + 2;
}

/* The OT slot the menu windows' labels go in (vs_battle_renderMenuItem's). */
static u_long* label_slot(void) { return (u_long*)((char*)psx_get(vs_scratch.unk8) + 8); }

/* Glyphs in the game's menu font (font colour c: 0 white, 1 lit, 3 grey); returns the pen. */
static int draw_glyphs(const u_char* g, int n, int x, int y, int c)
{
    u_long* ot = label_slot();
    vs_battle_setFontStyle(c + 4);
    for (int i = 0; i < n; i++) {
        if (g[i] == GLYPH_SPACING && i + 1 < n) {
            x += g[++i];
            continue;
        }
        x = vs_battle_printVariableWidthFontChar(g[i], x, y, ot - 3);
    }
    return x;
}
static int draw_text(const char* utf8, int x, int y, int c, int room)
{
    u_char g[TEXT];
    int n = encode(utf8, g, sizeof g - 3, style(PC_STYLE_UPPERCASE));
    if (room > 0)
        n = fit(g, n, room, NULL);
    return draw_glyphs(g, n, x, y, c);
}
static int text_width(const char* utf8)
{
    u_char g[TEXT];
    return width_of(g, encode(utf8, g, sizeof g, style(PC_STYLE_UPPERCASE)));
}

/* The information box's message: the help wrapped to the box (classic_help_lines,
 * classic_help_width), cut with "..." past them; set when it changes. */
/* The information box holds one line: a longer help is split into lines of
 * classic_help_width, shown in turn for classic_help_seconds each. */
#define HELP_LINES 8
static struct {
    u_char glyphs[TEXT * 3];
    int start[HELP_LINES], end[HELP_LINES], lines, shown;
    double since;
} help;
static void info_line(int k)
{
    static u_short message[48 + 4]; /* setInformationMessage copies 96 bytes */
    u_char* out = (u_char*)message;
    int n = help.end[k] - help.start[k];
    memset(message, 0, sizeof message);
    out[0] = 0xF8; /* vs_char_chunkSize 0: all at once, as the list's descriptions */
    out[1] = 0;
    n = n < 92 ? n : 92;
    memcpy(out + 2, help.glyphs + help.start[k], (size_t)n);
    out[2 + n] = GLYPH_END;
    if (help.shown != k || !help.since)
        help.since = pc_host_seconds();
    help.shown = k;
    vs_mainmenu_setInformationMessage((char*)message);
}
static void info(const char* text)
{
    int n, i, room = style(PC_STYLE_HELP_WIDTH);
    if (!text)
        text = "";
    if (S.info_key[0] && !strncmp(S.info_key, (char*)text, sizeof S.info_key - 1)) {
        /* (set every frame, as the game's lists do: the box may have been re-opened meanwhile) */
        if (help.lines > 1 && pc_host_seconds() - help.since >= pc_option_style_value(PC_STYLE_HELP_SECONDS))
            info_line((help.shown + 1) % help.lines);
        else
            info_line(help.shown < help.lines ? help.shown : 0);
        return;
    }
    pc_format(S.info_key, sizeof S.info_key, "%s", text);
    n = encode(text, help.glyphs, sizeof help.glyphs, 0);
    help.lines = 0;
    help.shown = -1;
    for (i = 0; i < n && help.lines < HELP_LINES;) {
        int start = i, pen = 0, last = -1; /* last: the end of the last whole word that fits */
        while (i < n) {
            int end = i, w = 0;
            while (end < n && help.glyphs[end] != GLYPH_SPACE) /* a word */
                w += glyph_width(help.glyphs[end++]);
            if (pen && pen + glyph_width(GLYPH_SPACE) + w > room)
                break;
            pen += (pen ? glyph_width(GLYPH_SPACE) : 0) + w;
            last = end;
            i = end < n ? end + 1 : end;
            if (end - start >= 90) /* (a line's bytes) */
                break;
        }
        if (last < 0) { /* one word wider than the line: as much as fits */
            last = start;
            while (last < n && last - start < 90 && width_of(help.glyphs + start, last - start + 1) <= room)
                last++;
            i = last > start ? last : start + 1;
        }
        help.start[help.lines] = start;
        help.end[help.lines] = last;
        help.lines++;
    }
    if (!help.lines) {
        help.start[0] = help.end[0] = 0;
        help.lines = 1;
    }
    info_line(0);
}

/* ---- the page's rows ---- */

static int page_index(void) { return pc_option_pages_index(S.page_id); }

static int row_info(int abs, pc_option_row* out)
{
    return pc_option_page_row(page_index(), PC_UI_WHERE_CLASSIC, abs, out);
}

static int selectable_kind(int kind) { return kind != PC_UI_SET_HEADER && kind != PC_UI_SET_NOTE; }

/* The rows from the page model; the focus kept by id. 0: the page is gone. */
static int rebuild(void)
{
    int page = page_index(), i;
    pc_option_row r;
    if (page < 0)
        return 0;
    S.generation = pc_option_pages_generation();
    S.count = pc_option_page_rows(page, PC_UI_WHERE_CLASSIC);
    if (S.count > MAX_ROWS)
        S.count = MAX_ROWS;
    for (i = 0; i < S.count; i++) {
        Row* row = &S.rows[i];
        memset(row, 0, sizeof *row);
        if (pc_option_page_row(page, PC_UI_WHERE_CLASSIC, i, &r)) {
            pc_format(row->id, sizeof row->id, "%s", r.row.id ? r.row.id : "");
            row->form = r.form, row->kind = r.row.kind, row->catalog = r.catalog, row->link = r.link;
            row->enabled = r.row.enabled;
            row->selectable = selectable_kind(r.row.kind);
        }
        S.selectable[i] = (u_char)row->selectable;
    }
    /* the focus: the row it was on, else the nearest selectable one */
    int found = -1;
    for (i = 0; S.focus_id[0] && i < S.count; i++)
        if (same(S.rows[i].id, S.focus_id))
            found = i;
    if (found < 0) {
        int c = S.cursor < S.count ? S.cursor : S.count - 1;
        for (i = 0; i < S.count; i++) {
            int k = c + i < S.count ? c + i : c - (i - (S.count - 1 - c));
            if (k >= 0 && k < S.count && S.selectable[k]) {
                found = k;
                break;
            }
        }
    }
    S.cursor = found < 0 ? 0 : found;
    int max_top = S.count > ROWS ? S.count - ROWS : 0;
    if (S.top > max_top)
        S.top = max_top;
    if (S.cursor < S.top)
        S.top = S.cursor;
    if (S.cursor >= S.top + ROWS)
        S.top = S.cursor - ROWS + 1;
    if (S.count > 0)
        pc_format(S.focus_id, sizeof S.focus_id, "%s", S.rows[S.cursor].id);
    return 1;
}

static int row_y(int window) { return (window + ROW_INFO) * 16 + 18; }

/* The label as the row's window shows it: encoded and cut to its column. */
/* The room a row's label has: to the value column, or the window's width. */
static int label_room(int kind)
{
    int room;
    switch (kind) {
    case PC_UI_SET_ACTION:
    case PC_UI_SET_HEADER:
        room = style(PC_STYLE_ROW_W) - 12;
        break;
    case PC_UI_SET_LINK:
        room = style(PC_STYLE_ROW_W) - 30;
        break;
    default:
        room = style(PC_STYLE_VALUE_X) - 10;
        break;
    }
    return room > 320 - style(PC_STYLE_ROW_X) - 12 ? 320 - style(PC_STYLE_ROW_X) - 12 : room;
}
/* The label wider than its room (the classic screen cuts it). */
static int label_cut(const pc_option_row* r)
{
    u_char g[TEXT];
    if (r->row.kind == PC_UI_SET_NOTE)
        return 0;
    return width_of(g, encode(r->row.label, g, sizeof g, style(PC_STYLE_UPPERCASE))) > label_room(r->row.kind);
}
static void row_label(const pc_option_row* r, u_char* g, int cap)
{
    int n;
    const char* text = r->row.label ? r->row.label : "";
    if (r->row.kind == PC_UI_SET_NOTE) { /* drawn by draw_values (wider than a window's label) */
        g[0] = GLYPH_END;
        return;
    }
    n = encode(text, g, cap - 3, style(PC_STYLE_UPPERCASE));
    n = fit(g, n, label_room(r->row.kind), text);
    g[n < 30 ? n : 30] = GLYPH_END; /* a window holds 31 glyphs */
}

/* Window row i (rows top + i) as a menu item at x; sliding in to the column when `slide`. */
static void place(int i, int x, int slide)
{
    int abs = S.top + i;
    pc_option_row r;
    u_char label[40];
    vs_battle_menuItem_t* item;
    if (abs >= S.count || !row_info(abs, &r)) {
        vs_battle_getMenuItem(ROW_BASE + i)->state = 0;
        return;
    }
    row_label(&r, label, sizeof label);
    item = vs_battle_setMenuItem(ROW_BASE + i, x, row_y(i),
        r.row.kind == PC_UI_SET_NOTE ? 0 : style(PC_STYLE_ROW_W), 0, (char*)label);
    if (slide) {
        item->state = menuItemStateSlideX;
        item->targetPosition0 = style(PC_STYLE_ROW_X);
    }
    item->unselectable = !r.row.enabled || !selectable_kind(r.row.kind);
    if (r.row.kind == PC_UI_SET_HEADER) {
        item->invertGradient = 1;
        item->fontColor = 1;
        item->unselectable = 1;
    }
    if (i == 0 && S.top > 0)
        item->isScrollable = menuItem_scrollUp;
    if (i == ROWS - 1 && S.top + ROWS < S.count)
        item->isScrollable = menuItem_scrollDown;
}
static int visible(void) { return S.count - S.top < ROWS ? S.count - S.top : ROWS; }
static void place_all(int slide)
{
    for (int i = 0; i < ROWS; i++)
        if (i < visible())
            place(i, slide ? 320 : style(PC_STYLE_ROW_X), slide);
        else
            vs_battle_getMenuItem(ROW_BASE + i)->state = 0;
}
/* Rows and flyouts away to the right (the title stack stays). */
static void fly_out_rows(void)
{
    for (int i = ROW_BASE; i < 40; i++)
        vs_mainMenu_menuItemFlyoutRight(i);
}

/* ---- values ---- */

static void draw_values(void)
{
    int right = style(PC_STYLE_ROW_X) + style(PC_STYLE_ROW_W);
    if (right > 316)
        right = 316;
    for (int i = 0; i < visible(); i++) {
        int abs = S.top + i, x, y, c;
        pc_option_row r;
        vs_battle_menuItem_t* item = vs_battle_getMenuItem(ROW_BASE + i);
        if (item->state == 0 || item->x >= 320 || !row_info(abs, &r))
            continue;
        x = item->x + style(PC_STYLE_VALUE_X);
        y = item->y;
        /* the window's right end, at most the screen's (sliding in: as far off screen as the window) */
        right = item->x + style(PC_STYLE_ROW_W) - 6;
        if (right > 316 + item->x - style(PC_STYLE_ROW_X))
            right = 316 + item->x - style(PC_STYLE_ROW_X);
        c = r.row.enabled ? style(PC_STYLE_LIT) : 3;
        switch (r.row.kind) {
        case PC_UI_SET_TOGGLE: {
            const char *on = NULL, *off = NULL;
            pc_ui_settings_catalog_choice(r.catalog, 1, &on, NULL);
            pc_ui_settings_catalog_choice(r.catalog, 0, &off, NULL);
            int other = r.row.enabled ? style(PC_STYLE_OTHER) : 3;
            x = draw_text(on ? on : "On", x, y, r.row.index == 1 ? c : other, 0) + style(PC_STYLE_TOGGLE_GAP);
            draw_text(off ? off : "Off", x, y, r.row.index == 0 ? c : other, 0);
            break;
        }
        case PC_UI_SET_CHOICE: {
            int focused = abs == S.cursor && r.row.enabled, arrow = text_width("<") + 2;
            if (focused)
                draw_text("<", x, y, 0, 0);
            x = draw_text(r.row.value, x + arrow, y, c, right - x - 2 * arrow);
            if (focused)
                draw_text(">", x + 2, y, 0, 0);
            break;
        }
        case PC_UI_SET_SLIDER: {
            /* the gauge (the HP/MP/RISK bar), the value right-aligned after it in the menu font */
            int bar = style(PC_STYLE_BAR_WIDTH), w, tw = text_width(r.row.value);
            float span = r.row.max - r.row.min;
            w = span > 0 ? (int)((r.row.number - r.row.min) * bar / span + 0.5f) : 0;
            w = w < 0 ? 0 : w > bar ? bar : w;
            if (x + bar + 2 + 6 + tw <= right) /* (unless the value's text needs its room: "Match window") */
                vs_battle_renderStatBar(style(PC_STYLE_BAR_COLOUR), w, label_slot(), (x & 0xFFFF) | ((y + 4) << 16));
            draw_text(r.row.value, tw < right - x ? right - tw : x, y, c, right - x);
            break;
        }
        case PC_UI_SET_BINDING:
        case PC_UI_SET_INFO:
            draw_text(r.row.value, x, y, r.row.enabled ? 0 : 3, right - x);
            break;
        case PC_UI_SET_LINK:
            draw_text(">", right - text_width(">"), y, r.row.enabled && r.link >= 0 ? 0 : 3, 0);
            break;
        case PC_UI_SET_NOTE: { /* a line of help: as written (the info box's case), dim */
            u_char g[TEXT];
            int n = encode(r.row.label, g, sizeof g - 3, 0);
            draw_glyphs(g, fit(g, n, right - item->x - 6, NULL), item->x + 6, y, 3);
            break;
        }
        default:
            break;
        }
    }
}

/* The focused row's help, or why it is greyed. */
static void row_help(void)
{
    pc_option_row r;
    if (pc_ui_settings_capture_button() >= 0) {
        const char* note = pc_ui_settings_capture_note();
        info(note ? note : "Press a key or a gamepad button for it. Esc cancels.");
        return;
    }
    if (!S.count || !row_info(S.cursor, &r)) {
        info("");
        return;
    }
    info(!r.row.enabled && r.row.reason[0] ? r.row.reason : r.row.help);
}

static void draw_cursor(int item_id)
{
    vs_battle_menuItem_t* item = vs_battle_getMenuItem(item_id);
    item->selected = 1;
    S.anim = vs_mainMenu_renderCursor(S.anim, (item->x - 14) | ((item->y - 8) << 16));
}

/* ---- page changes ---- */

static void leave(int result)
{
    pc_display_confirm_revert(); /* (nothing pending: nothing happens) */
    pc_ui_settings_flush();      /* the page's changes saved as it closes */
    if (result == -2) {
        vs_mainMenu_clearMenuExcept(vs_mainMenu_menuItemIds_none);
        vs_mainMenu_dismissInformationBox();
        vs_mainMenu_setMenuCommand(menuActionNone);
    } else {
        vs_mainMenu_clearMenuExcept(7); /* all but the OPTIONS title: MENU8's list comes back */
    }
    S.result = result;
    S.state = S_LEAVE;
}

static void set_title(const char* title)
{
    vs_battle_menuItem_t* item;
    u_char g[40];
    int n, i;
    if (S.title_item < 10 || S.title_item >= 20)
        return;
    item = vs_battle_getMenuItem(S.title_item);
    n = encode(title, g, sizeof g - 3, style(PC_STYLE_UPPERCASE));
    n = fit(g, n, 320 - 180 - 12, title);
    for (i = 0; i < n && i < 30; i++)
        item->text[i] = (char)g[i];
    item->text[i] = (char)0xFF;
}

/* Into page `id` (a sub-page: pushed; NULL: back to the parent). */
static void go(const char* id, int push)
{
    pc_option_page info_;
    if (push) {
        if (S.depth >= DEPTH)
            return;
        pc_format(S.stack[S.depth].id, sizeof S.stack[0].id, "%s", S.page_id);
        pc_format(S.stack[S.depth].focus, sizeof S.stack[0].focus, "%s", S.focus_id);
        S.stack[S.depth].top = S.top;
        S.depth++;
        pc_format(S.page_id, sizeof S.page_id, "%s", id);
        S.focus_id[0] = 0;
        S.cursor = S.top = 0;
    } else {
        S.depth--;
        pc_format(S.page_id, sizeof S.page_id, "%s", S.stack[S.depth].id);
        pc_format(S.focus_id, sizeof S.focus_id, "%s", S.stack[S.depth].focus);
        S.top = S.stack[S.depth].top;
    }
    if (pc_option_pages_info(page_index(), &info_))
        set_title(info_.title);
    pc_ui_settings_flush();
    fly_out_rows();
    S.state = S_SWAP;
    S.placed = -1;
    S.info_key[0] = 0;
}

/* ---- the choice flyout and the confirm ---- */

static void hoist(void)
{
    vs_mainMenu_flyoutMenuRightAndHoistSelection(S.cursor - S.top, ROW_INFO);
}

/* A list's cursor memory byte (setMenuRows indexes past cursorMemories[] by its list id, as here). */
static char* cursor_memory(int i)
{
    char* volatile base = (char*)&D_800F4EE8 + (D_800F4EE8.cursorMemories - (char*)&D_800F4EE8);
    return base + i;
}

static void open_flyout(void)
{
    pc_option_row r;
    if (!row_info(S.cursor, &r))
        return;
    S.fly_row = S.cursor;
    hoist();
    S.state = S_FLY_WAIT;
}

static void show_flyout(void)
{
    pc_option_row r;
    char* strings[32];
    int types[16], i, n, current;
    if (!row_info(S.fly_row, &r)) {
        S.state = S_BACK;
        return;
    }
    n = r.row.count < 16 ? r.row.count : 16;
    current = r.row.index;
    for (i = 0; i < n; i++) {
        const char *name = NULL, *why = NULL;
        u_char g[TEXT];
        int k;
        pc_ui_settings_catalog_choice(r.catalog, i, &name, &why);
        memset(S.fly_names[i], 0, sizeof S.fly_names[i]);
        memset(S.fly_help[i], 0, sizeof S.fly_help[i]);
        k = encode(name ? name : "?", g, sizeof g - 3, style(PC_STYLE_UPPERCASE));
        k = fit(g, k, 120, name);
        memcpy(S.fly_names[i], g, (size_t)(k < 25 ? k : 25));
        ((u_char*)S.fly_names[i])[k < 25 ? k : 25] = GLYPH_END;
        k = encode(why ? why : r.row.help, g, sizeof g, 0);
        k = k < 90 ? k : 90;
        memcpy(S.fly_help[i], g, (size_t)k);
        ((u_char*)S.fly_help[i])[k] = GLYPH_END;
        strings[i * 2] = (char*)S.fly_names[i];
        strings[i * 2 + 1] = (char*)S.fly_help[i];
        types[i] = (why ? 1 : 0) | (i == current ? 4 : 0);
    }
    /* the cursor on the current choice: the flyout's cursor memory (put back after) */
    S.fly_saved[0] = *cursor_memory(FLY_ID * 2);
    S.fly_saved[1] = *cursor_memory(FLY_ID * 2 + 1);
    {
        int rows = 10 - 3, page = 0, at = current < 0 ? 0 : current;
        if (n > rows && at > rows - 2) {
            page = at - (rows - 2);
            page = page > n - rows ? n - rows : page;
        }
        *cursor_memory(FLY_ID * 2) = (char)(at - page);
        *cursor_memory(FLY_ID * 2 + 1) = (char)page;
    }
    i = vs_main_settings.cursorMemory;
    vs_main_settings.cursorMemory = 1;
    vs_mainmenu_setMenuRows(n, 0x300 | FLY_ID, strings, types);
    vs_main_settings.cursorMemory = i;
    S.fly_count = n;
    S.state = S_FLY;
}

static void confirm_begin(void)
{
    S.confirm_focus = 0;
    S.confirm_placed = 0;
    S.info_seconds = -1;
    if (S.state != S_FLY) /* (from the flyout the row is hoisted already) */
        hoist();
    S.state = S_CONFIRM_WAIT;
}
static int confirm_seconds(void)
{
    double left = pc_display_confirm_remaining();
    return (int)(left + 0.999);
}
static void confirm_info(void)
{
    char text[120];
    int s = confirm_seconds();
    S.info_seconds = s;
    pc_format(text, sizeof text, "Keep these settings? Reverts in %d s.", s);
    info(text);
}
static void confirm_place(void)
{
    static const char* const names[2] = { "Keep", "Revert" };
    for (int k = 0; k < 2; k++) {
        u_char g[16];
        int n = encode(names[k], g, sizeof g, style(PC_STYLE_UPPERCASE));
        g[n] = GLYPH_END;
        vs_battle_menuItem_t* item = vs_battle_setMenuItem(FLY_BASE + k, 320, (k + 3) * 16 + 18, 126, 0, (char*)g);
        item->state = menuItemStateSlideX;
        item->targetPosition0 = 194;
    }
}
static void confirm_done(int keep)
{
    if (keep) {
        pc_display_confirm_keep();
        vs_battle_playMenuSelectSfx();
    } else {
        pc_display_confirm_revert();
        vs_battle_playMenuLeaveSfx();
    }
    S.state = S_BACK;
}

/* ---- input on the page ---- */

static void after_change(void)
{
    rebuild(); /* rows may come and go with the value (dependent settings) */
    if (pc_display_confirm_pending())
        confirm_begin();
}

static void step_value(int delta)
{
    const Row* row = &S.rows[S.cursor];
    int k = row->kind;
    if (k != PC_UI_SET_TOGGLE && k != PC_UI_SET_CHOICE && k != PC_UI_SET_SLIDER)
        return;
    if (!row->enabled) {
        vs_battle_playInvalidSfx();
        return;
    }
    if (pc_ui_settings_catalog_change(row->catalog, delta)) {
        vs_battle_playMenuChangeSfx();
        after_change();
    }
}

static void activate(void)
{
    const Row* row = &S.rows[S.cursor];
    if (!row->selectable || !row->enabled) {
        vs_battle_playInvalidSfx();
        return;
    }
    switch (row->kind) {
    case PC_UI_SET_TOGGLE:
        if (pc_ui_settings_catalog_change(row->catalog, 1)) {
            vs_battle_playMenuSelectSfx();
            after_change();
        }
        return;
    case PC_UI_SET_CHOICE:
        vs_battle_playMenuSelectSfx();
        open_flyout();
        return;
    case PC_UI_SET_LINK: {
        pc_option_page p;
        if (row->link < 0 || !pc_option_pages_info(row->link, &p)) {
            vs_battle_playInvalidSfx();
            return;
        }
        vs_battle_playMenuSelectSfx();
        go(p.id, 1);
        return;
    }
    case PC_UI_SET_ACTION:
    case PC_UI_SET_BINDING: {
        pc_option_row r;
        int ran = row_info(S.cursor, &r)
            && (r.form == PC_OPT_ACTION ? pc_option_action_run(r.ref) : pc_ui_settings_catalog_activate(row->catalog));
        if (ran) {
            vs_battle_playMenuSelectSfx();
            after_change();
        } else
            vs_battle_playInvalidSfx();
        return;
    }
    default:
        vs_battle_playInvalidSfx();
        return;
    }
}

static void page_input(void)
{
    u_int pressed = vs_main_buttonsPressed.all, repeat = (u_int)vs_main_buttonRepeat, nav = 0;
    if (pc_ui_settings_capture_button() >= 0)
        return; /* a binding capture takes the keys and pad (boot_window.c) */
    if (pressed & PADRup) {
        vs_battle_playMenuLeaveSfx();
        leave(-2);
        return;
    }
    if (pressed & PADRright) {
        activate();
        return;
    }
    if (pressed & PADRdown) {
        vs_battle_playMenuLeaveSfx();
        if (S.depth > 0)
            go(NULL, 0);
        else
            leave(-1);
        return;
    }
    if (repeat & PADLup)
        nav |= OPTIONS_NAV_UP;
    if (repeat & PADLdown)
        nav |= OPTIONS_NAV_DOWN;
    if (pressed & PADLup)
        nav |= OPTIONS_NAV_FRESH_UP;
    if (pressed & PADLdown)
        nav |= OPTIONS_NAV_FRESH_DOWN;
    if (nav) {
        options_nav_state st = { S.count, S.cursor, S.top, S.selectable, ROWS };
        options_nav_result res = options_nav_step(st, nav);
        int old_top = S.top;
        if (res.sound == OPTIONS_NAV_CHANGE)
            vs_battle_playMenuChangeSfx();
        else if (res.sound == OPTIONS_NAV_INVALID)
            vs_battle_playInvalidSfx();
        S.cursor = res.state.cursor;
        S.top = res.state.top;
        if (S.count)
            pc_format(S.focus_id, sizeof S.focus_id, "%s", S.rows[S.cursor].id);
        if (S.top != old_top) { /* paged: the windows again, in place, their highlights kept */
            u_char gradients[ROWS];
            for (int i = 0; i < ROWS; i++)
                gradients[i] = vs_battle_getMenuItem(ROW_BASE + i)->gradientState;
            place_all(0);
            for (int i = 0; i < ROWS; i++) {
                int from = i + (S.top - old_top);
                vs_battle_getMenuItem(ROW_BASE + i)->gradientState = from >= 0 && from < ROWS ? gradients[from] : 0;
            }
        }
        return;
    }
    if (repeat & PADLleft)
        step_value(-1);
    else if (repeat & PADLright)
        step_value(1);
}

/* The windows' text and state for the rows' live values (a greyed row, a header). */
static void refresh_items(void)
{
    for (int i = 0; i < visible(); i++) {
        pc_option_row r;
        vs_battle_menuItem_t* item = vs_battle_getMenuItem(ROW_BASE + i);
        if (item->state == 0 || !row_info(S.top + i, &r))
            continue;
        item->unselectable = !r.row.enabled || !selectable_kind(r.row.kind);
        item->selected = 0;
    }
}

/* ---- the protocol ---- */

int pc_options_entries(char** strings, int* types, int max)
{
    static u_short names[PC_OPTIONS_ENTRIES][16], helps[PC_OPTIONS_ENTRIES][48];
    int pages[PC_OPTIONS_ENTRIES], n = pc_option_pages_children(-1, pages, PC_OPTIONS_ENTRIES), i;
    if (n > max)
        n = max;
    if (n > PC_OPTIONS_ENTRIES)
        n = PC_OPTIONS_ENTRIES;
    for (i = 0; i < n; i++) {
        pc_option_page p;
        u_char g[TEXT];
        int k;
        memset(names[i], 0, sizeof names[i]);
        memset(helps[i], 0, sizeof helps[i]);
        if (!pc_option_pages_info(pages[i], &p))
            p.title = "?", p.help = "";
        k = encode(p.title, g, sizeof g - 3, style(PC_STYLE_UPPERCASE));
        k = fit(g, k, 120, p.title);
        k = k < 25 ? k : 25;
        memcpy(names[i], g, (size_t)k);
        ((u_char*)names[i])[k] = GLYPH_END;
        k = encode(p.help, g, sizeof g, 0);
        k = k < 90 ? k : 90;
        memcpy(helps[i], g, (size_t)k);
        ((u_char*)helps[i])[k] = GLYPH_END;
        strings[i * 2] = (char*)names[i];
        strings[i * 2 + 1] = (char*)helps[i];
        types[i] = 0;
    }
    S.entries_built = 1;
    return n;
}

void pc_options_open(int entry)
{
    int pages[PC_OPTIONS_ENTRIES], n = pc_option_pages_children(-1, pages, PC_OPTIONS_ENTRIES);
    pc_option_page p;
    memset(&S.rows, 0, sizeof S.rows);
    S.entry = entry;
    S.depth = 0;
    S.cursor = S.top = 0;
    S.focus_id[0] = 0;
    S.info_key[0] = 0;
    S.page_id[0] = 0;
    S.title_item = -1;
    for (int i = 10; i < 20; i++) /* the row MENU8 hoisted (vs_mainMenu_flyoutMenuRightAndHoistSelection) */
        if (vs_battle_getMenuItem(i)->state == 4)
            S.title_item = i;
    if (entry >= 0 && entry < n && pc_option_pages_info(pages[entry], &p))
        pc_format(S.page_id, sizeof S.page_id, "%s", p.id);
    S.state = S_ENTER;
    S.placed = -1;
}

int pc_options_exec(void)
{
    switch (S.state) {
    case S_ENTER: /* the entry hoisted, then the rows slide in one a frame */
    case S_SWAP:
        if (S.placed < 0) {
            if (!vs_mainmenu_ready())
                return 0;
            if (!rebuild()) {
                leave(-1);
                return 0;
            }
            S.placed = 0;
        }
        if (S.placed < visible()) {
            place(S.placed, 320, 1);
            S.placed++;
            draw_values(); /* (the values ride in with their windows) */
            return 0;
        }
        for (int i = S.placed; i < ROWS; i++)
            vs_battle_getMenuItem(ROW_BASE + i)->state = 0;
        S.state = S_PAGE;
        S.info_key[0] = 0;
        /* fallthrough */
    case S_PAGE:
        if (S.generation != pc_option_pages_generation() || S.style_generation != pc_option_style_generation()) {
            S.style_generation = pc_option_style_generation();
            if (!rebuild()) { /* the page went (its plugin unloaded): its parent, or the list */
                if (S.depth > 0)
                    go(NULL, 0);
                else
                    leave(-1);
                return 0;
            }
            place_all(0);
        }
        refresh_items();
        if (vs_mainmenu_ready()) {
            page_input();
            if (S.state != S_PAGE)
                return 0;
        }
        if (S.count)
            draw_cursor(ROW_BASE + S.cursor - S.top);
        draw_values();
        row_help();
        return 0;
    case S_FLY_WAIT:
        if (vs_mainmenu_ready())
            show_flyout();
        return 0;
    case S_FLY: {
        int selected = vs_mainmenu_getSelectedRow();
        if (selected == -1)
            return 0;
        *cursor_memory(FLY_ID * 2) = (char)S.fly_saved[0];
        *cursor_memory(FLY_ID * 2 + 1) = (char)S.fly_saved[1];
        if (selected == -3) { /* Triangle */
            leave(-2);
            return 0;
        }
        if (selected >= 0) {
            const Row* row = &S.rows[S.fly_row];
            pc_ui_settings_catalog_set(row->catalog, selected);
            if (pc_display_confirm_pending()) {
                vs_mainMenu_menuItemFlyoutRight(FLY_BASE); /* the choices go; the row stays as the title */
                for (int i = FLY_BASE + 1; i < 40; i++)
                    vs_mainMenu_menuItemFlyoutRight(i);
                confirm_begin();
                return 0;
            }
        }
        S.state = S_BACK;
        return 0;
    }
    case S_CONFIRM_WAIT:
        if (!pc_display_confirm_pending()) {
            S.state = S_BACK;
            return 0;
        }
        confirm_info();
        if (vs_mainmenu_ready()) {
            confirm_place();
            S.state = S_CONFIRM;
        }
        return 0;
    case S_CONFIRM: {
        u_int pressed = vs_main_buttonsPressed.all, repeat = (u_int)vs_main_buttonRepeat;
        if (!pc_display_confirm_pending()) { /* timed out (reverted), or kept elsewhere (RPC) */
            vs_battle_playMenuLeaveSfx();
            S.state = S_BACK;
            return 0;
        }
        confirm_info();
        if (vs_mainmenu_ready()) {
            if (pressed & PADRup) {
                pc_display_confirm_revert();
                vs_battle_playMenuLeaveSfx();
                leave(-2);
                return 0;
            }
            if (pressed & PADRright) {
                confirm_done(S.confirm_focus == 0);
                return 0;
            }
            if (pressed & PADRdown) {
                confirm_done(0);
                return 0;
            }
            if (repeat & (PADLup | PADLdown)) {
                S.confirm_focus ^= 1;
                vs_battle_playMenuChangeSfx();
            }
        }
        vs_battle_getMenuItem(FLY_BASE + (S.confirm_focus ^ 1))->selected = 0;
        draw_cursor(FLY_BASE + S.confirm_focus);
        return 0;
    }
    case S_BACK: /* the flyout or the confirm away, the rows back in */
        fly_out_rows();
        S.state = S_SWAP;
        S.placed = -1;
        S.info_key[0] = 0;
        return 0;
    case S_LEAVE:
        if (!vs_mainmenu_ready())
            return 0;
        S.state = S_NONE;
        S.entries_built = S.result == -1;
        return S.result;
    default:
        return -1;
    }
}

/* ---- RPC ---- */

static int json_text(char* out, int size, const char* text)
{
    int n = 0;
    if (size <= 0)
        return 0;
    out[n++] = '"';
    for (const char* p = text ? text : ""; *p && n < size - 3; p++) {
        if (*p == '"' || *p == '\\')
            out[n++] = '\\';
        if ((u_char)*p >= 32)
            out[n++] = *p;
    }
    out[n++] = '"';
    out[n] = 0;
    return n;
}

int pc_options_json(char* out, int size)
{
    static const char* const kinds[] = { "choice", "toggle", "slider", "action", "binding", "info", "header", "note",
        "link" };
    int n = 0, page = page_index(), i;
    const char* screen = STATE_NAMES[S.state];
    pc_option_page p;
#define PUT(...) (n += n < size ? pc_format(out + n, (size_t)(size - n), __VA_ARGS__) : 0)
#define TEXT_(t) (n += n < size ? json_text(out + n, size - n, (t)) : 0)
    if (S.state == S_FLY && vs_mainMenu_itemsListRow != 17) /* the game's flyout still sliding in */
        screen = "entering";
    if (S.state == S_NONE)
        /* MENU8's list is up: our entries built and its rows taking input (renderItemsList's text set) */
        screen = S.entries_built && (vs_battle_menuState.currentMenu & 0x3F) == 8 && vs_battle_menuItems
                && vs_mainmenu_getSelectedRow() == -1 && vs_mainMenu_itemsListRow == 17 /* (its input state) */
            ? "list"
            : "none";
    PUT("{\"screen\":\"%s\",\"ready\":%s,\"menu\":%d,\"page\":", screen, vs_battle_menuItems && vs_mainmenu_ready() ? "true" : "false",
        vs_battle_menuState.currentMenu);
    TEXT_(S.state != S_NONE ? S.page_id : "");
    PUT(",\"title\":");
    TEXT_(S.state != S_NONE && pc_option_pages_info(page, &p) ? p.title : "");
    PUT(",\"list_focus\":%d", !strcmp((char*)screen, "list") ? vs_mainMenu_getConfirmedRow() : -1);
    PUT(",\"depth\":%d,\"focus\":%d,\"focus_id\":", S.depth, S.state != S_NONE ? S.cursor : -1);
    TEXT_(S.state != S_NONE ? S.focus_id : "");
    PUT(",\"top\":%d,\"rows\":[", S.top);
    for (i = 0; S.state != S_NONE && page >= 0 && i < pc_option_page_rows(page, PC_UI_WHERE_CLASSIC); i++) {
        pc_option_row r;
        if (!pc_option_page_row(page, PC_UI_WHERE_CLASSIC, i, &r))
            continue;
        PUT("%s{\"id\":", i ? "," : "");
        TEXT_(r.row.id);
        PUT(",\"label\":");
        TEXT_(r.row.label);
        PUT(",\"kind\":\"%s\",\"value\":", r.row.kind >= 0 && r.row.kind <= PC_UI_SET_LINK ? kinds[r.row.kind] : "?");
        TEXT_(r.row.value);
        PUT(",\"enabled\":%s,\"index\":%d,\"count\":%d", r.row.enabled ? "true" : "false", r.row.index, r.row.count);
        if (r.row.kind == PC_UI_SET_SLIDER)
            PUT(",\"number\":%g", (double)r.row.number);
        if (r.row.reason[0]) {
            PUT(",\"reason\":");
            TEXT_(r.row.reason);
        }
        if (r.row.confirm)
            PUT(",\"confirm\":true");
        if (label_cut(&r))
            PUT(",\"cut\":true");
        PUT("}");
    }
    PUT("],\"entries\":[");
    {
        int pages[PC_OPTIONS_ENTRIES], k = pc_option_pages_children(-1, pages, PC_OPTIONS_ENTRIES);
        for (i = 0; i < k && i < PC_OPTIONS_ENTRIES; i++) {
            PUT("%s", i ? "," : "");
            TEXT_(pc_option_pages_info(pages[i], &p) ? p.id : "");
        }
    }
    PUT("],\"cut\":["); /* every page's labels the classic screen cuts (wider than their column) */
    {
        int first = 1;
        for (int pg = 0; pg < pc_option_pages_count(); pg++)
            for (i = 0; i < pc_option_page_rows(pg, PC_UI_WHERE_CLASSIC); i++) {
                pc_option_row r;
                if (!pc_option_page_row(pg, PC_UI_WHERE_CLASSIC, i, &r) || !label_cut(&r))
                    continue;
                PUT("%s", first ? "" : ",");
                TEXT_(r.row.label);
                first = 0;
            }
    }
    PUT("],\"confirm\":{\"pending\":%s,\"remaining\":%.2f,\"focus\":\"%s\",\"keys\":[",
        pc_display_confirm_pending() ? "true" : "false", pc_display_confirm_remaining(),
        S.state == S_CONFIRM ? (S.confirm_focus ? "revert" : "keep") : "");
    for (i = 0; i < pc_display_confirm_count(); i++) {
        PUT("%s", i ? "," : "");
        TEXT_(pc_display_confirm_key(i));
    }
    PUT("]},\"info\":");
    TEXT_(S.state != S_NONE ? S.info_key : "");
    PUT("}");
#undef PUT
#undef TEXT_
    return n < size ? n : size - 1;
}
