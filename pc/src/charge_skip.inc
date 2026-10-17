/* Battle charging skip: the game side (included in BATTLE's 146C.c, which
 * knows the action record and the camera; the host side, the settings and
 * what is presented: charge_skip.c).
 *
 * An action with an effect is committed in func_80070F28 (costs paid, the
 * item used, hits, damage and statuses rolled, the score counted), which then
 * sets _cameraMode 4. The wind-up that follows decides nothing:
 *   4  _handleCombatCameraZoom: the camera converges on the caster and the
 *      target, then func_8007138C starts the cast (its animation and a turn
 *      toward the aim over 10 or 20 vsyncs, the cast sound, the event flags
 *      0xB7 / 0xB9, the effect's request, an art's weapon or armour data, the HUD
 *      out, the name window if the Information option is on) and sets 5;
 *   5  func_80078748 holds: until the name window has been up for its 120
 *      ticks (func_800CB45C, 2 s), the cast animation's loop boundary, the
 *      object data queue (slot 0) and the effect's files (func_800CEEBC),
 *      then func_800704B0 sets 6;
 *   6  func_800787F0 waits for the cast flash (D_800F1868 & 2), then
 *      func_80070278 releases (7): the name window closed (func_800CB50C),
 *      and func_800CEF74 spawns the effect, whose script applies the hits.
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
 * held window is kept between vsyncs. */

extern int pc_qol_skip_charging, pc_qol_skip_charging_who; /* project_settings.cpp */
extern int pc_charge_skip_kinds;                           /* charge_skip.c */

/* The kind of action `index` (RPC skip_charging kinds; func_8007138C's
 * ranges): 1 a spell (below 0x8D), 2 an enemy's art (0x8D..0xB7), 4 a Break
 * Art (0xB8..0xDF, ten weapon categories of four), 8 an item (0xE0 on). */
static int charge_skip_kind(int index)
{
    return index < 0x8D ? 1 : index < 0xB8 ? 2 : index < 0xE0 ? 4 : 8;
}

/* Whether this vsync is in an action's wind-up (phase: _cameraMode 4..6),
 * and whether Skip takes it out (its hold: phases 5 and 6 of an action Skip
 * applies to; charge_skip.c, RPC skip_charging). */
int pc_charge_skip_state(int* phase, int* skipping, int* actor, int* action)
{
    D_800F19CC_t2* a;
    int caster;
    if (D_800F196C != 2 || D_800F19CC == NULL || _cameraMode < 4 || _cameraMode > 6)
        return 0;
    a = &D_800F19CC->unk854[D_800F19CC->unk0 & 3];
    if (a->actionIndex >= 256 || !vs_main_actions[a->actionIndex].unk2_0)
        return 0;
    caster = (u_char)a->unk4.unk0.targetActor;
    *phase = (int)_cameraMode;
    *actor = caster;
    *action = a->actionIndex;
    *skipping = pc_qol_skip_charging && (caster == 0 || pc_qol_skip_charging_who)
             && (pc_charge_skip_kinds & charge_skip_kind(a->actionIndex))
             && D_800F4C34 == 0 /* (not while an event script runs) */
             && _cameraMode >= 5;
    return 1;
}

static int charge_skip_now(void)
{
    int phase, skipping, actor, action;
    return pc_charge_skip_state(&phase, &skipping, &actor, &action) && skipping;
}

/* The effect's files loaded now (RPC skip_charging pump, on by default).
 * The loader (5BF94.c func_800CE9B0: states 6 purge, 2 textures, 8 code,
 * 3 data, then 5 ready) takes one step a game frame, and each file it asks
 * for comes from the CD queue over the next two vertical blanks: about 20
 * frames, though the host reads the disc at once. Here its steps run back
 * to back, each file read when asked for (main.c pc_cd_queue_load, its own
 * queue slot D_800F568C only), until it is ready: the same steps in the
 * same order, without the frames between. */
