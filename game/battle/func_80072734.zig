//! An action's targeting: its area and the actors (or their limbs) in reach (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

/// orig: func_80072734 (src/BATTLE/BATTLE.PRG/146C.c)
/// Records the current action's shape, range, angle and area of effect and the attacker's facing; lists the
/// actors 0..9 the action may target (alive and targetable, the attacker only when its kind allows, of an
/// allegiance it reaches, not untargetable 0x8000): every limb with a part for limb-targeting kinds, else the
/// body; then the attacker's own point and position, the area preview (func_800C05EC) when unk2984 is set, and
/// camera mode 2.
pub fn func_80072734(arg0: c_int) callconv(.c) void {
    var actorState: v.func_8006EBF8_t = undefined;
    const actors = v.g.vs_battle_actors();
    const attacker = psx.get(actors[state().unk8.unk4.unk0.targetActor].?.stats);
    const action: c_int = state().unk8.actionIndex;
    state().unk2988 = arg0;
    // vs_main_actions has 256 entries in the views; actionIndex is a u16 the C does not bound: psx.at.
    state().unk29A0 = psx.getBits(psx.at(v.g.actions(), action), "shape");
    const spec = psx.at(v.g.actions(), action);
    state().unk29A1 = 2;
    _ = c.func_800A1108(state().unk8.unk4.unk0.targetActor, &actorState);
    state().unk29A2 = actorState.unk0.unk4.pad;
    var count: c_int = 0;
    // The C tests the whole range word's top 5 bits against 0xFF: never true.
    if ((psx.byteAt(u32, spec, 4).* >> 27) == 0xFF) {
        state().unk29A4 = psx.getBits(attacker.?, "currentRange");
        state().unk29A5 = psx.getBits(attacker.?, "unk39");
        state().unk29A6 = psx.getBits(attacker.?, "unk3A");
        state().unk29A7 = @as(u8, psx.getBits(attacker.?, "currentAttackShapeAngle")) << 3;
    } else {
        state().unk29A4 = spec.rangeX;
        state().unk29A5 = spec.rangeY;
        state().unk29A6 = spec.rangeZ;
        state().unk29A7 = @as(u8, psx.getBits(spec, "angle")) << 3;
        state().unk29B2 = 0;
        state().unk29B0 = spec.aoe_24 & 7;
        state().unk29B4 = spec.aoe_0;
        state().unk29B5 = spec.aoe_8;
        state().unk29B6 = spec.aoe_16;
        state().unk29B7 = spec.aoe_24 & 0xF8;
        const kind = psx.getBits(psx.at(v.g.actions(), action), "unk2_4");
        var i: c_int = 0;
        while (i < 10) : (i += 1) { // i < 10
            const status = c.func_800A0BE0(i);
            const actor = actors[@intCast(i)];
            if (actor != null and (status & 0x3000001) == 1 and
                (i != state().unk8.unk4.unk0.targetActor or psx.globalMany(u8, "D_800E81B4")[kind] != 0) and
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
                            psx.at(&state().unk29C0, count).unk8 = psx.conv(u8, c.func_800A1648(i, part, 0));
                            psx.at(&state().unk29C0, count).unkA = psx.conv(u8, limb);
                            psx.at(&state().unk29C0, count).unk9 = psx.conv(u8, i);
                            count += 1; // at most 60
                        }
                    }
                } else {
                    c.func_800A190C(i, 255, psx.at(&state().unk29C0, count), 0);
                    psx.at(&@as(*v.actionTargetContextView, @ptrCast(state())).targets, count).limb = -1;
                    psx.at(&state().unk29C0, count).unk9 = psx.conv(u8, i);
                    count += 1; // at most 60
                }
            }
        }
    }
    state().unk2C00 = psx.conv(u16, count);
    c.func_800A190C(state().unk8.unk4.unk0.targetActor, 250, &state().unk29A8, 0);
    if (state().unk8.unk4.unk0.targetActor != 0) {
        state().unk2998 = psx.byteAt(v.SVECTOR, actors[state().unk8.unk4.unk0.targetActor], 16).*;
        state().unk2998.pad = 0;
    }
    if (state().unk2984 != 0) {
        c.func_800C05EC(&state().unk29A0, &state().unk29B0, count, @bitCast(psx.addr(&state().unk29C0)));
    }
    v.g._cameraMode().* = 2;
}
