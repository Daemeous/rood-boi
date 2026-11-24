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

/* Permanent stat-up items always give their maximum (+4) in Merlose builds, unless
 * VS_CALLO_RANDOM_STAT_ITEMS keeps the original random +1 to +4, and with VS_MAX_STAT_ITEMS
 * (the extras without Merlose, rood-boi's build_boot.py --extras). Otherwise the original roll. */
#if (defined(VS_CALLO) && !defined(VS_CALLO_RANDOM_STAT_ITEMS)) || defined(VS_MAX_STAT_ITEMS)
#define VS_CALLO_STAT_ROLL 3
#else
#define VS_CALLO_STAT_ROLL vs_main_getRand(4)
#endif

#ifdef VS_CALLO
/* Declared only in the Merlose build. The battle code is Zig now (game/battle): each hooked
 * function tests for these in its unit's translated C (c_146C, c_4A0A8, c_40564) with
 * @hasDecl, so a plain build compiles the original calls and never references the mod. */
/* BATTLE.PRG */
void vs_mod_spawnCallo(void);
int vs_mod_frame(void);
void vs_mod_hud(void);
int vs_mod_countTeam(int team);
void vs_mod_cutscene(int on);
unsigned int vs_mod_targetStatus(int actor);
int vs_mod_animSlot(void* actor, int id);
int vs_mod_animSet(int slot, int category);

/* MENU3.PRG (ITEMS > OPTIONS: EQUIP and MERLOSE) */
extern unsigned char _menuMode;
int vs_mod_optionsRows(int rowCount, char** strings, int* rowTypes);
int vs_mod_itemNavigationDone(int row);
int vs_mod_equipMenuDone(int row);
int vs_mod_loadCalloObjectData(int category, int wepId, int material);

/* MENU0.PRG (MAGIC: her spell choice) */
void vs_mod_markSpells(int rowInfo, int* rowTypes);
void vs_mod_spellHighlighted(int id);
#endif /* VS_CALLO */

#if (defined(VS_CALLO) && !defined(VS_CALLO_RANDOM_STAT_ITEMS)) || defined(VS_MAX_STAT_ITEMS)
/* Never defined or called: its declaration tells the Zig stat-up effects
 * (game/battle/increase_*_rand.zig) to give +4 (VS_CALLO_STAT_ROLL above). */
void vs_mod_statItemsAlwaysMax(void);
#endif

#ifdef ROOD_PC
/* pc/src/enemy_difficulty.c: the enemy difficulty option, called by the Zig battle code
 * (vs_actors_clearRoomEnemyMemory, vs_actors_rememberEnemy, vs_actors_applyActorInitialStats,
 * vs_actors_spawnRoomEnemy) and INITBTL. */
void pc_difficulty_begin_scene(int completions);
void pc_difficulty_scene_now(void);
void pc_difficulty_store(void* record, const void* stats);
void pc_difficulty_init(int slot, const void* init, void* stats);
void pc_difficulty_restore(const void* record, int slot, const void* init, void* stats);
#endif