int pc_charge_skip_pump = 1;
int pc_charge_skip_pumped; /* loader steps run so last time (RPC diagnostics) */
extern u_int D_800F531C;
extern vs_main_CdQueueSlot* D_800F568C;
int func_800CE9B0(void);
void func_800D7890(int);
int pc_cd_queue_load(vs_main_CdQueueSlot* slot); /* main.c */
static void charge_skip_load(void)
{
    D_800F569C_t* context = D_800F569C;
    int i;
    for (i = 0; i < 64; ++i) {
        if (D_800F531C != 6 && D_800F531C != 2 && D_800F531C != 8 && D_800F531C != 3)
            break;
        func_800D7890(1); /* (as func_800CF060 runs it) */
        func_800CE9B0();
        pc_cd_queue_load(D_800F568C);
    }
    D_800F569C = context;
    pc_charge_skip_pumped = i;
}

/* Called where the game runs _cameraMode `phase` of the action state; 1:
 * done here (the game's own step is not run). Phase 4, the camera's move
 * to the caster and the cast's start, is the game's own. */
int pc_charge_skip(int phase)
{
    D_800F4538_t* actor;
    if (phase != 5 || !charge_skip_now())
        return 0;
    /* No cast sound (func_80070278 stops the same three at the release). */
    func_80045D64(0x7E, 0x34);
    func_80045D64(0x7E, 0x35);
    func_80045D64(0x7E, 0x36);
    /* The turn toward the aim (func_8009E2E0) at once: what the actor update
     * (44F14.c) would add over its remaining vsyncs. */
    actor = D_800F4538[(u_char)D_800F19CC->unk854[D_800F19CC->unk0 & 3].unk4.unk0.targetActor];
    if (actor != NULL && actor->unk0.unk18 != 0) {
        actor->unk0.unk24 = (actor->unk0.unk24 + actor->unk0.unk3C) & 0xFFF;
        actor->unk0.facing = (actor->unk0.facing + actor->unk0.unk3E) & 0xFFF;
        actor->unk0.unk28 = (actor->unk0.unk28 + actor->unk0.unk40) & 0xFFF;
        actor->unk0.unk3C = actor->unk0.unk3E = actor->unk0.unk40 = 0;
        actor->unk0.unk18 = 0;
    }
    /* The cast's magic circle (background effect 56, spawned with the
     * request) ends the way it does when phase 6 begins (func_800704B0
     * clears D_800F522C, its script's cue), but now: it fades while the
     * effect's files load, not after (phase 6 waits for it to be gone,
     * D_800F1868 & 2). */
    func_800CF920();
    if (pc_charge_skip_pump)
        charge_skip_load();
    /* func_80078748 without the name window's 120 ticks and the animation's
     * loop boundary: the effect's files and the object data are what the
     * release needs. */
    if (vs_battle_getEmptyObjectDataSlot() == 0 && func_800CEEBC() != 0)
        func_800704B0();
    return 1;
}

/* The name window kept up past a skipped wind-up's release (settings
 * qol_skip_charging_name, seconds shown at least; 0: closed at the release
 * as the original does). D_800EB9D8 is its text (vs_battle_displaySceneMessage
 * allocates it, func_800CB50C dismisses text box 7 and frees it). */
extern float pc_qol_skip_charging_name; /* project_settings.cpp */
extern int* D_800EB9D8;
extern char D_800F4E68;
extern unsigned pc_vsync_count(void); /* boot_runtime.c */
static int charge_skip_held;          /* the window is held */
static unsigned charge_skip_held_at;  /* the vsync of the release */
static int charge_skip_held_vsyncs;   /* how long it stays up from there */

/* At the release (func_80070278): 1 if the name window stays up (the game
 * does not close it there). */
int pc_charge_skip_hold(void)
{
    int vsyncs;
    charge_skip_held = 0;
    if (D_800EB9D8 == NULL || !charge_skip_now())
        return 0;
    /* Shown for qol_skip_charging_name seconds from here (the stretch before
     * the release is not presented), never past the original's own minimum:
     * its countdown (D_800F4E68, 120 ticks, 2 a game frame of 2 vsyncs while
     * casting: one a vsync; the skipped hold did not run it). */
    vsyncs = (int)(pc_qol_skip_charging_name * 60.0f + 0.5f);
    if (vsyncs > (u_char)D_800F4E68)
        vsyncs = (u_char)D_800F4E68;
    if (vsyncs <= 0)
        return 0;
    charge_skip_held = 1;
    charge_skip_held_at = pc_vsync_count();
    charge_skip_held_vsyncs = vsyncs;
    return 1;
}

