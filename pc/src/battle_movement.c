/* Native equivalents of the hand-written actor movement-state assembly in
 * BATTLE.PRG 72B70.s and its 7BDD4.s/7C7E0.s helpers. Guest structure offsets
 * keep the original ABI; return values reproduce the original $v0 where C
 * callers test them. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include "game/BATTLE/BATTLE.PRG/ai_state.h"
extern unsigned char* vs_battle_aiActorAiStates[];
extern unsigned short (*D_800F58D8)[8];
extern void pc_unimplemented(const char*);
extern void pc_require_native_callback(unsigned, const char*, unsigned);
extern int vs_battle_aiTryExecutePlannedActions(void*);
extern int vs_battle_aiTryRecoverBlockedMovement(void*);
extern void vs_battle_aiUpdateWanderMovement(void*, unsigned, int);
extern void vs_battle_aiSetActorAwarenessIndicator(int, int);
extern void vs_battle_aiInitializeActorAiRelations(void*);
int func_800E4180(void* state, int chance, int kind, int slot);

/* 7BDD4.s: rebuild the movement flags at 0x190: keep bits outside 0xFE1
 * (mask 0xFFFFF01E) and set 0x3C0 when the signed byte at 0x23 is nonzero. */
void func_800E4BB8(void* state);


/* 72B70.s: enter movement mode `mode` (byte 0x89 always records the request).
 * Re-entering the current mode (0x8A) does nothing. Modes reset their own
 * fields; mode 5 picks the nearest flagged waypoint of the actor's set
 * (D_800F58D8, 8 halfwords; bit 15 marks an entry, bits 0-4/5-9 its tile)
 * by Manhattan distance to the actor tile at 0x34. Returns the original $v0,
 * which is always nonzero. */
int vs_battle_aiSetMovementBehavior(void* state, int mode);


/* 72B70.s: vs_battle_aiWanderAroundTile enters mode 3 and wanders around (tile, range);
 * vs_battle_aiWanderAroundSavedCenter does so with the state's own 0xCC/0xC8 fields. */
void vs_battle_aiWanderAroundTile(void* state, unsigned tile, int range);

void vs_battle_aiWanderAroundSavedCenter(void* state);


/* 72B70.s: vs_battle_aiMoveToTile enters mode 2 to walk to `tile` (stored at 0x168):
 * within `range` of the tile centre (L800E5030 in 7C7E0.s; height from
 * vs_field_groundHeightAtPackedTile, squared distances compared unsigned) the walk finishes
 * (vs_battle_aiIssueActorCommand 0), otherwise it moves (vs_battle_aiDispatchAiLocomotionMode 0, heading 0). */
extern int vs_field_groundHeightAtPackedTile(unsigned);
extern void vs_battle_aiIssueActorCommand(void*, int);
extern void vs_battle_aiDispatchAiLocomotionMode(void*, int, int);
extern int vs_core_mathSquaredVectorLength(int, int, int);
void vs_battle_aiMoveToTile(void* state, unsigned tile, int range);

/* 72B70.s: vs_battle_aiPatrolWaypoints enters mode 5 and patrols its waypoints
 * (vs_battle_aiUpdateWaypointPatrol). The assembly skips both when $a2 is 0; its only caller
 * (vs_battle_aiUpdatePatrolCombatBehavior) passes no such argument and the PS1 value is whatever
 * vs_battle_aiUpdateCombatPursuit left there, nonzero on most of its paths (3, 0x280, the grid
 * size), so the port always patrols. */
extern void vs_battle_aiUpdateWaypointPatrol(void*);
void vs_battle_aiPatrolWaypoints(void* state);


/* 72B70.s: per-frame movement decision. Dispatch (vs_battle_aiTryExecutePlannedActions), unstick
 * (vs_battle_aiTryRecoverBlockedMovement) and the state's own callback at 0x68 each may claim the
 * frame; otherwise the actor wanders. The callback must be a native function. */
void vs_battle_aiDispatchMovementBehavior(void* state);


/* Guest words at 0x800F58xx hold 32-bit guest addresses (see psx_load). */
extern D_800F58BC_t* vs_battle_aiRoomAiState;
extern unsigned short (*D_800F58B8)[32];
extern int vs_core_mathSquaredVectorLength(int, int, int);
extern int vs_gte_rsqrt(int);

int vs_core_mathSquaredTileDistance(unsigned a, unsigned b);


/* 73460.s: candidate needs its target engaged: 1 when the state is busy
 * (byte 0x13), 0 without candidate option bit 4, else whether the target has
 * no engagement (vs_field_gimmickInstances[target] null). */
int vs_battle_aiDoesActionRequireCooldown(void* state, void* candidate);

/* 73460.s: candidate allowed now: not while the halfword at 0x90 is set, and
 * not when vs_battle_aiDoesActionRequireCooldown holds and the timer at 0x13F is running. */
int vs_battle_aiIsActionCooldownReady(void* state, void* candidate);


/* PS1 main RAM through any of its KUSEG/KSEG0/KSEG1 mirrors (2 MB repeated
 * up to 8 MB), for the original's out-of-list reads. The game's data,
 * overlays and heap keep their PS1 addresses there; an unaligned or non-RAM
 * address would raise an address/bus error on the PS1. */
