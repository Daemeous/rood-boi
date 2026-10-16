/* Modern menus, the native Score screen: MENU9's side (src/MENU/MENU9.PRG/238.c,
 * ROOD_PC; ui_score.h has the design).
 *
 * MENU9.PRG is never loaded for the native screen. Its code and statics are
 * linked all the time, so score_bridge.c runs the original _calculateProgress
 * once per open (the writes on open: roomFlags[1], the cursor memory's bzero,
 * the scoredata high-water marks, the titles; _score and _characterRank) and
 * reads the results and MENU9's text tables through the one const table of
 * pointers below (238.c pc_ui_score_menu9_table: by address, no copies). The
 * game rules (score tables, rank thresholds, title conditions, reference
 * times) stay in 238.c.
 *
 * Included by 238.c (after its tables) and score_bridge.c. */
#ifndef PC_UI_SCORE_MENU9_H
#define PC_UI_SCORE_MENU9_H
#ifdef __cplusplus
extern "C" {
#endif

/* The counts the native screen relies on (238.c's tables and loops). */
enum { PC_UI_SCORE_N_TITLES = 32, PC_UI_SCORE_N_STATS = 23, PC_UI_SCORE_N_TRIALS = 8, PC_UI_SCORE_N_RANKS = 16,
       PC_UI_SCORE_N_SECTIONS = 4, PC_UI_SCORE_N_MISC = 6 };

typedef struct {
    int (*calculate)(void);                 /* _calculateProgress (returns 1) */
    const void* score;                      /* _score (u_long: 32 bits on both builds; the macro checks) */
    const unsigned short* rank;             /* _characterRank: 0 best .. 15 */
    const unsigned short* clear_count;      /* _clearCount (stateFlags.clearCount at the open) */
    const unsigned short* chests;           /* _openedChestCount (now) */
    const unsigned short* map;              /* _mapCompletion (now) */
    /* vsString tables (u16 offsets, then the strings): */
    const unsigned short* menu_text;        /* title, gazette, recordTime, encyclopaedia */
    const unsigned short* rank_text;        /* 16, index = _characterRank */
    const unsigned short* title_text;       /* 32 */
    const unsigned short* title_desc;       /* 32 */
    const unsigned short* stat_headers;     /* 23 */
    const unsigned short* stat_desc;        /* 23 */
    const unsigned short* time_attacks;     /* 8 */
    const unsigned short* time_attack_desc; /* 8 */
    const unsigned short* misc;             /* noneClear .. noneBossRushDesc */
    const char* const* reference_times;     /* 8 raw strings "$HH:MM:SS" */
} pc_ui_score_menu9;

extern const pc_ui_score_menu9 pc_ui_score_menu9_table; /* 238.c */

/* 238.c's check (one line there): the statics' widths and the tables' counts
 * (the build's generated index enums) as the native screen reads them. */
#define PC_UI_SCORE_MENU9_LAYOUT()                                                               \
    typedef char _pcScoreLayout[sizeof _score == 4 && sizeof _characterRank == 2                  \
                && sizeof _clearCount == 2 && sizeof _openedChestCount == 2                       \
                && sizeof _mapCompletion == 2 && VS_menuText_INDEX_encyclopaedia == 3             \
                && VS_rankText_INDEX_rank1 == PC_UI_SCORE_N_RANKS - 1                               \
                && VS_titleText_INDEX_title31 == PC_UI_SCORE_N_TITLES - 1                           \
                && VS_titleDescriptions_INDEX_title31 == PC_UI_SCORE_N_TITLES - 1                   \
                && VS_statHeaders_INDEX_unarmedUsage == PC_UI_SCORE_N_STATS - 1                     \
                && VS_statDescriptions_INDEX_unarmedUsage == PC_UI_SCORE_N_STATS - 1                \
                && VS_timeAttacks_INDEX_asura == PC_UI_SCORE_N_TRIALS - 1                           \
                && VS_timeAttackDescriptions_INDEX_asura == PC_UI_SCORE_N_TRIALS - 1                \
                && VS_miscInfo_INDEX_noneBossRushDesc == PC_UI_SCORE_N_MISC - 1                     \
                && sizeof _timeAttackReferenceTimes / sizeof _timeAttackReferenceTimes[0]         \
                    == PC_UI_SCORE_N_TRIALS                                                         \
            ? 1                                                                                   \
            : -1]

#ifdef __cplusplus
}
#endif
#endif
