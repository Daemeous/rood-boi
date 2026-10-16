/* Modern menus, the native Score screen: its numbers as MENU9 prints them
 * (src/MENU/MENU9.PRG/238.c: _renderBossRushDetails, _printFastestClearTime,
 * _renderBossRushRecords, _setTitleRows), as plain text. Pure C: no game
 * headers (score_format_probe tests it; score_bridge.c fills the screen with it). */
#ifndef PC_UI_SCORE_FORMAT_H
#define PC_UI_SCORE_FORMAT_H
#ifdef __cplusplus
extern "C" {
#endif

/* The Records page's inputs: MENU9's statics after _calculateProgress and
 * vs_main_scoredata's fields. */
typedef struct {
    unsigned clear_now;      /* _clearCount (u_short) */
    unsigned rounds;         /* scoredata.clearCount */
    unsigned minutes;        /* scoredata.completionTimeMinutes */
    unsigned chests_now;     /* _openedChestCount (u_short) */
    unsigned chests_best;    /* scoredata.openedChestCount */
    unsigned map_now;        /* _mapCompletion (u_short) */
    unsigned map_best;       /* scoredata.mapCompletion */
    unsigned max_chain;      /* scoredata.maxChain */
    unsigned kills[6];       /* scoredata.enemyKills: human, beast, undead, phantom, dragon, evil */
    unsigned attacks[10];    /* scoredata.weaponAttacks: 0 unarmed, 1..9 dagger .. crossbow */
} pc_score_inputs;

enum { PC_SCORE_CHESTS = 52, PC_SCORE_MAP = 361, PC_SCORE_SENTINEL = 0x800000 };

/* Row `row` (0..22) of the Records page into out (always terminated). Returns
 * 1 when the row shows MENU9's noneClear text instead (the fastest clear with
 * no clear: out is ""), 0 otherwise, -1 for a row out of range. */
int pc_score_record_text(int row, const pc_score_inputs* in, char* out, int size);
/* "R<rounds+1> HH:MM" (_printFastestClearTime: minutes / 60, % 60; 99:59 at
 * 6000 minutes and over), as the original's int arithmetic gives it. */
void pc_score_fastest_clear(unsigned rounds, unsigned minutes, char* out, int size);
/* n * 100 / total as the original computes it (u_short statics as int, the
 * scoredata u_int as unsigned 32-bit). */
int pc_score_percent_short(unsigned n, int total);
unsigned pc_score_percent_u32(unsigned n, unsigned total);
/* A time attack's packed time (vs_trialScore_t time:24): "HH:MM:SS" from
 * (t >> 16, t >> 8 & 0xFF, t & 0xFF); "--:--:--" for the 0x800000 sentinel.
 * Returns 0 for the sentinel, 1 otherwise. */
int pc_score_trial_time(unsigned time24, char* out, int size);
/* A reference time ("$01:15:00" raw) without its '$': "01:15:00". */
void pc_score_reference_time(const char* raw, char* out, int size);
/* The title rows: unlocked[i] = bit i of the mask; returns how many are set. */
int pc_score_title_rows(unsigned mask, unsigned char unlocked[32]);
/* Titles 0..15 that count for the rank (_determineCharacterRank): set bits of mask & 0xFFFF. */
int pc_score_rank_titles(unsigned mask);
/* A number with thousands grouped: 999999999 -> "999,999,999". */
void pc_score_grouped(unsigned v, char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