/* 73460.s: 1 unless some hostile actor (relation >= 3) stands within two
 * tiles of the engaged target of `id`. The original starts its list walk at
 * &vs_battle_actors itself, so the first "node" has id vs_battle_actors[1]
 * (a pointer, or 0); its relation word (0x1A4 + id * 4, wrapping) and, when
 * that reads as hostile, its model vs_actors_actorRecords[id] (0x800F4538 + id * 4) and
 * the model's tile (+0x5C) are read through guest RAM as the PS1 does. */
int vs_battle_aiHasNearbyEnemy(void* state, int id);


/* 73580.s: whether candidate action `candidate` can start now (weight,
 * threshold against the room value at vs_battle_aiRoomAiState + 0x18, availability, target
 * engagement, distance and range, relation and model flags). The original
 * calls vs_gte_rsqrt on the distance and discards the result; the call is kept
 * for its GTE side effects. */
int vs_battle_aiCanExecuteActionCandidate(void* state, void* candidate);


extern unsigned* vs_field_movingPlatformTargetWaypoint(int);
extern void vs_battle_aiFindNearestRouteGateEntry(int, unsigned, unsigned, void*);
extern int vs_battle_aiUpdateTargetApproachMovement(void*, int);
extern int vs_battle_aiUpdateTargetFacing(void*, int);
extern int ratan2(int, int);
extern void vs_battle_aiRecoverMovement(void*, int);
extern void vs_battle_aiUpdateNavigationAreaMovement(void*, int);
extern void vs_battle_aiMoveWithHeightFilteredDirections(void*, int);
extern void vs_battle_aiResolveDirectionalMovement(void*, int, int);
extern void func_800E0A68(void*, int);
extern void func_800E1238(void*, int);
extern void func_800E153C(void*, int);
extern void vs_battle_aiMoveAwayFromReference(void*, int);

extern void vs_battle_aiClearAiContactOccupancy(int);
extern int vs_battle_aiFindRouteGateAtTile(int, unsigned, int);
extern int vs_battle_aiDestinationOccupancy(void*, unsigned);
extern void vs_battle_aiIssueActorCommand(void*, int);
extern unsigned short* D_800F5910; /* the room's u16 link pairs */
/* 766FC.s L800DF8A4: continue a route through a room link (gate). Byte 0x19B
 * is the gate bit the actor holds in the room flags (vs_battle_aiSetAiContactOccupancy by
 * vs_battle_aiResolveNavigationAreaRoute), word 0x19C the far end it is heading to. Off a link tile
 * (tile byte 1 < 2) the gate is released (vs_battle_aiClearAiContactOccupancy), the far end reset
 * to -1 and 0x19B cleared. On a link tile, vs_battle_aiFindRouteGateAtTile finds the link whose
 * far end is 0x19C; when its near end (even halfword `index` * 2 of
 * D_800F5910: x bits 0-4, z bits 5-9) can be entered (vs_battle_aiDestinationOccupancy > 0), the
 * gate of pair index / 2 is released, the step is stored at 0x170 and command
 * 14 issued; otherwise vs_battle_aiRecoverMovement recovers.
 * The original builds the step tile in the caller's $s2 (it saves $s2 but
 * never initialises it), so byte 3 of 0x170 is the caller's top $s2 byte:
 * `caller_s2` passes it. vs_battle_aiDestinationOccupancy shifts byte 3 out and the command
 * consumers (vs_actors_executeQueuedActorCommands, vs_actors_beginActorJumpToTile, vs_actors_beginTeleport) read bytes 0-2
 * only, so the value is unobservable; callers pass what they know. */
unsigned pc_route_continuations; /* times run (tests read it by symbol) */
void pc_route_continuation(void* state, unsigned caller_s2)
{
    vs_battle_aiState* base = state;
    ++pc_route_continuations;
    if (base->tile.b[1] < 2) {
        *(int*)&base->exit = -1;
        vs_battle_aiClearAiContactOccupancy(base->gate);
        base->gate = 0;
        return;
    }
    int index = vs_battle_aiFindRouteGateAtTile((int)(__UINTPTR_TYPE__)base, *(unsigned*)&base->exit, 0);
    if (index >= 0) {
        unsigned link = D_800F5910[index * 2];
        unsigned tile = (caller_s2 & 0xff000000u) | (link & 0x1f) | (((link >> 5) & 0x1f) << 16);
        if (vs_battle_aiDestinationOccupancy(state, tile) > 0) {
            *(int*)&base->exit = -1;
            vs_battle_aiClearAiContactOccupancy((index >> 1) + 2);
            base->gate = 0;
            base->next.movementPosition.raw = tile;
            vs_battle_aiIssueActorCommand(state, 14);
            return;
        }
    }
    vs_battle_aiRecoverMovement(state, 0);
}

/* 7BDD4.s: head for the packed target tile at 0x168 unless already on it
 * (then vs_battle_aiRecoverMovement); with a gate held (0x19B) continue the route through
 * it instead (L800DF8A4). The callers' $s2 is not known natively; its byte
 * is unobservable (see pc_route_continuation). */
void vs_battle_aiMoveActorTowardDestinationTile(void* state, int angle);

/* 7BDD4.s: free movement (vs_battle_aiResolveDirectionalMovement mode 1, angle & 0xFFF) unless the
 * actor is held (0x1A); with a gate held (0x19B) continue the route through
 * it (L800DF8A4, returning through 0x800E4BAC). */
