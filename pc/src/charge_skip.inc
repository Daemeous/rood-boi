/* Battle charging skip: the game side (included in BATTLE's 146C.c, which
 * knows the action record and the camera; the host side, the settings and
 * what is presented: charge_skip.c).
 *
 * An action with an effect is committed in vs_battle_commitSelectedAction (costs paid, the
 * item used, hits, damage and statuses rolled, the score counted), which then
 * sets _cameraMode 4. The wind-up that follows decides nothing:
 *   4  _handleCombatCameraZoom: the camera converges on the caster and the
 *      target, then vs_battle_startActionPresentation starts the cast (its animation and a turn
 *      toward the aim over 10 or 20 vsyncs, the cast sound, the event flags
 *      0xB7 / 0xB9, the effect's request, an art's weapon or armour data, the HUD
 *      out, the name window if the Information option is on) and sets 5;
 *   5  func_80078748 holds: until the name window has been up for its 120
 *      ticks (func_800CB45C, 2 s), the cast animation's loop boundary, the
 *      object data queue (slot 0) and the effect's files (func_800CEEBC),
 *      then func_800704B0 sets 6;
 *   6  vs_battle_startReadyActionAnimation waits for the cast flash (D_800F1868 & 2), then
 *      vs_battle_startActionAnimation releases (7): the name window closed (func_800CB50C),
 *      and vs_effects_startEffectTrack spawns the effect, whose script applies the hits.
 * Skip (pc_charge_skip, called where the game runs phase 5) takes out the
 * hold and only that: phase 4 runs as it is (the camera moves to the caster
 * at its own pace, the name window opens the game's way), then 5 stops the
 * cast sound, applies the turn at once, gives the magic circle its end cue
 * at once, loads the effect's files back to back (charge_skip_load) and
 * releases as soon as they are in and the object data is placed. Phase 6 (the
 * circle gone by then) and everything else run as they are: about 4 game
 * frames in all, not presented (charge_skip.c). The name window would close
 * at that release, a flash: it is kept up past it instead (pc_charge_skip_hold
 * at the release, pc_charge_skip_name every frame) until it has been shown for
 * qol_skip_charging_name seconds, never longer than the original's own 120
 * ticks, and closed the game's way (func_800CB50C). Whether a vsync skips is
 * decided from the game's state every time (pc_charge_skip_state); only the
 * held window and the hold's start are kept between vsyncs.
 *
 * A wind-up may show some of its hold first (qol_skip_charging_hold_mine for
 * Ashley's actions, _hold_enemy for the others', seconds): phases 5 and 6 run
 * as they are, shown and paced, for that long from phase 5's first vsync, and
 * only then does Skip take out the rest. A hold the original ends sooner ends
 * the original's way (it is the game's own code that runs). Why: the defence
 * and chain input of an action (vs_battle_updateBattleAbilityTiming) only runs in phase 7, from the
 * release; a press there before the hit's window opens is "FAST!" or
 * "TOO FAST!" and spends the action's one attempt. Off, the hold is seconds of
 * warning; skipped outright, an enemy's release followed the camera's arrival
 * at once, and a press meant for after its action (Circle for Ashley's own
 * attack) landed in it. The timing itself counts from the release
 * (battleAbilityTimer, zeroed by vs_battle_startActionAnimation) and only in phase 7, so the
 * skipped vsyncs take nothing from it. */

extern int pc_qol_skip_charging, pc_qol_skip_charging_who; /* project_settings.cpp */
extern float pc_qol_skip_charging_hold_mine, pc_qol_skip_charging_hold_enemy;
extern int pc_charge_skip_kinds;                           /* charge_skip.c */
extern unsigned pc_vsync_count(void);                      /* boot_runtime.c */
static int charge_skip_from_set __attribute__((used)); /* phase 5 has begun: its first vsync */
static unsigned charge_skip_from __attribute__((used));

/* The hold shown before Skip applies, vsyncs (qol_skip_charging_hold_mine /
 * _enemy: 0..1.5 s in the menus, by the caster; the tests' RPC goes past the
 * originals' own holds, up to 10 s). */
/* The kind of action `index` (RPC skip_charging kinds; vs_battle_startActionPresentation's
 * ranges): 1 a spell (below 0x8D), 2 an enemy's art (0x8D..0xB7), 4 a Break
 * Art (0xB8..0xDF, ten weapon categories of four), 8 an item (0xE0 on). */
