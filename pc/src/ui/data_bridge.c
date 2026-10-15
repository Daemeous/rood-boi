/* Modern menus, the native Data screen: the game side (battle builds;
 * data_frame.h).
 *
 * The shell's System > Data row delivers command 7 as before: MAINMENU runs
 * MENU7 (vs_menu7_dataMenu) and MENU7 stays the only state machine: its pad,
 * decisions, sounds, card I/O, cursor memories and every write to the game's
 * settings are the original's. With ui_menus modern and the overlay up the
 * run is framed (260.c, ROOD_PC):
 *   - MENU7 draws nothing (its GPU and text output gated, its logic running);
 *   - its Save / Load chooser (states 5 and 6, a MAINMENU row list) is this
 *     file's list (pc_ui_data_frame_op, in getSelectedRow() + 1's protocol);
 *   - the frame's clicks are goals (ui_data.cpp's mailbox) the planner
 *     (data_plan.c) walks with one pad press a frame while the real pad is
 *     idle (pc_ui_data_frame_inject);
 *   - MENU7's statics are published after every call (_pcPublish) into the
 *     model the frame draws (ui_data_draw.cpp).
 *
 * Lifecycle (data_frame.h's model):
 *   off      nothing armed or framed
 *   armed    menu_bridge.c deliver(7): the shell hid for Data; the frame
 *            draws "Reading memory card" until MENU7's first stamp (30
 *            vsyncs at most); MAINMENU keeps the DATA title off screen
 *   framed   vs_menu7_dataMenu state 3 latched this run; the frame draws
 *            while its stamp is at most 8 vsyncs old
 *   end      Back (MENU7 returns 1 from state 13): func_800FAA5C reopens the
 *            top level, pc_ui_menu_open calls pc_ui_data_end (a pending tab
 *            from a tab click or L1 / R1 goes back to the shell); Close
 *            (state 14): currentMenu 7 then 0x7F, pc_ui_menu_heartbeat ends
 *            it; a load: 260.c finishes the frame before the loading screen
 *            (PC_UI_DATA_LOADED), pc_ui_menu_reset forgets it again.
 *
 * The frame writes no game RAM itself: what this file's chooser does is
 * MENU7's state 6's protocol, and the planner's presses are pad words MENU7
 * reads as the player's. Nothing here keeps a pointer into MENU7's heap
 * (MCMAN, the scanned files): the publish is decoded at once. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "ui_shell.h"
#include "data_frame.h"
#include "data_plan.h"
#include <libetc.h>

extern unsigned pc_vsync_count(void) __attribute__((weak));
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */
unsigned pc_ui_map_buttons(unsigned pad); /* menu_bridge.c: libetc PAD bits to PC_UI_* */
int pc_ui_shell_step_tab(int dir);        /* ui_shell.cpp */
int pc_room_warp_blocks_saving(void) __attribute__((weak)); /* room_warp.c: the card refuses writes */

static int pending_tab = -1; /* a tab click (or L1 / R1): the shell reopens there */
static int ops_first;        /* the chooser's first frame: nothing taken from the pad */
static int ops_row_set;      /* the chooser's row was placed this run */
static int last_card;        /* the card rows' cursor last seen (1, 2) */
static int card_memory = 1;  /* where MENU7 starts its card rows (the auto-advance's target) */
static int call_input;       /* this call's pad (real or injected) was not idle: its publish settles */

static unsigned now(void) { return pc_vsync_count ? pc_vsync_count() : 0; }

static void zero(void* p, unsigned n)
{
    unsigned char* b = p;
    while (n--)
        *b++ = 0;
}

static void decode(const unsigned char* text, int max_bytes, char* out, int size)
{
    out[0] = 0;
    if (text)
        pc_ui_text_decode(text, max_bytes, out, size);
}

/* vs_battle_getMapCompletion (146C.c) on a copy: its roomFlags[1] fix-up
 * stays out of the game's RAM (as map_bridge.c's). */
static int completion(void)
{
    int flags[16];
    int i, j, value = 0;
    for (i = 0; i < 16; ++i)
        flags[i] = vs_main_mapStatus.roomFlags[i];
    if (flags[1] & 0x800000)
        flags[1] |= 0x400000;
    for (i = 0; i < 16; ++i)
        for (j = 0; j < 32; ++j)
            if (flags[i] & vs_battle_mapCompletionFlags[i] & (1 << j))
                ++value;
    return value * 100 / 361;
}