void func_800E4B70(void* state, int angle);

/* 7C7E0.s: dispatch through jtbl_800F1740 by priority state (0x125) and
 * `kind`, passing `angle`. The original table holds these six functions. */
void vs_battle_aiDispatchAiLocomotionMode(void* state, int kind, int angle);

/* 7C7E0.s: aim at the point (0x17C, 0x180), record it packed at 0x158, mark
 * movement (bytes 1 = kind, 9 = 1) and dispatch with the heading. */
void func_800E4FE0(void* state, int kind);


/* 72B70.s: approach actor `id`. Uses the model's waypoint group when allowed,
 * otherwise its tile; then plans with vs_battle_aiUpdateTargetApproachMovement and on success enters
 * mode 1. A missing cached actor still enters mode 1 for nonzero ids, as the
 * original's leftover $v0 (id * 4) does. */
int vs_battle_aiApproachActor(void* state, int id, int distance, int option);

int vs_battle_aiTrackActorPosition(void* state, int id);

/* 72B70.s: flee toward the (logically halved, running) average tile of hostile
 * actors (relation bits 5-7 == 7), remembering it for 90 frames at 0x110/0x114;
 * while in mode 4 keep going there until the timer runs out. */
int vs_battle_aiFleeVisibleEnemies(void* state, int kind);

/* 72B70.s: first other actor whose relation bits 6-7 equal `relation`; stores
 * its object's tile in *tile when given. Returns its id or -1. */
int vs_battle_aiFindActorByRelationship(void* state, unsigned* tile, int relation);


extern int vs_battle_aiUpdateCombatPursuit(void*);
extern int func_800DA5CC(void*);
extern int func_800DA6A4(void*);
extern int func_800DA738(void*);
extern int vs_battle_aiUpdatePatrolCombatBehavior(void*);
extern int func_800DAAD8(void*);
extern int vs_battle_aiIsEngagementReady(void*);
extern void vs_battle_aiMoveToTile(void*, unsigned, int);

/* 72B70.s behaviour 0x800DBA4C: wander unless fleeing (0x0E set), else
 * vs_battle_aiUpdateCombatPursuit. */
static int behaviour_800DBA4C(void* state)
{
    if (!((vs_battle_aiState*)state)->flagE)
        return vs_battle_aiUpdateCombatPursuit(state);
    if (!vs_battle_aiFleeVisibleEnemies(state, 1))
        vs_battle_aiWanderAroundSavedCenter(state);
    return 1;
}
/* 72B70.s behaviour 0x800DBA88: flee when 0x0E is set; for stances 0/1 (0x131)
 * approach the best candidate's target within 0x100 (0x140 for models with
 * flag 0x20), stance 0 capping that by the candidate's rsqrt distance. */
static int behaviour_800DBA88(void* state)
{
    vs_battle_aiState* base = state;
    if (base->flagE)
        return vs_battle_aiFleeVisibleEnemies(state, 1);
    unsigned stance = (1u << base->status) & 3;
    if (!stance)
        return vs_battle_aiUpdateCombatPursuit(state);
    unsigned* best = (unsigned*)base->entries.candidates[0];
    int limit = ((unsigned char*)base->object.actorObject)[0xa] & 0x20 ? 0x140 : 0x100;
    unsigned target = best[3] & 0xf;
    if (stance & 1) {
        int root = vs_gte_rsqrt((int)best[2]);
        if (!(limit < root))
            limit = root;
    }
    int result = vs_battle_aiApproachActor(state, target, limit, 0);
    return result ? result : vs_battle_aiTrackActorPosition(state, target);
}
/* 72B70.s behaviour 0x800DBB98: unless vs_battle_aiUpdateCombatPursuit claims the frame, wander
 * near the first hostile actor (relation 2): far ones (> 0x64000) by tile
 * (vs_battle_aiMoveToTile), near ones by area (vs_battle_aiWanderAroundTile, range 5). */
static int behaviour_800DBB98(void* state)
{
    vs_battle_aiState* base = state;
    unsigned tile;
    int result = vs_battle_aiUpdateCombatPursuit(state);
    if (result)
        return result;
    int id = vs_battle_aiFindActorByRelationship(state, &tile, 2);
    if (id < 0)
        return 0;
    if (0x64000u < base->tbl.t.distances[id])
        vs_battle_aiMoveToTile(state, tile, 0);
    else
        vs_battle_aiWanderAroundTile(state, tile, 5);
    return 1;
}
/* 72B70.s behaviour vs_battle_aiUpdateEngagementApproachBehavior: when vs_battle_aiIsEngagementReady holds let vs_battle_aiUpdateCombatPursuit
 * claim the frame; otherwise approach actor 0 within 0x100. Not static:
 * vs_battle_aiTryBeginAshleyAlliance's Zig port stores its address. */
int behaviour_800DBC0C(void* state)
{
    if (vs_battle_aiIsEngagementReady(state)) {
        int result = vs_battle_aiUpdateCombatPursuit(state);
        if (result)
            return result;
    }
    int result = vs_battle_aiApproachActor(state, 0, 0x100, 0);
    return result ? result : vs_battle_aiTrackActorPosition(state, 0);
}
/* Native vs_battle_aiDecisionCallbacks (actor behaviour table indexed by actor flags bits 4-8);
 * build_boot.py verifies the original words before linking. */
