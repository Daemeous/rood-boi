//! A melee action's targeting: its range, the actors (or limbs) and chests inside it, the user's last target (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// vs_battle_actionState, read at each use as the C does.
fn state() *v.vs_battle_ActionState {
    return v.g.vs_battle_actionState().*.?;
}

/// A half of the user actor's unk10 word (its selected target and limb).
fn userHalf(high: bool) c_int {
    const word: u32 = @bitCast(v.g.vs_battle_actors()[state().unk8.primaryTarget.target.targetActor].?.unk10.s32);
    return @intCast(if (high) word >> 16 else word & 0xFFFF);
}

/// orig: vs_battle_collectDirectActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Records the action's shape, the user's facing and its range (the user's own for 0xFF), previews it; lists the
/// actors 0..15 it may target that the range covers (each limb with a part for limb-targeting kinds, else the
/// body) with their hit previews, then, for kinds that reach objects (0x80), the closed chests (unk10 < 64)
/// inside it (a trapped chest, kind 2 or 3 untouched, carries the user's risk); the list preview; and for an
/// enemy user, the entry it targeted last (its target and limb, else its target). Camera mode 1.
pub fn vs_battle_collectDirectActionTargets(arg0: c_int) callconv(.c) void {
    var actorState: v.func_8006EBF8_t = undefined;
    var result: v.vs_battle_ActionRequest = undefined;
    const actors = v.g.vs_battle_actors();
    const action: c_int = state().unk8.actionIndex;
    state().unk2988 = arg0;
    // vs_main_actions has 256 entries in the views; actionIndex is a u16 the C does not bound: psx.at.
    state().unk29A0 = psx.getBits(psx.at(v.g.actions(), action), "shape");
    const spec = psx.at(v.g.actions(), action);
    state().unk29A1 = 1;
    _ = c.vs_actors_actorPlacement(state().unk8.primaryTarget.target.targetActor, &actorState);
    state().unk29A2 = actorState.location.position.pad;
    const attacker = psx.get(actors[state().unk8.primaryTarget.target.targetActor].?.stats);
    if (spec.rangeX == 0xFF) {
        state().rangeX = psx.getBits(attacker.?, "currentRange");
        state().rangeY = psx.getBits(attacker.?, "unk39");
        state().rangeZ = psx.getBits(attacker.?, "unk3A");
        state().rangeAngle = @as(u8, psx.getBits(attacker.?, "currentAttackShapeAngle")) << 3;
    } else {
        state().rangeX = spec.rangeX;
        state().rangeY = spec.rangeY;
        state().rangeZ = spec.rangeZ;
        state().rangeAngle = @as(u8, psx.getBits(spec, "angle")) << 3;
    }
    c.vs_actors_anchorPoint(state().unk8.primaryTarget.target.targetActor, 250, &state().rangeOrigin, 0);
    if (state().unk2984 != 0) {
        c.func_800C05EC(&state().unk29A0, null, 0, null);
    }
    const kind = psx.getBits(psx.at(v.g.actions(), action), "unk2_4");
    var count: c_int = 0;
    var i: c_int = 0;
    while (i < 16) : (i += 1) { // i < 16
        const status = c.vs_actors_actorStateFlags(i);
        const actor = actors[@intCast(i)];
        if (actor != null and
            (i != state().unk8.primaryTarget.target.targetActor or psx.GlobalMany(u8, "D_800E81B4").ptr()[kind] != 0) and
            (status & 0x3000001) == 1 and
            (@as(c_int, actor.?.allegiance) & psx.GlobalMany(u8, "D_800E81D4").ptr()[kind]) != 0 and
            (actor.?.allegiance & 0x8000) == 0)
        {
            // unk29C0 has 23 entries: the C does not bound the count (the views' targets alias them).
            if (v.g.vs_battle_categoryTargetsBodyParts()[kind] != 0) {
                var limb: c_int = 0;
                while (limb < 6) : (limb += 1) { // limb < 6
                    const part = c.func_800A152C(i, limb, 2);
                    if (part >= 0) {
                        _ = c.func_800A1B9C(i, limb, @ptrCast(psx.at(&state().previews, count)), 0);
                        if (c.vs_battle_isPointInTargetVolume(@ptrCast(&state().unk29A0), @ptrCast(psx.at(&state().previews, count))) != 0) {
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
                if (c.vs_battle_isPointInTargetVolume(@ptrCast(&state().unk29A0), @ptrCast(psx.at(&state().previews, count))) != 0) {
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
    if (psx.GlobalMany(u8, "D_800E81D4").ptr()[kind] & 0x80 != 0) {
        const chests: [*c]v._mpdRoomSection13 = @ptrCast(@alignCast(c.vs_items_roomTreasureChests(&i)));
        var j: c_int = 0;
        while (j < i) : (j += 1) { // j < an int
            const chest = &chests[@intCast(j)];
            const target = psx.at(&state().previews, count);
            target.unk0.vx = psx.conv(i16, @as(c_int, chest.tileX) *% 128 +% 64);
            target.unk0.vz = psx.conv(i16, @as(c_int, chest.tileZ) *% 128 +% 64);
            target.unk0.vy = psx.conv(i16, (@as(c_int, c.vs_field_floorHeightAt((@as(c_int, chest.tileX) << 7) | 64, (@as(c_int, chest.tileZ) << 7) | 64)) << 17) >> 17);
            if (chest.openedFlag < 64 and c.vs_battle_isPointInTargetVolume(@ptrCast(&state().unk29A0), @ptrCast(target)) != 0) {
                target.unk8 = 16;
                target.unkA = 16;
                target.unk9 = psx.conv(u8, j | 0x20);
                psx.byteAt(i16, target, 12).* = 0;
                if (psx.conv(c_uint, @as(c_int, @as(u16, @bitCast(chest.unkC))) - 2) < 2 and chest.isOpen == 0) {
                    target.unkE = psx.conv(i16, @divTrunc((@as(c_int, attacker.?.risk) + 150) * 100, 256)); // a u16: no overflow
                } else {
                    target.unkE = 0;
                }
                count += 1; // at most 96 + chests
                const bytes: [*]u8 = @ptrCast(target);
                bytes[20] = 10;
                bytes[23] = 0;
                bytes[22] = 0;
                bytes[21] = 0;
                target.unk10 = 0;
                target.unkB = 0;
            }
        }
    }
    state().previewCount = psx.conv(u16, count);
    if (state().unk2984 != 0) {
        c.vs_battle_setSphereTargets(count, &state().previews, 0);
    }
    if (state().unk8.primaryTarget.target.targetActor != 0) {
        count = 0;
        while (count < state().previewCount) : (count += 1) { // count < a u16
            if (@as(c_int, psx.at(&state().previews, count).unk9) == userHalf(false) and
                @as(c_int, psx.at(&@as(*v.actionTargetContextView, @ptrCast(state())).targets, count).limb) == userHalf(true))
            {
                state().unk298C = psx.conv(u8, count);
            }
        }
        if (state().unk298C == 0xFF) {
            count = 0;
            while (count < state().previewCount) : (count += 1) { // count < a u16
                if (@as(c_int, psx.at(&state().previews, count).unk9) == userHalf(false)) {
                    state().unk298C = psx.conv(u8, count);
                }
            }
        }
    }
    v.g._cameraMode().* = 1;
}
