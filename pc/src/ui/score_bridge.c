/* Modern menus, the native Score screen: the game side (battle builds; ui_score.h).
 *
 * The original (MENU9, src/MENU/MENU9.PRG/238.c) is never loaded for the native
 * screen; this file does to the game what its open and its cursors do, from the
 * shell's events (ui_score.cpp), drained after every shell tick (pc_ui_menu_top):
 *   OPEN      the world frozen (func_8008A4DC(0), vs_menu9_exec's initData; the
 *             value before it remembered and put back at CLOSE); MENU9's own
 *             _calculateProgress once (through 238.c's table, score_menu9.h:
 *             roomFlags[1], the cursor memory's bzero without cursor memory,
 *             the scoredata high-water marks, the titles, _score and
 *             _characterRank, byte for byte the original's); the snapshot
 *             (MENU9's text decoded, the Records formatted, MON.BIN parsed);
 *             the game's cursor on command 8 and the select sound (deliver's)
 *   CURSOR    vs_battle_menu9CursorMemory's pair of a list (the original's
 *             _menuInput writes it on every move)
 *   HOME      cursorMemories[2] = row, [3] = 0 (_setRows with 4 rows)
 *   GAZETTE   menu9CursorMemory.gazetteRow = cursorMemories[2] + [3] (the top
 *             menu's exits)
 *   SFX       vs_main_playSfxDefault(0x7E, n); MENU_SFX the main menu's list
 *   CLOSE     the world back as it was (a deviation: MENU9's state 6 calls
 *             func_8008A4DC(1) unconditionally; here only if it ran at OPEN,
 *             so a freeze another owner held stays its own; the plan's
 *             Deviations)
 *   ORIGINAL  nothing more: the cursor memory is written, the world back;
 *             deliver(9) runs MENU9, whose _calculateProgress runs again (it
 *             only raises what this one raised)
 * Not touched: MENU9's camera backup (the native screen never moves the
 * camera), its text box, information box and menu items, the HUD slide (the
 * shell's). The Bestiary's file is read into host memory once (score_mon.c). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "score_format.h"
#include "ui_score.h"

extern int D_800F190C; /* BATTLE 146C.c: the world runs (func_8008A4DC) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */
void pc_ui_score_mon_fill(const int kill_flags[PC_MON_FLAG_WORDS], pc_ui_score* s); /* score_mon.c */

void pc_ui_score_bridge_refresh(void);
void pc_ui_score_bridge_abandon(void);
void pc_ui_score_bridge_reset(void);

#define T (&pc_ui_score_menu9_table)
#define TEXT_MAX 512

static int owned;     /* OPEN applied, CLOSE not yet: the world is frozen by the screen */
static int world_was; /* D_800F190C before OPEN froze it */

/* String i of a vsString table (u16 offsets in u16 units, then the strings), decoded. */
static void text(const u_short* table, int i, char* out, int size)
{
    pc_ui_text_decode((const u_char*)&table[table[i]], TEXT_MAX, out, size);
}

/* A table holds at least n strings: its first offset (the table's own length) is n or more. */
static int holds(const u_short* table, int n) { return table && table[0] >= n && table[0] < 0x400; }

static void fill(void)
{
    pc_ui_score* s = pc_ui_score_model_edit();
    pc_score_inputs in;
    int i, j;
    vs_main_bzero(s, sizeof *s);
    s->tables_ok = holds(T->menu_text, 4) && holds(T->rank_text, PC_UI_SCORE_N_RANKS)
        && holds(T->title_text, PC_UI_SCORE_N_TITLES) && holds(T->title_desc, PC_UI_SCORE_N_TITLES)
        && holds(T->stat_headers, PC_UI_SCORE_N_STATS) && holds(T->stat_desc, PC_UI_SCORE_N_STATS)
        && holds(T->time_attacks, PC_UI_SCORE_N_TRIALS) && holds(T->time_attack_desc, PC_UI_SCORE_N_TRIALS)
        && holds(T->misc, PC_UI_SCORE_N_MISC);
    s->score = *(const u_int*)T->score;
    s->rank = *T->rank;
    s->titles = (u_int)vs_main_scoredata.titles;
    s->clears = *T->clear_count;
    s->map_now = *T->map;
    s->map_best = vs_main_scoredata.mapCompletion;
    s->chests_now = *T->chests;
    s->chests_best = vs_main_scoredata.openedChestCount;
    s->max_chain = vs_main_scoredata.maxChain;
    for (i = 0; i < 6; ++i)
        s->kills[i] = vs_main_scoredata.enemyKills[i];
    vs_main_memcpy(s->cursor, &vs_battle_menu9CursorMemory, sizeof s->cursor);
    if (s->tables_ok) {
        for (i = 0; i < 4; ++i)
            text(T->menu_text, i, s->section[i], sizeof s->section[i]);
        if (s->rank >= 0 && s->rank < PC_UI_SCORE_N_RANKS)
            text(T->rank_text, s->rank, s->rank_text, sizeof s->rank_text);
        for (i = 0; i < PC_UI_SCORE_N_TITLES; ++i) {
            text(T->title_text, i, s->title_name[i], sizeof s->title_name[i]);
            text(T->title_desc, i, s->title_desc[i], sizeof s->title_desc[i]);
        }
        for (i = 0; i < PC_UI_SCORE_N_STATS; ++i) {
            text(T->stat_headers, i, s->stat_name[i], sizeof s->stat_name[i]);
            text(T->stat_desc, i, s->stat_desc[i], sizeof s->stat_desc[i]);
        }
        for (i = 0; i < PC_UI_SCORE_N_TRIALS; ++i) {
            text(T->time_attacks, i, s->trial_name[i], sizeof s->trial_name[i]);
            text(T->time_attack_desc, i, s->trial_desc[i], sizeof s->trial_desc[i]);
        }
        for (i = 0; i < PC_UI_SCORE_N_MISC; ++i)
            text(T->misc, i, s->misc[i], sizeof s->misc[i]);
    }
    /* the Records page (_renderBossRushDetails' sources) */
    vs_main_bzero(&in, sizeof in);
    in.clear_now = s->clears;
    in.rounds = vs_main_scoredata.clearCount;
    in.minutes = vs_main_scoredata.completionTimeMinutes;
    in.chests_now = s->chests_now;
    in.chests_best = s->chests_best;
    in.map_now = s->map_now;
    in.map_best = s->map_best;
    in.max_chain = s->max_chain;
    for (i = 0; i < 6; ++i)
        in.kills[i] = vs_main_scoredata.enemyKills[i];
    for (i = 0; i < 10; ++i)
        in.attacks[i] = vs_main_scoredata.weaponAttacks[i];
    for (i = 0; i < PC_UI_SCORE_N_STATS; ++i)
        s->stat_none[i] = pc_score_record_text(i, &in, s->stat_value[i], sizeof s->stat_value[i]) == 1;
    /* the time attack (_printBossRushMenuRow, _renderBossRushRecords) */
    for (i = 0; i < PC_UI_SCORE_N_TRIALS; ++i) {
        s->trial_has[i] = vs_main_scoredata.bossTimeTrialScores[i][0].time != PC_SCORE_SENTINEL;
        for (j = 0; j < 3; ++j) {
            const vs_trialScore_t* t = &vs_main_scoredata.bossTimeTrialScores[i][j];
            s->trial_round[i][j]
                = pc_score_trial_time(t->time, s->trial_time[i][j], sizeof s->trial_time[i][j]) ? (int)t->round + 1 : 0;
        }
        pc_score_reference_time(T->reference_times[i], s->trial_ref[i], sizeof s->trial_ref[i]);
    }
    /* the Bestiary (_initEnemyList's rules over the host's copy of MON.BIN) */
    pc_ui_score_mon_fill(vs_main_scoredata.enemyKillFlags, s);
    s->valid = 1;
}