int (*vs_battle_aiDecisionCallbacks[])(void*) = { behaviour_800DBA4C, behaviour_800DBA88, behaviour_800DBB98,
    func_800DA5CC, func_800DA6A4, func_800DA738, vs_battle_aiUpdatePatrolCombatBehavior, func_800DAAD8,
    behaviour_800DBC0C, 0 };

/* 72B70.s: whether a candidate's target counts as engaged in the room's
 * current encounter (vs_battle_aiRoomAiState): forced by 0x8E, blocked by 0x98, otherwise
 * the target relation bit 0 plus the room's counters and per-actor mask. The
 * original calls vs_battle_aiIsActionCooldownReady and discards its result. */
int vs_battle_aiCanApproachEngagedTarget(void* state, void* candidate);


extern unsigned D_800F58E0;
extern void vs_battle_aiUnlinkEngagedActor(void*);
/* 6FA6C.s: join the room's encounter. Already engaged (0x8E) succeeds;
 * a full encounter (count 0x0D below members 0x12) or a block (0x98 or
 * D_800F58E0) fails. Otherwise set the engagement timer (1, or 0x20 after a
 * recent leave at 0x9A), add the actor's bit to mask 0x10, bump members and
 * push the actor on the room's engaged list (head 0x1C, tail 0x20, link 0x1A0). */
int vs_battle_aiTryEngageActor(void* state);

/* 6FA6C.s: leave the encounter: clear the mask bit and timer, block
 * re-joining (0x98 = 0x30 unless exactly two actors remain; 0x9A = 90),
 * unlink (vs_battle_aiUnlinkEngagedActor), drop a member and notify (vs_battle_aiSetActorAwarenessIndicator). */
void vs_battle_aiDisengageActor(void* state);

void vs_battle_aiDisengageActorById(int id);


/* 7C7B0.s: whether actor `id` is farther than the reach threshold. The
 * original adds 0x80 to `range` for model class 10 but then squares its
 * constant 10 instead, so the test is always 100 < distance (unsigned). */
int func_800E4FB0(void* state, int id, int range);


/* 7B5DC.s floor-height helpers over the grid cached at scratchpad 0x3BC
 * (64-byte rows of 16-bit cells): raw height sign((cell & 0x3FF) << 6) minus
 * (cell & 0x1C00) >> 3; the *EC variants sign-extend it from 15 bits. */
int vs_field_groundHeightFlagsAtTile(int x, int z);

int vs_field_groundHeightFlagsAtWorldPosition(int x, int z);

int vs_field_groundHeightFlagsAtPackedTile(unsigned tile);

int vs_field_groundHeightAtTile(int x, int z);

int vs_field_groundHeightAtPackedTile(unsigned tile);

int vs_field_groundHeightAtWorldPosition(int x, int z);


/* Scratchpad tables set up by the movement code (see vs_battle_aiCacheActorAiWorkingState):
 * 0x3EC + dir: packed direction (dx signed bits 0-1, dz signed bits 4-5);
 * 0x3E0 + 3x3 index: per-neighbour direction or -1; 0x3C0: navigation cells;
 * 0x3BC: floor cells; 0x3C4/0x3DC/0x3F4: optional occupancy bitmap.
 * vs_move is the AI movement frame (scratch.h). */
static int direction_dx(int packed) { return (int)((unsigned)packed << 30) >> 30; }
static int direction_dz(int packed) { return (int)((unsigned)packed << 26) >> 30; }

/* 7BDD4.s: whether stepping from `tile` in direction `dir` is blocked: the
 * neighbour has no floor (raw height negative) or differs by 64 or more. */
int vs_battle_aiIsAdjacentTileStepBlocked(unsigned tile, int dir);


/* 70D4C.s: id + 1 of the first other actor whose bounds overlap the point one
 * radius ahead of `state` in direction `dir`, else 0; cached per direction in
 * scratchpad 0x3D2 (valid bits) / 0x3D4 + dir. */
int vs_battle_aiDirectionalBlockingActor(void* state, int dir);


/* 70D4C.s vs_battle_aiCanWalkInDirection: whether direction `dir` is passable (1) or blocked (0),
 * cached in scratchpad 0x3D0 (valid bits) / 0x3D1 (passable bits). Blocked by
 * an actor (vs_battle_aiDirectionalBlockingActor), the occupancy/step test, reserved cell 0x38, cell
 * flags (bit 4 unless state byte 6, bits 5/7, mask byte 0x32 against bits
 * 8-15), a different area code when required, or a higher floor level. The
 * original also stashes `dir` in GTE VXY2, which is not reproduced. */
