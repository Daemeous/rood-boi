/* Pose blending (setting anim_blend): Ashley's slowed stride drawn between its poses, at draw time only.
 *
 * Under analog movement her stride plays at the pace of her speed (analog_move.c pc_analog_anim_steps): the animation
 * advances one whole tick when its carried fraction overflows, so at a light push the pose changes every few vsyncs. The
 * sim keeps stepping whole ticks (game state, collision, events and determinism are the same with this on or off); this
 * draws the actor at the fraction between the pose it has and the one the next tick brings:
 *
 *  - the next tick is found by running the game's own step (vs_actors_updateActorAnimationBlend: the decode, and the
 *    blend from the animation before at the start of a walk) on a scratch copy of the actor, with events and queued
 *    animations off (an end of the animation: no blend this frame);
 *  - the two poses are blended as the game itself keys them, linearly in the bones' Euler angles (13-bit, shortest way
 *    round) and in the scale vectors; the actor's bone chain (vs_actors_drawActorModel, no drawing: the visible bit is cleared in
 *    the copy) and its weapons' chains (vs_actors_buildWeaponBoneChain) are built from that on copies, into this unit's
 *    buffers, so bones keep their lengths and the weapons stay in the hand;
 *  - the renderer is told which matrices to draw instead of the actor's and the weapons' (pc_pose_blend_matrices, asked by
 *    actor_native.c where it takes the draw's palette). The palette feeds the ray-traced scene's skinned geometry and the
 *    frame interpolation between game frames, so shadows and reflections match what is drawn.
 *
 * The game's own build and draw then run as before with the real pose: nothing it keeps changes. Everything here is a
 * function of the game state (the pose, the carried fraction) and the setting; no history. */
#include "common.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/44F14.h"
#include "scratch.h"
#include "psx_types.h"
#include <string.h>
#include <sys/types.h>
#include <libgte.h>

extern int pc_analog_pose_progress(void) __attribute__((weak)); /* analog_move.c: 0..255, the carried fraction times anim_blend */

#define FLAG_VISIBLE 2u /* the actor's flag word (offset 8): the bone chain's draw runs only when set */
#define FLAG_STEPS 4u /* an actor whose animation steps (and runs its events) */
#define FLAG_ATTACHED 0x2000u /* its bones hang on another actor's */
#define TURN 0x2000 /* the pose angles' turn: 13 bits */
#define MAPS 8
#define WEAPON_COPIES 4

/* The draw's own storage, not game state (build_boot.py: .data.rood_host stays out of save states and the state hash). */
#define HOST __attribute__((section(".data.rood_host")))
static D_800F4538_t ahead HOST, blended HOST;
static vs_battle_wepModels_t weapon_copy[WEAPON_COPIES] HOST;
static int blended_ready HOST, copies_used HOST;
static struct {
    const void *real, *blend;
} maps[MAPS] HOST;
static int map_count HOST;

static unsigned* flags_of(void* actor) { return (unsigned*)((char*)&((D_800F4538_t*)actor)->unk0 + 8); }

static void map_add(const void* real, const void* blend)
{
    if (map_count < MAPS) {
        maps[map_count].real = real;
        maps[map_count].blend = blend;
        map_count++;
    }
}

/* The matrices to draw in place of `matrices` (the actor's bones or a weapon's chain, as the model draw receives
 * them), or NULL: the model is drawn as the game built it. */
const void* pc_pose_blend_matrices(const void* matrices)
{
    for (int i = 0; i < map_count; i++)
        if (maps[i].real == matrices)
            return maps[i].blend;
    return 0;
}

/* The start of a frame's actor pass: nothing is blended until a hook below says so. */
void pc_pose_blend_begin(void)
{
    map_count = 0;
    blended_ready = 0;
    copies_used = 0;
}

static int blend_angle(int from, int to, int s)
{
    int d = ((to - from + TURN / 2) & (TURN - 1)) - TURN / 2;
    return (from + ((d * s + 128) >> 8)) & (TURN - 1);
}
static int blend_scale(int from, int to, int s) { return from + (((to - from) * s + 128) >> 8); }

/* Ashley's actor, after this frame's animation step and before its bone chain is built and drawn. `camera` is the
 * parent the game builds its chain on. */
void pc_pose_blend_actor(D_800F4538_t* actor, MATRIX* camera)
{
    int s = &pc_analog_pose_progress ? pc_analog_pose_progress() : 0;
    if (s <= 0 || actor->companionSlot != 0 || (*flags_of(actor) & FLAG_ATTACHED) || !actor->unk0.nBones
        || actor->unk0.nBones > 41)
        return;
    /* One tick on: the game's own step (the decode and, at the start of a walk, the blend from the pose before) run on a
     * copy: no events, no queued animation, no eased scale or position; the transform block it writes is put back. */
    unsigned char block[sizeof vs_xform];
    memcpy(block, vs_xform, sizeof block);
    memcpy(&ahead, actor, sizeof ahead);
    ahead.framesPerStep = 1;
    ahead.queuedAnimation = 0;
    ahead.scaleFrames = 0;
    ahead.unk0.turnFrames = 0;
    ahead.unk0.translationFrames = 0;
    *flags_of(&ahead) &= ~FLAG_STEPS;
    vs_actors_updateActorAnimationBlend(&ahead);
    memcpy(vs_xform, block, sizeof block);
    if (ahead.pose.holdingLastFrame || ahead.pose.reachedEnd || ahead.pose.frame != (u_short)(actor->pose.frame + 1))
        return;
    memcpy(&blended, actor, sizeof blended);
    for (int i = 0; i < actor->unk0.nBones; i++) {
        SVECTOR* r = &blended.blendPose.unk0[i];
        const SVECTOR* to = &ahead.blendPose.unk0[i];
        r->vx = blend_angle(r->vx, to->vx, s);
        r->vy = blend_angle(r->vy, to->vy, s);
        r->vz = blend_angle(r->vz, to->vz, s);
        SVECTOR* k = &blended.blendPose.scaleKeys[i];
        const SVECTOR* kt = &ahead.blendPose.scaleKeys[i];
        k->vx = blend_scale(k->vx, kt->vx, s);
        k->vy = blend_scale(k->vy, kt->vy, s);
        k->vz = blend_scale(k->vz, kt->vz, s);
    }
    *flags_of(&blended) &= ~FLAG_VISIBLE;
    vs_actors_drawActorModel(&blended, camera, 0);
    blended_ready = 1;
    map_add(actor->bones, blended.bones);
}

/* One of her weapons (or shield) before the game builds its chain on her bone `weapon->unkD`: the chain built the same
 * way on the blended bones. */
void pc_pose_blend_weapon(vs_battle_wepModels_t* weapon, D_800F4538_t* actor)
{
    if (!blended_ready || copies_used >= WEAPON_COPIES)
        return;
    vs_battle_wepModels_t* copy = &weapon_copy[copies_used++];
    memcpy(copy, weapon, sizeof *copy);
    unsigned* flags = flags_of(actor);
    unsigned saved = *flags;
    *flags &= ~FLAG_VISIBLE; /* the chain's build, not its draw (it reads its actor's flag, not the copy's) */
    vs_actors_buildWeaponBoneChain(copy, &blended.bones[weapon->unkD]);
    *flags = saved;
    map_add((const char*)weapon + 0x20, (const char*)copy + 0x20);
}