static void freeze(void)
{
    if (owned)
        return;
    world_was = D_800F190C;
    owned = 1;
    func_8008A4DC(0);
}

static void thaw(void)
{
    if (!owned)
        return;
    owned = 0;
    if (world_was)
        func_8008A4DC(1);
}

static void apply(const pc_ui_score_event* e)
{
    switch (e->kind) {
    case PC_UI_SEV_OPEN:
        freeze();
        T->calculate(); /* MENU9's own: the writes on open, _score and _characterRank */
        fill();
        vs_battle_submenuStates[0] = 8;
        vs_battle_playMenuSelectSfx();
        pc_ui_score_model_ready(); /* the focus from the cursor memory (queues HOME: drained below) */
        break;
    case PC_UI_SEV_CURSOR: {
        u_char* c = (u_char*)&vs_battle_menu9CursorMemory;
        /* titlePage / titleRow, recordTime*, gazettePage (the boss), encyclopaedia* */
        static const int page_at[4] = {0, 4, 6, 2}, row_at[4] = {1, 5, 6, 3};
        if (e->a < 0 || e->a > 3)
            break;
        c[page_at[e->a]] = (u_char)e->b;
        if (e->a != 2)
            c[row_at[e->a]] = (u_char)e->c;
        break;
    }
    case PC_UI_SEV_HOME:
        D_800F4EE8.cursorMemories[2] = (char)e->a;
        D_800F4EE8.cursorMemories[3] = 0;
        break;
    case PC_UI_SEV_GAZETTE:
        vs_battle_menu9CursorMemory.gazetteRow = D_800F4EE8.cursorMemories[2] + D_800F4EE8.cursorMemories[3];
        break;
    case PC_UI_SEV_SFX:
        vs_main_playSfxDefault(0x7E, e->a);
        break;
    case PC_UI_SEV_MENU_SFX:
        if (e->a == 0)
            vs_battle_playMenuLeaveSfx();
        else if (e->a == 1)
            vs_battle_playMenuSelectSfx();
        else
            vs_battle_playMenuChangeSfx();
        break;
    case PC_UI_SEV_CLOSE:
        thaw();
        break;
    default: /* ORIGINAL: everything is written; deliver(9) follows */
        break;
    }
}

static void drain(void)
{
    pc_ui_score_event e;
    while (pc_ui_score_take(&e))
        apply(&e);
}

/* After a drain the world is frozen exactly while the screen is on. `owned` is game state (saved and
 * restored with a snapshot) and the screen's mode host state (not): a snapshot taken on the screen and
 * resumed in a new process, or one taken outside it and loaded while it shows, would leave them apart. */
static void reconcile(void)
{
    int on = pc_ui_score_on();
    if (owned && !on)
        thaw();
    else if (!owned && on)
        freeze();
}

/* After the shell's tick: its events applied. */
void pc_ui_score_bridge_refresh(void)
{
    drain();
    reconcile();
}

/* The session ended without its close (menu_bridge.c abandon, after pc_ui_shell_close left the screen):
 * its store events and the world back. */
void pc_ui_score_bridge_abandon(void)
{
    pc_ui_score_leave();
    drain();
    thaw();
}

/* A soft reset: the game's memory is the next game's; nothing is written. */
void pc_ui_score_bridge_reset(void)
{
    pc_ui_score_forget();
    owned = 0;
}