int vs_battle_aiCanWalkInDirection(void* state, int dir)
{
    vs_battle_aiState* base = state;
    if ((vs_move.flyTested >> dir) & 1)
        return (vs_move.flyPassable >> dir) & 1;
    int blocked = vs_battle_aiDirectionalBlockingActor(state, dir);
    if (!blocked) {
        unsigned tile = base->tile.raw, fine = base->unk120;
        int packed = vs_move.directions[dir], probe = dir;
        int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
        int next_x = ((int)(fine & 0xff) + direction_dx(packed)) >> 1;
        int next_z = ((int)((fine >> 16) & 0xff) + direction_dz(packed)) >> 1;
        if (next_x == tile_x && next_z == tile_z)
            goto done;
        if (dir & 1)
            probe = (signed char)vs_move.lookup[(next_x - tile_x + 1) + (next_z - tile_z + 1) * 4];
        unsigned char* cells = (unsigned char*)vs_nav.cells;
        unsigned short* cell = (unsigned short*)(cells + next_x * 2 + next_z * 64);
        if (!vs_nav.exits) {
            blocked = vs_battle_aiIsAdjacentTileStepBlocked(tile, probe);
        } else {
            unsigned reserved = base->previous.stuckMovementPosition.raw;
            unsigned char* reserved_cell =
                cells + (((reserved >> 10) & 0x3fc0) | ((reserved & 0xff) << 1));
            if (reserved_cell == (unsigned char*)cell) {
                blocked = 1;
                goto done;
            }
            unsigned char* bitmap = vs_nav.bitmap;
            blocked = (bitmap[tile_z * vs_move.width + tile_x] >> probe) & 1;
        }
        if (blocked)
            goto done;
        unsigned flags = *cell;
        if ((blocked = (flags >> 4) & (base->flags4.b[2] ^ 1)))
            goto done;
        if ((blocked = ((flags >> 5) & 1) | ((flags >> 7) & 1)))
            goto done;
        if ((blocked = base->f32 & (flags >> 8)))
            goto done;
        unsigned here = *(unsigned short*)(cells + tile_x * 2 + tile_z * 64);
        if ((base->f29 & (base->flags0.b[2] ^ 1)) && (here & 0xf) != (flags & 0xf)
            && (base->area & 0xf) != (flags & 0xf)) {
            blocked = 1;
            goto done;
        }
        unsigned char* floors = (unsigned char*)vs_nav.floors;
        blocked = (int)(*(unsigned short*)(floors + tile_x * 2 + tile_z * 64) & 0x1c00)
                < (int)(*(unsigned short*)(floors + next_x * 2 + next_z * 64) & 0x1c00);
    }
done:
    vs_move.flyTested |= 1u << dir;
    if (blocked)
        vs_move.flyPassable &= ~(1u << dir);
    else
        vs_move.flyPassable |= 1u << dir;
    return (vs_move.flyPassable >> dir) & 1;
}

/* 7BDD4.s: scratchpad linear-congruential step (seed half 0x3F8 + increment
 * word 0x3FC, stored back as a word); returns the value & (n - 1). */
int vs_battle_aiNextActorRandomMasked(int n);

/* 70D4C.s: whether `tile` can be entered: 0 for the reserved tile (0x38,
 * compared on its low 24 bits against tile with byte 1 cleared), outside the
 * grid, or blocked cell flags (0x80B0 plus the actor's mask 0x32 in bits
 * 8-15); -1 when another actor's bounds overlap the tile centre; else 1. */
int vs_battle_aiDestinationOccupancy(void* state, unsigned tile);


/* 70D4C.s: whether the actor may step from tile `from` to tile `to` in
 * direction `dir`: 1 yes, 0 no, -1 when the step leaves its approach radius.
 * Quirks kept: for a negative X step the original negates the Z delta instead
 * of X; the area-code test always uses byte 0x132 (0x154 is loaded then
 * overwritten); the approach test reads halfwords from the vs_actors_actorRecords array
 * itself (+0x1C/+0x20 past the entry, not through it) and passes the packed
 * `to` tile as the SQR Z input. */
int vs_battle_aiCanTraverseTileEdge(void* state, unsigned from, unsigned to, int dir);


typedef int (*direction_test)(void*, int);
extern int rcos(int);
extern int rsin(int);
extern void vs_battle_aiIssueActorCommand(void*, int);
extern unsigned char D_800F1758[];

/* 7B55C.s: passability bits of the four axis directions (0, 2, 4, 6) as
 * bits 3..0. The original stashes its state in GTE registers (IR0-IR3,
 * SXY0/1, MAC1), which is not reproduced. */
int vs_battle_aiCardinalPassabilityMask(void* state, direction_test test);


/* 7B3C8.s: record the step tile toward the target (0x168) in the path flags
 * (0x190 bits 22+ = x, 27+ = z; 0 per axis already reached, otherwise the
 * target coordinate plus one when it lies ahead). The fields overlap, as in the
 * original. */
void vs_battle_aiSetDetourBoundary(void* state);


/* 7B3C8.s: forget (0x194/0x198/0x19A) a tile the actor keeps revisiting
 * (5 frames running blocks it for 120), clear 0x156, and store the step
 * count (0x7E, or 0x1F at random for non-flagged actors) in flags bits 5-11. */
unsigned vs_battle_aiRefreshDetourProgress(void* state, unsigned flags);


/* 7B5DC.s: start a step plan. When the plan was empty, forced (bit 16) or the
 * actor is fleeing (byte 4), pick a random turn side (bit 4) and maybe shout
 * (func_800E4180 kind 1); clear bit 17 when no steps remain; store the
 * passability of the four axis directions in bits 18-21. */
unsigned vs_battle_aiInitializeObstacleDetour(void* state, unsigned flags, direction_test test);


