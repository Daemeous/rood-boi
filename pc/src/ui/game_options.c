/* Modern menus: the game's options for the System tab's pages (battle
 * builds; game_options.h). Each setter writes what MENU8's _optionsMenu
 * (src/MENU/MENU8.PRG/88.c) writes when that row's choice is confirmed, with
 * the same side effects:
 *   map              simpleMap = _simpleMapValues[choice] ({3, 1, 0})
 *   ability timing   bit 5 of the first settings byte (abilityTimingDisplay)
 *   weapon / armor   bits 6 / 7 (weaponStatChange / armorStatChange)
 *   cursor memory    cursorMemory 1 / 0
 *   information      information 1 / 0. MENU8 also re-inits its information
 *                    box (vs_mainMenu_initInformationBox, MAINMENU): that box
 *                    belongs to the game's own menu, which is not on screen
 *                    while the modern shell is, and the game re-inits it
 *                    whenever its menu opens, so nothing is called here.
 *   puzzle mode      bit 3 (unk0_0 & 8) clear = on, and
 *                    vs_main_stateFlags.puzzleMode = on; refused while
 *                    vs_main_stateFlags.puzzleModeDisabled (MENU8 greys it)
 *   sound            monoSound 0 / 1, then vs_main_setMonoSound(monoSound)
 *   vibration        vibrationOn 1 / 0 (the pad's actuators do nothing on PC:
 *                    reported unavailable, refused)
 * All of it is part of the game's save (vs_main_settings), not settings.cfg. */
#include "common.h"
#include "../../../src/SLUS_010.40/main.h"
#include "game_options.h"

static const char _mapValues[3] = { 3, 1, 0 };

static int _flagBit(int option)
{
    switch (option) {
    case PC_GAME_OPT_ABILITY_TIMING:
        return 0x20;
    case PC_GAME_OPT_WEAPON_STATUS:
        return 0x40;
    case PC_GAME_OPT_ARMOR_STATUS:
        return 0x80;
    }
    return 0;
}

int pc_game_option_get(int option)
{
    u_char flags = *(u_char*)&vs_main_settings;
    int i;

    switch (option) {
    case PC_GAME_OPT_MAP:
        for (i = 0; i < 3; ++i) {
            if (vs_main_settings.simpleMap == _mapValues[i]) {
                return i;
            }
        }
        return -1;
    case PC_GAME_OPT_ABILITY_TIMING:
    case PC_GAME_OPT_WEAPON_STATUS:
    case PC_GAME_OPT_ARMOR_STATUS:
        return (flags & _flagBit(option)) != 0;
    case PC_GAME_OPT_CURSOR_MEMORY:
        return vs_main_settings.cursorMemory != 0;
    case PC_GAME_OPT_INFORMATION:
        return vs_main_settings.information != 0;
    case PC_GAME_OPT_PUZZLE_MODE:
        return (flags & 8) == 0;
    case PC_GAME_OPT_SOUND:
        return vs_main_settings.monoSound != 0;
    case PC_GAME_OPT_VIBRATION:
        return vs_main_settings.vibrationOn != 0;
    }
    return -1;
}

const char* pc_game_option_unavailable(int option)
{
    if (option == PC_GAME_OPT_PUZZLE_MODE && vs_main_stateFlags.puzzleModeDisabled != 0) {
        return "Cannot change in the middle of a puzzle";
    }
    if (option == PC_GAME_OPT_VIBRATION) {
        return "No effect on PC yet";
    }
    return NULL;
}

int pc_game_option_set(int option, int choice)
{
    u_char* flags = (u_char*)&vs_main_settings;
    int count = option == PC_GAME_OPT_MAP ? 3 : 2;

    if (option < 0 || option >= PC_GAME_OPT_COUNT || choice < 0 || choice >= count) {
        return 0;
    }
    if (pc_game_option_unavailable(option) != NULL || pc_game_option_get(option) == choice) {
        return 0;
    }
    switch (option) {
    case PC_GAME_OPT_MAP:
        vs_main_settings.simpleMap = _mapValues[choice];
        break;
    case PC_GAME_OPT_ABILITY_TIMING:
    case PC_GAME_OPT_WEAPON_STATUS:
    case PC_GAME_OPT_ARMOR_STATUS:
        *flags = (*flags & ~_flagBit(option)) | (choice ? _flagBit(option) : 0);
        break;
    case PC_GAME_OPT_CURSOR_MEMORY:
        vs_main_settings.cursorMemory = choice;
        break;
    case PC_GAME_OPT_INFORMATION:
        vs_main_settings.information = choice;
        break;
    case PC_GAME_OPT_PUZZLE_MODE:
        *flags = (*flags & ~8) | (choice ? 0 : 8);
        vs_main_stateFlags.puzzleMode = choice;
        break;
    case PC_GAME_OPT_SOUND:
        vs_main_settings.monoSound = choice;
        vs_main_setMonoSound(vs_main_settings.monoSound);
        break;
    case PC_GAME_OPT_VIBRATION:
        vs_main_settings.vibrationOn = choice;
        break;
    }
    return 1;
}