/* Every game frame (vs_battle_exec), and before the window is opened again
 * (vs_battle_displaySceneMessage, `now` 1): the held window closed, the
 * game's way, once it has been up long enough and its text is out, when the
 * action is over, or now. */
void pc_charge_skip_name(int now)
{
    if (!charge_skip_held)
        return;
    if (D_800EB9D8 == NULL) { /* (closed by the game) */
        charge_skip_held = 0;
        return;
    }
    if (!now && D_800F196C == 2 && D_800F19CC != NULL
        && (pc_vsync_count() - charge_skip_held_at < (unsigned)charge_skip_held_vsyncs
            || !vs_battle_textBoxes[7].unk0.done))
        return;
    charge_skip_held = 0;
    func_800CB50C();
}

/* Test aid (RPC skip_charging): whether the name window is up (its text
 * allocated and text box 7 open), its text (up to `size` bytes, the game's
 * encoding) and whether it is held. */
int pc_charge_skip_name_window(unsigned char* out, int size, int* held)
{
    int n = 0;
    *held = charge_skip_held;
    if (D_800EB9D8 == NULL || vs_battle_textBoxes[7].state <= 0)
        return -1;
    for (; n < size && n < 112; ++n)
        out[n] = ((unsigned char*)D_800EB9D8)[n];
    return n;
}

/* Test aids (RPC skip_charging; charge_skip.c). The view and the caster's
 * facing at the release's first vsync: lookAt xyz, position xyz, the
 * camera's distance and yaw, the facing. */
void pc_charge_skip_view(int out[9], int actor)
{
    D_800F4538_t* a = actor >= 0 && actor < 17 ? D_800F4538[actor] : NULL;
    out[0] = vs_scratch.camera.lookAt.vx, out[1] = vs_scratch.camera.lookAt.vy, out[2] = vs_scratch.camera.lookAt.vz;
    out[3] = vs_scratch.camera.position.vx, out[4] = vs_scratch.camera.position.vy;
    out[5] = vs_scratch.camera.position.vz;
    out[6] = vs_battle_cameraCurrentSpherical.values.distance;
    out[7] = vs_battle_cameraCurrentSpherical.values.yaw;
    out[8] = a != NULL ? a->unk0.facing & 0xFFF : -1;
}
/* BATTLE's state (D_800F196C), its camera mode and in the action state the
 * actor and action, for the random number pin and the actions' log. */
int pc_charge_skip_battle_state(int* camera, int* actor, int* action)
{
    D_800F19CC_t2* a;
    *camera = (int)_cameraMode;
    *actor = *action = -1;
    if (D_800F19CC == NULL)
        return -1;
    if (D_800F196C == 2) {
        a = &D_800F19CC->unk854[D_800F19CC->unk0 & 3];
        *actor = (u_char)a->unk4.unk0.targetActor;
        *action = a->actionIndex;
    }
    return (int)D_800F196C;
}
/* The random numbers (main.c vs_main_getRand) from `seed`, as _initRand
 * seeds them at boot. */
extern int randArr[97];
extern int randIndex;
void pc_charge_skip_reseed(unsigned seed)
{
    int i;
    srand(seed);
    for (i = 0; i < 97; ++i)
        randArr[i] = rand();
    randIndex = 96;
}
/* Diagnostics (RPC skip_charging trace): this vsync's _cameraMode, the effect
 * loader's state, object slot 0 in use, D_800F1868 and the background
 * effects' ids and kinds. */
extern u_int D_800F531C;
extern D_800F53B8_t* D_800F54B0;
int pc_charge_skip_trace(char* out, int size)
{
    D_800F53B8_t* e;
    int n = snprintf(out, size, "%d:%u:%d:%d", (int)_cameraMode, D_800F531C,
        vs_battle_objectDataSlots[0].dataType != 0, D_800F1868);
    for (e = D_800F54B0; e != NULL && n < size - 16; e = psx_get(e->next))
        n += snprintf(out + n, size - n, ",%d/%d", e->unkD1C.unk38, e->unk14_8);
    return n < size ? n : size - 1;
}
/* The chain / defence ability window of the action under way (0: none open;
 * func_80069C6C opens it: battleAbilityTargetWindow), for the tests. */
int pc_charge_skip_window(void)
{
    return D_800F19CC != NULL && D_800F196C == 2 ? (int)D_800F19CC->battleAbilityTargetWindow : 0;
}