/* Shared tail of the three step planners (7B5DC.s L800E3F50/L800E4084). */
static int finish_step(vs_battle_aiState* base, unsigned flags, int dir, int axis_only)
{
    unsigned plan = vs_battle_aiRefreshDetourProgress(base, (flags & 0xfffffff1) | ((unsigned)dir << 1));
    unsigned tile = base->tile.raw, target = base->destination168.destination;
    int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
    int target_x = target & 0xff, target_z = (target >> 16) & 0xff;
    unsigned x, z;
    if (!axis_only) {
        z = tile_z == target_z ? 0 : tile_z + (tile_z < target_z);
        x = tile_x == target_x ? 0 : tile_x + (tile_x < target_x);
    } else if (plan & 4) {
        z = tile_z + (tile_z < target_z);
        x = 0;
    } else {
        x = tile_x + (tile_x < target_x);
        z = 0;
    }
    plan = (plan & 0x3fffff) | (z << 27) | (x << 22) | 1;
    base->movementFlags.raw = plan;
    return 1;
}
/* 7B5DC.s: step around an obstacle between headings `from` and `to`. */
int func_800E3EAC(void* state, int from, int to, direction_test test)
{
    vs_battle_aiState* base = state;
    unsigned flags = vs_battle_aiInitializeObstacleDetour(state, base->movementFlags.raw, test);
    int turn = (flags & 0x10) ? 2 : -2;
    int offset = ((from + turn) & 7) == to ? turn : 4;
    int dir = (to + offset) & 7;
    if (!test(state, dir)) {
        dir = (((flags >> 1) & 7) + turn) & 7;
        if (!test(state, dir)) {
            base->movementFlags.raw = flags;
            return 0;
        }
    }
    return finish_step(base, flags, dir, 0);
}
static int final_step(vs_battle_aiState* base, unsigned flags, int dir, direction_test test)
{
    if (!test(base, dir)) {
        base->movementFlags.raw = flags;
        return 0;
    }
    return finish_step(base, flags, dir, 1);
}
/* 7B5DC.s: keep following a wall: `side` is +-1; turning the planned way
 * succeeds at once, otherwise try the reverse heading, then reverse - turn. */
int func_800E3FF4(void* state, int heading, int side, direction_test test)
{
    vs_battle_aiState* base = state;
    unsigned flags = vs_battle_aiInitializeObstacleDetour(state, base->movementFlags.raw, test);
    int turn = (flags & 0x10) ? 1 : -1;
    if (turn == side)
        return finish_step(base, flags, heading, 1);
    int dir = (heading + 4) & 7;
    if (test(state, dir))
        return finish_step(base, flags, dir, 1);
    return final_step(base, flags, (dir - turn * 2) & 7, test);
}
/* 7B5DC.s (0x800E40F0): step toward heading `dir`, trying dir - turn, its
 * reverse, then dir reversed. */
int func_800E40F0(void* state, int dir, direction_test test)
{
    vs_battle_aiState* base = state;
    unsigned flags = vs_battle_aiInitializeObstacleDetour(state, base->movementFlags.raw, test);
    int turn = (flags & 0x10) ? 2 : -2;
    int next = (dir - turn) & 7;
    if (test(state, next))
        return finish_step(base, flags, next, 1);
    next = (next + 4) & 7;
    if (test(state, next))
        return finish_step(base, flags, next, 1);
    return final_step(base, flags, (dir + 4) & 7, test);
}

/* 7B3C8.s entries 0x800E3C30/3C3C/3C48: run a step planner only when the
 * target is within 0x31000 (squared, tile centres) and byte 4 bit 0 is set,
 * then refresh the step tile (vs_battle_aiSetDetourBoundary). The original stashes $ra/$a0/$v0
 * in GTE VXY0/VZ0/VXY1, which is not reproduced. */
static int near_target(vs_battle_aiState* base)
{
    unsigned tile = base->tile.raw, target = base->destination168.destination;
    int dx = (int)((target & 0xff) << 7) - (int)((tile & 0xff) << 7);
    int dz = (int)(((target << 8) >> 17)) - (int)(((tile << 8) >> 17));
    return ((dx * dx + dz * dz) < 0x31000) & (int)base->flags4.word;
}
int func_800E3C30(void* state, int a, int b, direction_test test)
{
    if (!near_target(state))
        return 0;
    int result = func_800E3EAC(state, a, b, test);
    vs_battle_aiSetDetourBoundary(state);
    return result;
}
int func_800E3C3C(void* state, int a, int b, direction_test test)
{
    if (!near_target(state))
        return 0;
    int result = func_800E3FF4(state, a, b, test);
    vs_battle_aiSetDetourBoundary(state);
    return result;
}
int func_800E3C48(void* state, int a, direction_test test)
{
    if (!near_target(state))
        return 0;
    int result = func_800E40F0(state, a, test);
    vs_battle_aiSetDetourBoundary(state);
    return result;
}
/* Native D_800F1760 (rows: around, follow, toward; columns: approach mode).
 * build_boot.py verifies the original words. */
int (*D_800F1760[3][4])() = {
    { (int (*)())func_800E3EAC, (int (*)())func_800E3C30, (int (*)())func_800E3EAC,
        (int (*)())func_800E3C30 },
    { (int (*)())func_800E3FF4, (int (*)())func_800E3C3C, (int (*)())func_800E3FF4,
        (int (*)())func_800E3C3C },
    { (int (*)())func_800E40F0, (int (*)())func_800E3C48, (int (*)())func_800E40F0,
        (int (*)())func_800E3C48 },
};