/* Whether this vsync is in an action's wind-up (phase: _cameraMode 4..6),
 * and whether Skip takes it out (its hold: phases 5 and 6 of an action Skip
 * applies to; charge_skip.c, RPC skip_charging). */
/* The effect's files loaded now (RPC skip_charging pump, on by default).
 * The loader (5BF94.c vs_effects_updatePrimaryEffects: states 6 purge, 2 textures, 8 code,
 * 3 data, then 5 ready) takes one step a game frame, and each file it asks
 * for comes from the CD queue over the next two vertical blanks: about 20
 * frames, though the host reads the disc at once. Here its steps run back
 * to back, each file read when asked for (main.c pc_cd_queue_load, its own
 * queue slot vs_effects_resourceReadSlot only), until it is ready: the same steps in the
 * same order, without the frames between. */
int pc_charge_skip_pump = 1;
int pc_charge_skip_pumped; /* loader steps run so last time (RPC diagnostics) */
extern u_int vs_effects_primaryPhase;
extern vs_main_CdQueueSlot* vs_effects_resourceReadSlot;
int vs_effects_updatePrimaryEffects(void);
void vs_effects_selectEffectResourceContext(int);
int pc_cd_queue_load(vs_main_CdQueueSlot* slot); /* main.c */
/* Called where the game runs _cameraMode `phase` of the action state; 1:
 * done here (the game's own step is not run). Phase 4, the camera's move
 * to the caster and the cast's start, is the game's own. */
/* The name window kept up past a skipped wind-up's release (settings
 * qol_skip_charging_name, seconds shown at least; 0: closed at the release
 * as the original does). vs_menu_sceneNotificationBuffer is its text (vs_battle_displaySceneMessage
 * allocates it, func_800CB50C dismisses text box 7 and frees it). */
extern float pc_qol_skip_charging_name; /* project_settings.cpp */
extern int* vs_menu_sceneNotificationBuffer;
extern char vs_menu_sceneNotificationTimer;
static int charge_skip_held __attribute__((used));          /* the window is held */
static unsigned charge_skip_held_at __attribute__((used));  /* the vsync of the release */
static int charge_skip_held_vsyncs __attribute__((used));   /* how long it stays up from there */

/* At the release (vs_battle_startActionAnimation): 1 if the name window stays up (the game
 * does not close it there). */
/* Every game frame (vs_battle_exec), and before the window is opened again
 * (vs_battle_displaySceneMessage, `now` 1): the held window closed, the
 * game's way, once it has been up long enough and its text is out, when the
 * action is over, or now. */
/* Test aid (RPC skip_charging): whether the name window is up (its text
 * allocated and text box 7 open), its text (up to `size` bytes, the game's
 * encoding) and whether it is held. */
/* Test aids (RPC skip_charging; charge_skip.c). The view and the caster's
 * facing at the release's first vsync: lookAt xyz, position xyz, the
 * camera's distance and yaw, the facing. */
/* BATTLE's state (D_800F196C), its camera mode and in the action state the
 * actor and action, for the random number pin and the actions' log. */
/* The random numbers (main.c vs_main_getRand) from `seed`, as _initRand
 * seeds them at boot. */
extern int randArr[97];
extern int randIndex;
/* Diagnostics (RPC skip_charging trace): this vsync's _cameraMode, the effect
 * loader's state, object slot 0 in use, D_800F1868 and the background
 * effects' ids and kinds. */
extern u_int vs_effects_primaryPhase;
extern D_800F53B8_t* vs_effects_secondaryInstances;
/* The chain / defence ability window of the action under way (0: none open;
 * vs_battle_armDefenseAbilityTimingWindow opens it: battleAbilityTargetWindow), for the tests. */
/* The chain / defence input of the action under way (vs_battle_updateBattleAbilityTiming), for the
 * tests: out[0] its timer (battleAbilityTimer, ticks from the release), [1]
 * the window (battleAbilityTargetWindow; 0 none yet), [2] whether the one
 * attempt is spent, [3] its success (1 chain, 2 defence). 0: no action. */

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
int pc_charge_skip_state(int* phase, int* skipping, int* actor, int* action);
int charge_skip_now(void);

/* Declared for the Zig's c module (the C bodies that declared them are gone). */
void charge_skip_load(void);
int charge_skip_kind(int index);
int charge_skip_hold_vsyncs(int caster);