/* This journey (read only): what a save now would record. */
static void fill_journey(pc_ui_data_model* m)
{
    m->hp = D_80060068.unk0.currentHP;
    m->hp_max = D_80060068.unk0.maxHP;
    m->mp = D_80060068.unk0.currentMP;
    m->mp_max = D_80060068.unk0.maxMP;
    m->map = completion();
    m->saves = vs_main_settings.cumulativeSaveCount;
    m->play_h = vs_main_gametime.t.h;
    m->play_m = vs_main_gametime.t.m;
    m->play_s = vs_main_gametime.t.s;
    m->warp_blocked = pc_room_warp_blocks_saving && pc_room_warp_blocks_saving();
}

static void begin_model(pc_ui_data_model* m)
{
    zero(m, sizeof *m);
    m->card_status[0] = m->card_status[1] = -1;
    m->card = m->file = m->cancelled = -1;
    pending_tab = -1;
    ops_first = ops_row_set = last_card = 0;
    pc_ui_data_reset_mailbox();
}

/* menu_bridge.c deliver(7): the shell hid for Data. */
void pc_ui_data_arm(void)
{
    pc_ui_data_model* m = pc_ui_data_state();
    begin_model(m);
    m->armed = 1;
    m->armed_vsync = now();
    m->phase = PC_UI_DATA_PH_BUSY;
    fill_journey(m);
}

int pc_ui_data_armed(void) { return pc_ui_data_state()->armed; }

int pc_ui_data_on(void)
{
    pc_ui_data_model* m = pc_ui_data_state();
    return m->armed || m->framed;
}

int pc_ui_data_end(void)
{
    int t = pending_tab;
    pc_ui_data_model* m = pc_ui_data_state();
    if (!m->armed && !m->framed)
        return -1;
    begin_model(m);
    return t;
}

void pc_ui_data_forget(void) { pc_ui_data_end(); }

int pc_ui_data_busy(void)
{
    pc_ui_data_model* m = pc_ui_data_state();
    return m->framed && !m->finished && m->state >= 7 && m->state <= 10 && m->phase == PC_UI_DATA_PH_BUSY;
}

int pc_ui_data_frame_begin(int run)
{
    pc_ui_data_model* m = pc_ui_data_state();
    if (!m->armed)
        return 0;
    m->armed = 0;
    m->framed = 1;
    m->run = run;
    m->stamped = 1;
    m->stamp = now();
    m->state = 3;
    m->phase = PC_UI_DATA_PH_BUSY;
    return 1;
}

int pc_ui_data_framed(int run)
{
    pc_ui_data_model* m = pc_ui_data_state();
    return m->framed && m->run == run;
}

void pc_ui_data_frame_stamp(int state)
{
    pc_ui_data_model* m = pc_ui_data_state();
    if (!m->framed || m->finished)
        return;
    m->stamped = 1;
    m->stamp = now();
    m->state = state;
}

void pc_ui_data_frame_finish(int code)
{
    pc_ui_data_model* m = pc_ui_data_state();
    if (!m->framed)
        return;
    if (code == PC_UI_DATA_LOADED) {
        /* the jump's loading screen comes next: nothing of the frame over it */
        begin_model(m);
        return;
    }
    m->finished = 1;
    m->finish_code = code;
    m->ops_open = 0;
    pc_ui_data_reset_mailbox();
}

static unsigned pad_bits(unsigned press)
{
    unsigned pad = 0;
    if (press & PC_UI_DATA_PRESS_UP)
        pad |= PADLup;
    if (press & PC_UI_DATA_PRESS_DOWN)
        pad |= PADLdown;
    if (press & PC_UI_DATA_PRESS_CIRCLE)
        pad |= PADRright;
    if (press & PC_UI_DATA_PRESS_CROSS)
        pad |= PADRdown;
    return pad;
}