/* 7B5DC.s: per-slot cry/animation request (0x474 + slot * 2: count << 8 |
 * kind). A new request (kind != 3) fires with chance/128 and loads its length
 * from D_800F1758; each call counts down, sets 0x13A = 0x40 and, for kinds 1
 * and 2, a pitch at 0x188 by remaining length before vs_battle_aiIssueActorCommand(16).
 * Returns 1 while at least 2 frames remained. */
int func_800E4180(void* state, int chance, int kind, int slot);


/* 7BDD4.s: whether `tile` lies behind the actor relative to its facing
 * target (actor 0x15C, or its own model heading when that is >= 16). */
int func_800E47E4(void* state, unsigned tile);


/* 7BDD4.s: try climbing toward a higher tile: from `tile` walk up to
 * steps - 1 tiles in `dir` while the floor stays at least as high; on the
 * first lower tile return it if vs_battle_aiCanTraverseTileEdge and vs_battle_aiDestinationOccupancy accept the
 * step, else 0. Only with the occupancy bitmap active (0x3F4). The original
 * intended min(steps - 1, 2) but tests its constant, so the bound is steps - 1. */
unsigned func_800E49D8(void* state, unsigned tile, int dir, int steps);

void vs_battle_aiReserveAiDestinationTile(int x, int z);


/* vs_battle_aiIssueActorCommand speed selection from the actor's gait word 0x224: running
 * (byte 1) uses mask `run` (bytes 0/1 test, speed from byte 2/3), walking
 * uses `walk_low`/`walk_high`. Returns 0 with *command set, or -1 to stand. */
/* vs_battle_aiIssueActorCommand's shared move (L800DEF8C): face `angle`, pick the gait and
 * step; if a corner of the actor's square (half-size 0x92) would enter a
 * blocked neighbour, slide to the tile edge instead (command 0x0D). */
/* 766FC.s: issue movement/action command `action` to the actor's command
 * block (0x54): record it at scratchpad 0x3C8, reset the block (byte 8 =
 * 0x80, byte 10 = facing target) and fill it through jtbl_800F16F8 (18
 * entries). Byte 9 receives the command code. */
void vs_battle_aiIssueActorCommand(void* state, int action);


/* 7BDD4.s: refresh the approach point (0xF4/0xF6, half-tile units) of the
 * target actor 0xE5: its waypoint group when it has one (model byte 0x5D for
 * plain models, tile byte 1 of 0x60 for flagged ones), else its position. */
void vs_battle_aiRefreshAiTargetTile(void* state);

void func_800E48F8(void* state);


extern void vs_battle_aiReleaseActorAiReservations(void*);
/* 72B70.s: finish a pending stop request (0x12D): request 1 with a basic
 * command (< 9) idles (command 1); otherwise release the tile reservation
 * (vs_battle_aiReleaseActorAiReservations), clear the request and switch to command 2. */
void func_800DB7B4(void* state);


/* 7BDD4.s: with no route (0x18C) and no cooldown (0x155), and a random draw
 * below `range` (always true for powers of two), set command byte 0xB to
 * `value` and start a 30-frame cooldown. The original computes a masked byte
 * but stores the raw value. */
void func_800E48A8(void* state, int value, int range);


extern int vs_field_collectMovingPlatformPositions(void*);
extern short vs_field_floorHeightAt(int, int);
/* 7E05C.s func_800E6BA0 (register ABI: the probe point in $s2-$s4): 1 unless
 * one of the room's obstacle points (vs_field_collectMovingPlatformPositions) lies within 0x40 / 0x20 /
 * 0x40 of it (obstacle Y raised by 0x20). */
/* 7E05C.s: trace an attack from `from` toward `to` in 32 steps of
 * (to - from) >> 5. Stop at the first actor (not `self`, not hidden by flags
 * 0xEA, not knocked down) whose bounds contain the point while in front of the
 * start, at an obstacle, or below the floor: store the point (pad = actor id
 * + 1, or 0) in `out` and return it. Returns 0 when nothing was hit. The start
 * X/Z are compared as GTE IR0/IR2 (16-bit), and the routine ends by falling
 * through into func_800E6EAC's return. The address comes back as a 32-bit
 * word: 146C.c declares the function int (an 8-byte pointer would be read
 * through a 4-byte return there). */
int vs_field_traceActorAndTerrainCollision(unsigned* from, unsigned* to, unsigned* out, int self);


/* 7DE94.s: release actor `id`'s tile reservation (vs_battle_aiReleaseActorAiReservations body) and
 * mark it held (0x1A = 1). The original enters vs_battle_aiReleaseActorAiReservations's body with the
 * flag value; setting it afterwards is equivalent (nothing reads it there). */
void func_800E6764(int id);

/* 7DE94.s: release the reservation of actor `id` when its command (byte 9)
 * is one of the set 0xA41800 and its model has no motion flags (0xA & 0x1F,
 * 0xB & 0xB). */
void func_800E6700(int id);


