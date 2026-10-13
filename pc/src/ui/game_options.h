/* Modern menus: the game's own options (vs_main_settings, saved with the
 * game) read and written the way the game's Options screen (MENU8 88.c)
 * does. game_options.c, battle builds; ui_settings.cpp links these weakly
 * and leaves the rows out without them.
 *
 * Values are choice indices in the Options screen's own order:
 *   MAP              0 Frame, 1 Simple, 2 Off (simpleMap 3 / 1 / 0)
 *   ABILITY_TIMING   0 Off, 1 On (settings bit 5)
 *   WEAPON_STATUS    0 Off, 1 On (bit 6)
 *   ARMOR_STATUS     0 Off, 1 On (bit 7)
 *   CURSOR_MEMORY    0 Off, 1 On (cursorMemory)
 *   INFORMATION      0 Off, 1 On (information)
 *   PUZZLE_MODE      0 Off, 1 On (bit 3 clear = on; also sets
 *                    vs_main_stateFlags.puzzleMode; unavailable while
 *                    vs_main_stateFlags.puzzleModeDisabled)
 *   SOUND            0 Stereo, 1 Mono (monoSound; vs_main_setMonoSound)
 *   VIBRATION        0 Off, 1 On (vibrationOn; no effect on PC yet)
 * (On/Off options are listed On first in the game; here 1 is On.) */
#ifndef PC_UI_GAME_OPTIONS_H
#define PC_UI_GAME_OPTIONS_H
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_GAME_OPT_MAP, PC_GAME_OPT_ABILITY_TIMING, PC_GAME_OPT_WEAPON_STATUS, PC_GAME_OPT_ARMOR_STATUS,
    PC_GAME_OPT_CURSOR_MEMORY, PC_GAME_OPT_INFORMATION, PC_GAME_OPT_PUZZLE_MODE, PC_GAME_OPT_SOUND,
    PC_GAME_OPT_VIBRATION, PC_GAME_OPT_COUNT
};
/* The current choice (-1: unknown option, or a value the game never writes). */
int pc_game_option_get(int option);
/* Set a choice with the Options screen's side effects; 1 when it changed. */
int pc_game_option_set(int option, int choice);
/* Why the option cannot change now (NULL: it can). */
const char* pc_game_option_unavailable(int option);

#ifdef __cplusplus
}
#endif
#endif