void pc_ui_data_frame_inject(unsigned* pressed, int* repeat)
{
    pc_ui_data_model* m = pc_ui_data_state();
    pc_ui_data_goal* g = pc_ui_data_goal_slot();
    pc_ui_data_snap s;
    unsigned press = 0, real = pc_ui_map_buttons(*pressed);
    int r;
    if (!m->framed || m->finished)
        return;
    s.phase = m->published ? m->phase : PC_UI_DATA_PH_BUSY;
    s.card = m->card;
    s.file = m->file;
    s.cancelled = m->cancelled;
    s.real = *pressed != 0 || *repeat != 0;
    s.settling = m->settling;
    r = pc_ui_data_plan(&s, g, &press);
    if (r == PC_UI_DATA_PLAN_DROPPED && !s.real)
        ++m->expired;
    /* L1 / R1 in the card rows or the file list (MENU7 ignores them): back
     * out to the chooser, then that tab */
    if ((real & (PC_UI_TAB_PREV | PC_UI_TAB_NEXT))
        && (s.phase == PC_UI_DATA_PH_CARD || s.phase == PC_UI_DATA_PH_FILES)) {
        zero(g, sizeof *g);
        g->kind = PC_UI_DATA_GOAL_TAB;
        g->a = pc_ui_shell_step_tab(real & PC_UI_TAB_NEXT ? 1 : -1);
        g->cool = 1; /* this frame is the player's */
    }
    /* the card rows: MENU7 swaps them on Up / Down only; Left / Right swap
     * them too (4:3 draws the two cards side by side in the header) */
    if (s.phase == PC_UI_DATA_PH_CARD && !(*repeat & (PADLup | PADLdown)) && (*repeat & (PADLleft | PADLright)))
        *repeat |= PADLup;
    if (r == PC_UI_DATA_PLAN_PRESS && !s.real) {
        *pressed = pad_bits(press);
        *repeat = (int)pad_bits(press);
        ++m->presses;
    }
    if (!s.real && g->kind)
        ++m->injected_frames;
    call_input = *pressed != 0 || *repeat != 0;
}

void pc_ui_data_frame_ops(int save_ok, int at_point, int in_battle)
{
    pc_ui_data_model* m = pc_ui_data_state();
    if (!m->framed)
        return;
    m->save_ok = save_ok;
    m->at_point = at_point;
    m->in_battle = in_battle;
    m->ops_open = 1;
    ops_first = 1;
    if (!ops_row_set) { /* a new run: Save when it can, else Load; back from a flow: the row it left */
        m->ops_row = save_ok && !m->warp_blocked ? 0 : 1;
        ops_row_set = 1;
    }
}

static int can_save(const pc_ui_data_model* m) { return m->save_ok && !m->warp_blocked; }

/* The goal for the card rows after a click on Save or Load: MENU7's own
 * start row (the auto-advance; a message or a prompt on the way stops it). */
static void advance(void)
{
    pc_ui_data_goal* g = pc_ui_data_goal_slot();
    zero(g, sizeof *g);
    g->kind = PC_UI_DATA_GOAL_CARD;
    g->a = card_memory;
    g->automatic = 1;
}

int pc_ui_data_frame_op(void)
{
    pc_ui_data_model* m = pc_ui_data_state();
    pc_ui_data_goal* g = pc_ui_data_goal_slot();
    unsigned pressed = pc_ui_map_buttons(vs_main_buttonsPressed.all);
    unsigned repeat = pc_ui_map_buttons(vs_main_buttonRepeat) | pressed;
    int row = m->ops_row;
    if (!m->framed || m->finished)
        return 0;
    m->ops_open = 1;
    if (ops_first) { /* the first pass: the player picks (a click already posted counts) */
        ops_first = 0;
        pressed = repeat = 0;
    }
    /* the frame's goal (the planner left it for this list) */
    if (g->kind == PC_UI_DATA_GOAL_OP || g->kind == PC_UI_DATA_GOAL_BACK || g->kind == PC_UI_DATA_GOAL_CLOSE
        || g->kind == PC_UI_DATA_GOAL_TAB) {
        int kind = g->kind, a = g->a;
        zero(g, sizeof *g);
        if (kind == PC_UI_DATA_GOAL_OP) {
            if (a != 1 && a != 2)
                return 0;
            m->ops_row = a - 1;
            if (a == 1 && !can_save(m)) {
                vs_battle_playInvalidSfx();
                return 0;
            }
            vs_battle_playMenuSelectSfx();
            m->ops_open = 0;
            advance();
            m->op = a;
            return a;
        }
        vs_battle_playMenuLeaveSfx();
        m->ops_open = 0;
        if (kind == PC_UI_DATA_GOAL_TAB) {
            pending_tab = a;
            return -1;
        }
        return kind == PC_UI_DATA_GOAL_CLOSE ? -2 : -1;
    }
    if (pressed & (PC_UI_TAB_PREV | PC_UI_TAB_NEXT)) {
        vs_battle_playMenuLeaveSfx();
        pending_tab = pc_ui_shell_step_tab(pressed & PC_UI_TAB_NEXT ? 1 : -1);
        m->ops_open = 0;
        return -1;
    }
    if (pressed & PC_UI_EXIT) { /* Triangle: the menu closes */
        vs_battle_playMenuLeaveSfx();
        m->ops_open = 0;
        return -2;
    }
    if (pressed & PC_UI_BACK) {
        vs_battle_playMenuLeaveSfx();
        m->ops_open = 0;
        return -1;
    }
    if (pressed & PC_UI_CONFIRM) {
        if (row == 0 && !can_save(m)) {
            vs_battle_playInvalidSfx();
            return 0;
        }
        vs_battle_playMenuSelectSfx();
        m->ops_open = 0;
        m->op = row + 1;
        return row + 1;
    }
    if (repeat & (PC_UI_UP | PC_UI_DOWN | PC_UI_LEFT | PC_UI_RIGHT)) {
        m->ops_row = 1 - row;
        vs_battle_playMenuChangeSfx();
    }
    return 0;
}

