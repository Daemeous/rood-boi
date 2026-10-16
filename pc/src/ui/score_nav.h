/* Modern menus, the native Score screen: MENU9's list cursors
 * (src/MENU/MENU9.PRG/238.c _menuInput states 5, 8, 11, 14), ported as they
 * step vs_battle_menu9CursorMemory's (page, row) pairs. The focused entry is
 * page + row; the original shows rows page .. page + 7.
 *
 *   Up:   on a press at (0, 0): wrap to (max, 7); else on a repeat: row >= 2:
 *         row - 1; else page > 0: page - 1; else row > 0: row - 1
 *   Down: on a press at (max, 7): wrap to (0, 0); else on a repeat: row >= 6:
 *         page + 1 while page < max, else row + 1 while row < 7; else row + 1
 *   Time attack: page = (page -/+ 1) & 7 on a repeat (row unused)
 * Pure C (score_nav_probe tests it against a transcription of 238.c). */
#ifndef PC_UI_SCORE_NAV_H
#define PC_UI_SCORE_NAV_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_SCORE_LIST_TITLES, PC_SCORE_LIST_RECORDS, PC_SCORE_LIST_TRIALS, PC_SCORE_LIST_BESTIARY,
       PC_SCORE_LISTS };

/* Entries of a list: 32, 23, 8, 78. */
int pc_score_nav_rows(int list);
/* The last page (0x18, 0x0F, 7 for the time attack's index, 0x46). */
int pc_score_nav_max_page(int list);
/* One tick: up / down held this frame (repeat, the press included) and
 * pressed (edge). Returns 1 when the cursor moved (the original plays its
 * move sound then), 0 otherwise. */
int pc_score_nav_step(int list, unsigned char* page, unsigned char* row, int up_repeat, int up_press,
    int down_repeat, int down_press);
/* The entry focused (page + row; the time attack's page). */
int pc_score_nav_index(int list, int page, int row);
/* The (page, row) Down presses from (0, 0) reach for entry `index` (the
 * pointer's pick: the same entry, its canonical pair). Out of range: clamped. */
void pc_score_nav_canon(int list, int index, unsigned char* page, unsigned char* row);

#ifdef __cplusplus
}
#endif
#endif
