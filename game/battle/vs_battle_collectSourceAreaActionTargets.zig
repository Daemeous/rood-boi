//! A ranged action's targeting preview (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// The 8 bytes from the actor's unk10 (a SVECTOR of its last target position).
fn unk10Vector(actor: ?*v.vs_battle_actor) v.SVECTOR {
    const base: [*]const u8 = @ptrCast(actor.?);
    return @as(*const v.SVECTOR, @ptrCast(@alignCast(base + @offsetOf(v.vs_battle_actor, "unk10")))).*;
}

/// vs_battle_actionState, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.vs_battle_actionState().*.?;
}

/// The action's kind (unk2_4) as the C reads it: bits 20..23 of its first word.
fn kind(spec: *v.vs_action_t) u32 {
    return (psx.byteAt(u32, spec, 0).* >> 20) & 15;
}

/// orig: vs_battle_collectSourceAreaActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Aims the current action's range from its user (func_800A1AF8) at the targeted actor (level when it targets
/// the user itself), previews it (func_800C0700) when unk2984 is set, then lists every actor 0..15 the action may
/// target and the range covers (vs_battle_isPointInTargetVolume): each limb with a part for limb-targeting kinds, else the body,
/// with its hit preview (vs_battle_previewActionAgainstLimb into vs_battle_populateActionTargetPreview). Then the list (vs_battle_setSphereTargets), unk298C 240, camera 3.
pub fn vs_battle_collectSourceAreaActionTargets(_: c_int) callconv(.c) void {
    var origin: v.SVECTOR = undefined;
    var result: v.D_800F19CC_t2 = undefined;
    const actors = v.g.vs_battle_actors();
    const action: c_int = state().unk8.actionIndex;
    c.func_800A1AF8(state().unk8.primaryTarget.target.targetActor, 0, @ptrCast(&origin), 0);
    // vs_main_actions has 256 entries in the views; actionIndex is a u16 the C does not bound: psx.at.
    const spec = psx.at(v.g.actions(), action);
    state().unk29B0 = psx.getBits(spec, "shape");
    state().unk29B1 = 1;
    state().areaX = spec.rangeX;
    state().areaY = spec.rangeY;
    state().areaZ = spec.rangeZ;
    if (state().unk8.primaryTarget.target.targetActor != 0) {
        state().unk8.unk844 = unk10Vector(actors[state().unk8.primaryTarget.target.targetActor]);
        const dx = psx.conv(i16, @as(c_int, state().unk8.unk844.vx) - origin.vx);
        const dz = psx.conv(i16, @as(c_int, state().unk8.unk844.vz) - origin.vz);
        state().unk29B2 = psx.conv(i16, c.ratan2(dx, dz));
        const dy = psx.conv(i16, @as(c_int, state().unk8.unk844.vy) - origin.vy);
        const distance = c.vs_gte_rsqrt(@as(c_int, dx) *% dx +% @as(c_int, dz) *% dz); // two -0x8000s sum to 2^31: wraps
        state().unk29B7 = psx.conv(u8, @divTrunc(c.ratan2(dy, distance), 16) +% 64);
        state().areaOrigin = unk10Vector(actors[state().unk8.primaryTarget.target.targetActor]);
    } else {
        state().unk29B7 = 64;
        state().unk29B2 = 0;
    }
    state().areaOrigin = origin;
    if (state().unk2984 != 0) {
        c.func_800C0700(&state().unk29B0);
    }
    var count: c_int = 0;
    var i: c_int = 0;
    while (i < 16) : (i += 1) { // i < 16
        const status = c.vs_actors_actorStateFlags(i);
        const actor = actors[@intCast(i)];
        if (actor != null and
            (i != state().unk8.primaryTarget.target.targetActor or psx.GlobalMany(u8, "D_800E81B4").ptr()[kind(spec)] != 0) and
            (status & 0x3000001) == 1 and
            (@as(c_int, actor.?.allegiance) & psx.GlobalMany(u8, "D_800E81D4").ptr()[kind(spec)]) != 0 and
            (actor.?.allegiance & 0x8000) == 0)
        {
            // unk29C0 has 23 entries: the C does not bound the count (the views' targets alias them).
            if (v.g.vs_battle_categoryTargetsBodyParts()[kind(spec)] != 0) {
                var limb: c_int = 0;
                while (limb < 6) : (limb += 1) { // limb < 6
                    const part = c.func_800A152C(i, limb, 2);
                    if (part >= 0) {
                        _ = c.func_800A1B9C(i, limb, @ptrCast(psx.at(&state().previews, count)), 0);
                        if (c.vs_battle_isPointInTargetVolume(@ptrCast(&state().unk29B0), @ptrCast(psx.at(&state().previews, count))) != 0) {
                            psx.at(&state().previews, count).unk8 = psx.conv(u8, c.vs_actors_boneTagValue(i, part, 0));
                            psx.at(&state().previews, count).unkA = psx.conv(u8, limb);
                            psx.at(&state().previews, count).unk9 = psx.conv(u8, i);
                            _ = c.vs_battle_previewActionAgainstLimb(action, state().unk8.primaryTarget.target.targetActor, i, limb, &result);
                            c.vs_battle_populateActionTargetPreview(@ptrCast(psx.at(&state().previews, count)), @ptrCast(&result), @ptrCast(&result.targets[0]));
                            count += 1; // at most 96
                        }
                    }
                }
            } else {
                c.vs_actors_anchorPoint(i, 255, psx.at(&state().previews, count), 0);
                if (c.vs_battle_isPointInTargetVolume(@ptrCast(&state().unk29B0), @ptrCast(psx.at(&state().previews, count))) != 0) {
                    psx.at(&state().previews, count).unk8 = 16;
                    psx.at(&@as(*v.actionTargetContextView, @ptrCast(state())).targets, count).limb = -1;
                    psx.at(&state().previews, count).unk9 = psx.conv(u8, i);
                    _ = c.vs_battle_previewActionAgainstLimb(action, state().unk8.primaryTarget.target.targetActor, i,
                        psx.getBits(psx.get(actors[@intCast(i)].?.stats).?, "unk36"), &result);
                    c.vs_battle_populateActionTargetPreview(@ptrCast(psx.at(&state().previews, count)), @ptrCast(&result), @ptrCast(&result.targets[0]));
                    count += 1; // at most 96
                }
            }
        }
    }
    state().previewCount = psx.conv(u16, count);
    c.vs_battle_setSphereTargets(count, &state().previews, 1);
    state().unk298C = 240;
    v.g._cameraMode().* = 3;
}