/* 72B70.s: resolve a step onto a cell flagged 0x20 (e.g. a ledge or gap):
 * keep stepping in `dir` while cells stay flagged; at the first clear cell
 * within min(0xB2, 3) steps, return it with the step/entry result
 * (vs_battle_aiCanTraverseTileEdge, then vs_battle_aiDestinationOccupancy) in bits 24-31. Unflagged targets are
 * returned unchanged. As in the original, the flagged-cell walk continues
 * past the step limit while byte 0xB2 is nonzero. */
unsigned vs_battle_aiFindGapLandingTile(void* state, unsigned tile, int dir, unsigned from);


/* 7BDD4.s: remember the best candidate's target (when there is one and the
 * actor is not locked, byte 0x19): 0x38 = target << 24 | current tile,
 * 0x3C = the target's tile, 0x40 = 900-frame memory. */
void vs_battle_aiRememberAiActionTarget(void* state);


/* Room-entry and actor bookkeeping helpers from 7BDD4.s and 7E05C.s. */
extern unsigned short (*D_800F58D0)[32];
extern void* D_800F5920;
extern void* vs_main_allocHeap(unsigned);
extern void vs_core_memoryClearAlignedWords(void*, unsigned);

/* 7BDD4.s: pick the first armed reaction from the packed bytes at 0x224:
 * flag 0x20 (code 10) then 0x10 (code 9) when byte 1 is set, the reverse
 * otherwise. Low byte flags select byte 2, byte 1 flags select byte 3. */
int func_800E4764(void* state, int* value, int* code);


/* 7BDD4.s: clear the reaction word, copy 0x34 to 0x170 and issue actor
 * command 11 (func_800E4B10) or 12 (func_800E4B18). */
void func_800E4B10(void* state);

void func_800E4B18(void* state);


/* 7BDD4.s: clear (vs_battle_aiClearAiContactOccupancy) or set (vs_battle_aiSetAiContactOccupancy) bit `bit` of the
 * room record's byte 0xE. Bit 31 inverts the sign test, as in the original. */
static void room_flag(int bit, int clear)
{
    D_800F58BC_t* room = vs_battle_aiRoomAiState;
    if (!room)
        return;
    int mask = 1 << (bit & 31);
    if (clear)
        mask = -1 - mask;
    room->closedGates = mask < 0 ? room->closedGates & mask : room->closedGates | mask;
}
void vs_battle_aiClearAiContactOccupancy(int bit);

void vs_battle_aiSetAiContactOccupancy(int bit);


/* 7BDD4.s: set actor `id`'s 0x13F countdown to 30 and fall through into
 * vs_battle_aiRememberAiActionTarget with that actor. */
void func_800E4C64(int id);


/* 7E05C.s: adjust the occupancy count of the tile under an actor's placement
 * word (0x5C) by +1 (vs_battle_aiAddAiTileOccupant) or -1 (vs_battle_aiRemoveAiTileOccupant), in steps of 0x400. */
static void tile_occupancy(unsigned char* actor, int delta)
{
    unsigned placement = *(unsigned*)(actor + 0x5c);
    if (!D_800F58B8)
        return;
    unsigned short* cell =
        (unsigned short*)((unsigned char*)D_800F58D0 + (placement & 0xff) * 2 + ((placement << 8) >> 18));
    *cell += delta << 10;
}
void vs_battle_aiAddAiTileOccupant(void* actor);

void vs_battle_aiRemoveAiTileOccupant(void* actor);

void vs_battle_aiMarkAiTileDynamicObstacle(int x, int z);


/* 7E05C.s: lowest of 16 floor heights sampled from (x0, z0) toward (x1, z1)
 * in steps of a sixteenth, starting at 0 (heights are 15-bit signed). */
int vs_battle_aiMinimumAiGroundHeightAlongSegment(int x0, int z0, int x1, int z1);

void vs_battle_aiClearActorAiNavigationReset(int id);


/* 7E05C.s: when actor `id` is of type 11 or 14, its room-flag word (0x170,
 * bits 8-15) is set and its model byte 0xC has a zero low nibble, clear that
 * room flag. A missing record reads PS1 low RAM, as the original does. */
void vs_battle_aiReleaseActorAiContactOccupancy(int id);


/* 7E05C.s: actors whose 0x16 target is `id` drop it (0x10), then release
 * actor `id`'s reservation through vs_battle_aiDisengageActor. */
void vs_battle_aiClearActorAiLinksAndPlan(int id);

void vs_battle_aiAllocateOrbitingBossState(void* state);


/* Engagement state helpers from 7399C.s and 73B0C.s. */
extern int rand(void);
extern void vs_battle_aiSetTemporaryAwarenessIndicator(void*, int, int);

/* 7399C.s: leave the engaged state (0x13): restore the saved movement
 * callback (0x6C -> 0x68) and, unless 0x30 is set, queue the recovery motion. */
void vs_battle_aiEndAshleyAlliance(void* state);


/* 7399C.s: enter the engaged state when forced, or on the second of two
 * rand() & 0x7F < 0x4C rolls; save 0x68 to 0x6C and install vs_battle_aiUpdateEngagementApproachBehavior. */
void vs_battle_aiTryBeginAshleyAlliance(void* state, int force);


/* 73B0C.s: move *value by `step` (up when mode is 2, else down) and clamp it
 * to 0..*limit. */
void vs_battle_aiApplySimulatedStatDelta(short* value, short* limit, int step, int mode);