void pc_ui_data_frame_publish(const pc_ui_data_game* g)
{
    pc_ui_data_model* m = pc_ui_data_state();
    int i, saves;
    if (!m->framed || m->finished)
        return;
    m->published = 1;
    m->state = g->state;
    m->op = g->state == 7 || g->state == 8 ? 1 : g->state == 9 || g->state == 10 ? 2 : 0;
    m->data_not_saved = g->data_not_saved;
    m->container_empty = g->container_empty;
    m->cursor_xy = g->cursor_xy;
    m->elements_active = g->elements_active;
    m->list_up = g->list_up;
    m->page = g->list_up ? (72 - g->list_y0) / 40 : 0;
    /* handleInput lays elements 5..9 out from the page before it reads Up /
     * Down: a press that pages the list publishes the new cursor over the old
     * page. The planner waits this publish out (the file reads one behind). */
    m->settling = call_input;
    call_input = 0;
    pc_ui_data_decode(g->cursor_xy, g->list_y0, &m->card, &m->file, &m->cancelled);
    m->msg = g->msg;
    m->msg_off = g->msg_off;
    decode(g->msg_text, PC_UI_DATA_TEXT, m->msg_text, sizeof m->msg_text);
    m->progress_counter = g->progress_counter;
    m->progress_x = g->progress_x;
    m->progress_target = g->progress_target;
    m->fade = g->fade;
    m->phase = pc_ui_data_phase(g->state, g->elements_active, g->progress_counter, g->fade, g->cursor_xy, g->msg);
    m->prompt = m->phase == PC_UI_DATA_PH_PROMPT ? pc_ui_data_msg_prompt(g->msg) : PC_UI_DATA_PROMPT_NONE;
    m->gated += (unsigned)g->gated;
    card_memory = g->card_memory == 2 ? 2 : 1;
    m->location = g->location;
    decode(g->location_name, 64, m->location_name, sizeof m->location_name);
    if (g->state == 6)
        m->card_sel = 0;
    /* the card MENU7 checks: the card rows' cursor when "checking" shows */
    if (m->phase == PC_UI_DATA_PH_CARD) {
        last_card = m->card;
        m->card_sel = 0;
    }
    if (g->msg == PC_UI_DATA_MSG_CHECKING && last_card)
        m->card_sel = last_card;
    m->files_valid = g->files_valid;
    if (g->files_valid) {
        saves = 0;
        for (i = 0; i < PC_UI_DATA_FILES; ++i) {
            const pc_ui_data_file_game* s = &g->files[i];
            pc_ui_data_file* f = &m->files[i];
            f->state = s->state;
            f->generation = s->generation;
            f->h = s->h, f->m = s->m, f->s = s->s;
            f->saves = s->saves;
            f->hp = s->hp, f->hp_max = s->hp_max, f->mp = s->mp, f->mp_max = s->mp_max;
            f->location = s->location, f->clear = s->clear, f->map = s->map;
            f->current = g->current == i + 1;
            f->newest = s->state >= 3 && g->newest == i;
            decode(s->name, 64, f->name, sizeof f->name);
            saves += s->state >= 3;
        }
        m->current = g->current;
        m->newest = g->newest;
        if (m->card_sel)
            m->card_status[m->card_sel - 1] = saves;
    } else {
        zero(m->files, sizeof m->files);
        m->current = 0;
        m->newest = 0;
    }
    if (m->card_sel && g->msg == PC_UI_DATA_MSG_INSERT_ERROR)
        m->card_status[m->card_sel - 1] = -2;
    if (m->card_sel && g->msg == PC_UI_DATA_MSG_NO_DATA && !g->files_valid)
        m->card_status[m->card_sel - 1] = 0;
    if (g->state != 6)
        m->ops_open = 0;
    fill_journey(m);
}
