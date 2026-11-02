//! A melee action's targeting: its range, the actors (or limbs) and chests inside it, the user's last target (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

/// The u16 at byte `offset` of the user actor (its selected target and limb).
fn userHalf(offset: isize) c_int {
    return psx.byteAt(u16, v.g.vs_battle_actors()[state().unk8.unk4.unk0.targetActor], offset).*;
}

/// orig: func_80071F68 (src/BATTLE/BATTLE.PRG/146C.c)
/// Records the action's shape, the user's facing and its range (the user's own for 0xFF), previews it; lists the
/// actors 0..15 it may target that the range covers (each limb with a part for limb-targeting kinds, else the
/// body) with their hit previews, then, for kinds that reach objects (0x80), the closed chests (unk10 < 64)
/// inside it (a trapped chest, kind 2 or 3 untouched, carries the user's risk); the list preview; and for an
/// enemy user, the entry it targeted last (its target and limb, else its target). Camera mode 1.
pub fn func_80071F68(arg0: c_int) callconv(.c) void {
    var actorState: v.func_8006EBF8_t = undefined;
    var result: v.D_800F19CC_t2 = undefined;
    const actors = v.g.vs_battle_actors();
    const action: c_int = state().unk8.actionIndex;
    state().unk2988 = arg0;
    // vs_main_actions has 256 entries in the views; actionIndex is a u16 the C does not bound: psx.at.
    state().unk29A0 = psx.getBits(psx.at(v.g.actions(), action), "shape");
    const spec = psx.at(v.g.actions(), action);
    state().unk29A1 = 1;
    _ = c.func_800A1108(state().unk8.unk4.unk0.targetActor, &actorState);
    state().unk29A2 = actorState.unk0.unk4.pad;
    const attacker = psx.get(actors[state().unk8.unk4.unk0.targetActor].?.stats);
    if (spec.rangeX == 0xFF) {
        state().unk29A4 = psx.getBits(attacker.?, "currentRange");
        state().unk29A5 = psx.getBits(attacker.?, "unk39");
        state().unk29A6 = psx.getBits(attacker.?, "unk3A");
        state().unk29A7 = @as(u8, psx.getBits(attacker.?, "currentAttackShapeAngle")) << 3;
    } else {
        state().unk29A4 = spec.rangeX;
        state().unk29A5 = spec.rangeY;
        state().unk29A6 = spec.rangeZ;
        state().unk29A7 = @as(u8, psx.getBits(spec, "angle")) << 3;
    }
    c.func_800A190C(state().unk8.unk4.unk0.targetActor, 250, &state().unk29A8, 0);
    if (state().unk2984 != 0) {
        c.func_800C05EC(&state().unk29A0, null, 0, 0);
    }
    const kind = psx.getBits(psx.at(v.g.actions(), action), "unk2_4");
    var count: c_int = 0;
    var i: c_int = 0;
    while (i < 16) : (i += 1) { // i < 16
        const status = c.func_800A0BE0(i);
        const actor = actors[@intCast(i)];
        if (actor != null and
            (i != state().unk8.unk4.unk0.targetActor or psx.globalMany(u8, "D_800E81B4")[kind] != 0) and
            (status & 0x3000001) == 1 and
            (@as(c_int, actor.?.allegiance) & psx.globalMany(u8, "D_800E81D4")[kind]) != 0 and
            (actor.?.allegiance & 0x8000) == 0)
        {
            // unk29C0 has 23 entries: the C does not bound the count (the views' targets alias them).
            if (v.g.D_800E81C4()[kind] != 0) {
                var limb: c_int = 0;
                while (limb < 6) : (limb += 1) { // limb < 6
                    const part = c.func_800A152C(i, limb, 2);
                    if (part >= 0) {
                        _ = c.func_800A1B9C(i, limb, @ptrCast(psx.at(&state().unk29C0, count)), 0);
                        if (c.func_800C1564(@ptrCast(&state().unk29A0), @ptrCast(psx.at(&state().unk29C0, count))) != 0) {
                            psx.at(&state().unk29C0, count).unk8 = psx.conv(u8, c.func_800A1648(i, part, 0));
                            psx.at(&state().unk29C0, count).unkA = psx.conv(u8, limb);
                            psx.at(&state().unk29C0, count).unk9 = psx.conv(u8, i);
                            _ = c.func_8008631C(action, state().unk8.unk4.unk0.targetActor, i, limb, &result);
                            c.func_8006F630(@ptrCast(psx.at(&state().unk29C0, count)), @ptrCast(&result), @ptrCast(&result.unk4C[0]));
                            count += 1; // at most 96
                        }
                    }
                }
            } else {
                c.func_800A190C(i, 255, psx.at(&state().unk29C0, count), 0);
                if (c.func_800C1564(@ptrCast(&state().unk29A0), @ptrCast(psx.at(&state().unk29C0, count))) != 0) {
                    psx.at(&state().unk29C0, count).unk8 = 16;
                    psx.at(&@as(*v.actionTargetContextView, @ptrCast(state())).targets, count).limb = -1;
                    psx.at(&state().unk29C0, count).unk9 = psx.conv(u8, i);
                    _ = c.func_8008631C(action, state().unk8.unk4.unk0.targetActor, i,
                        psx.getBits(psx.get(actors[@intCast(i)].?.stats).?, "unk36"), &result);
                    c.func_8006F630(@ptrCast(psx.at(&state().unk29C0, count)), @ptrCast(&result), @ptrCast(&result.unk4C[0]));
                    count += 1; // at most 96
                }
            }
        }
    }
    if (psx.globalMany(u8, "D_800E81D4")[kind] & 0x80 != 0) {
        const chests: [*c]v._mpdRoomSection13 = @ptrCast(@alignCast(c.func_8008E370(&i)));
        var j: c_int = 0;
        while (j < i) : (j += 1) { // j < an int
            const chest = &chests[@intCast(j)];
            const target = psx.at(&state().unk29C0, count);
            target.unk0.vx = psx.conv(i16, @as(c_int, chest.unk0) *% 128 +% 64);
            target.unk0.vz = psx.conv(i16, @as(c_int, chest.unk2) *% 128 +% 64);
            target.unk0.vy = psx.conv(i16, (@as(c_int, c.func_8008DC7C((@as(c_int, chest.unk0) << 7) | 64, (@as(c_int, chest.unk2) << 7) | 64)) << 17) >> 17);
            if (chest.unk10 < 64 and c.func_800C1564(@ptrCast(&state().unk29A0), @ptrCast(target)) != 0) {
                target.unk8 = 16;
                target.unkA = 16;
                target.unk9 = psx.conv(u8, j | 0x20);
                psx.byteAt(i16, target, 12).* = 0;
                if (psx.conv(c_uint, @as(c_int, @as(u16, @bitCast(chest.unkC))) - 2) < 2 and chest.unk8 == 0) {
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
    state().unk2C00 = psx.conv(u16, count);
    if (state().unk2984 != 0) {
        c.func_800C1664(count, @bitCast(psx.addr(&state().unk29C0)), 0);
    }
    if (state().unk8.unk4.unk0.targetActor != 0) {
        count = 0;
        while (count < state().unk2C00) : (count += 1) { // count < a u16
            if (@as(c_int, psx.at(&state().unk29C0, count).unk9) == userHalf(0x10) and
                @as(c_int, psx.at(&@as(*v.actionTargetContextView, @ptrCast(state())).targets, count).limb) == userHalf(0x12))
            {
                state().unk298C = psx.conv(u8, count);
            }
        }
        if (state().unk298C == 0xFF) {
            count = 0;
            while (count < state().unk2C00) : (count += 1) { // count < a u16
                if (@as(c_int, psx.at(&state().unk29C0, count).unk9) == userHalf(0x10)) {
                    state().unk298C = psx.conv(u8, count);
                }
            }
        }
    }
    v.g._cameraMode().* = 1;
}
