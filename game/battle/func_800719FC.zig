//! A ranged action's targeting preview (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

/// The action's kind (unk2_4) as the C reads it: bits 20..23 of its first word.
fn kind(spec: *v.vs_action_t) u32 {
    return (psx.byteAt(u32, spec, 0).* >> 20) & 15;
}

/// orig: func_800719FC (src/BATTLE/BATTLE.PRG/146C.c)
/// Aims the current action's range from its user (func_800A1AF8) at the targeted actor (level when it targets
/// the user itself), previews it (func_800C0700) when unk2984 is set, then lists every actor 0..15 the action may
/// target and the range covers (func_800C1564): each limb with a part for limb-targeting kinds, else the body,
/// with its hit preview (func_8008631C into func_8006F630). Then the list (func_800C1664), unk298C 240, camera 3.
pub fn func_800719FC(_: c_int) callconv(.c) void {
    var origin: v.SVECTOR = undefined;
    var result: v.D_800F19CC_t2 = undefined;
    const actors = v.g.vs_battle_actors();
    const action: c_int = state().unk8.actionIndex;
    c.func_800A1AF8(state().unk8.unk4.unk0.targetActor, 0, @ptrCast(&origin), 0);
    // vs_main_actions has 256 entries in the views; actionIndex is a u16 the C does not bound: psx.at.
    const spec = psx.at(v.g.actions(), action);
    state().unk29B0 = psx.getBits(spec, "shape");
    state().unk29B1 = 1;
    state().unk29B4 = spec.rangeX;
    state().unk29B5 = spec.rangeY;
    state().unk29B6 = spec.rangeZ;
    if (state().unk8.unk4.unk0.targetActor != 0) {
        state().unk8.unk844 = psx.byteAt(v.SVECTOR, actors[state().unk8.unk4.unk0.targetActor], 16).*;
        const dx = psx.conv(i16, @as(c_int, state().unk8.unk844.vx) - origin.vx);
        const dz = psx.conv(i16, @as(c_int, state().unk8.unk844.vz) - origin.vz);
        state().unk29B2 = psx.conv(i16, c.ratan2(dx, dz));
        const dy = psx.conv(i16, @as(c_int, state().unk8.unk844.vy) - origin.vy);
        const distance = c.vs_gte_rsqrt(@as(c_int, dx) *% dx +% @as(c_int, dz) *% dz); // two -0x8000s sum to 2^31: wraps
        state().unk29B7 = psx.conv(u8, @divTrunc(c.ratan2(dy, distance), 16) +% 64);
        state().unk29B8 = psx.byteAt(v.SVECTOR, actors[state().unk8.unk4.unk0.targetActor], 16).*;
    } else {
        state().unk29B7 = 64;
        state().unk29B2 = 0;
    }
    state().unk29B8 = origin;
    if (state().unk2984 != 0) {
        c.func_800C0700(&state().unk29B0);
    }
    var count: c_int = 0;
    var i: c_int = 0;
    while (i < 16) : (i += 1) { // i < 16
        const status = c.func_800A0BE0(i);
        const actor = actors[@intCast(i)];
        if (actor != null and
            (i != state().unk8.unk4.unk0.targetActor or psx.globalMany(u8, "D_800E81B4")[kind(spec)] != 0) and
            (status & 0x3000001) == 1 and
            (@as(c_int, actor.?.allegiance) & psx.globalMany(u8, "D_800E81D4")[kind(spec)]) != 0 and
            (actor.?.allegiance & 0x8000) == 0)
        {
            // unk29C0 has 23 entries: the C does not bound the count (the views' targets alias them).
            if (v.g.D_800E81C4()[kind(spec)] != 0) {
                var limb: c_int = 0;
                while (limb < 6) : (limb += 1) { // limb < 6
                    const part = c.func_800A152C(i, limb, 2);
                    if (part >= 0) {
                        _ = c.func_800A1B9C(i, limb, @ptrCast(psx.at(&state().unk29C0, count)), 0);
                        if (c.func_800C1564(@ptrCast(&state().unk29B0), @ptrCast(psx.at(&state().unk29C0, count))) != 0) {
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
                if (c.func_800C1564(@ptrCast(&state().unk29B0), @ptrCast(psx.at(&state().unk29C0, count))) != 0) {
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
    state().unk2C00 = psx.conv(u16, count);
    c.func_800C1664(count, @bitCast(psx.addr(&state().unk29C0)), 1);
    state().unk298C = 240;
    v.g._cameraMode().* = 3;
}
