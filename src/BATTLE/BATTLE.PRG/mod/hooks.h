/* Where the Callo Merlose companion mod hooks into the game (decomp edition: `make CALLO=1`,
 * which defines VS_CALLO). In a normal build these macros give back the original code, so the
 * game is unchanged; with CALLO=1 they call the mod (callo.c, merlose.h). */
#pragma once

#ifdef VS_CALLO
#define VS_CALLO_HOOK(original, hook) hook  /* call hook instead of original */
#define VS_CALLO_AFTER(hook, value) hook(value) /* pass a result through hook */
#define VS_CALLO_CALL(call) call
#else
#define VS_CALLO_HOOK(original, hook) original
#define VS_CALLO_AFTER(hook, value) value
#define VS_CALLO_CALL(call)
#endif

/* BATTLE.PRG grows into the padding before it on the disc, by up to this many sectors: up to
 * 0x800F9800 in RAM, where INITBTL.PRG and the effect overlays load. */
#define VS_CALLO_BATTLE_PRG_SECTORS 7

/* Permanent stat-up items always give their maximum (+4) */
#define VS_CALLO_STAT_ROLL 3

/* BATTLE.PRG */
void vs_mod_spawnCallo(void);
int vs_mod_frame(void);
void vs_mod_hud(void);
int vs_mod_countTeam(int team);
void vs_mod_cutscene(int on);
unsigned int vs_mod_targetStatus(int actor);

/* MENU3.PRG (ITEMS > OPTIONS: EQUIP and MERLOSE) */
extern unsigned char _menuMode;
int vs_mod_optionsRows(int rowCount, char** strings, int* rowTypes);
int vs_mod_itemNavigationDone(int row);
int vs_mod_equipMenuDone(int row);
int vs_mod_loadCalloObjectData(int category, int wepId, int material);

/* MENU0.PRG (MAGIC: her spell choice) */
void vs_mod_markSpells(int rowInfo, int* rowTypes);
void vs_mod_spellHighlighted(int id);
