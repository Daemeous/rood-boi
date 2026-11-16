//! An action's targeting: its area and the actors (or their limbs) in reach (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
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

/// orig: vs_battle_collectGroundAreaActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Records the current action's shape, range, angle and area of effect and the attacker's facing; lists the
/// actors 0..9 the action may target (alive and targetable, the attacker only when its kind allows, of an
/// allegiance it reaches, not untargetable 0x8000): every limb with a part for limb-targeting kinds, else the
/// body; then the attacker's own point and position, the area preview (func_800C05EC) when unk2984 is set, and
/// camera mode 2.
pub fn vs_battle_collectGroundAreaActionTargets(arg0: c_int) callconv(.c) void {
    var actorState: v.func_8006EBF8_t = undefined;
    const actors = v.g.vs_battle_actors();
    const attacker = psx.get(actors[state().unk8.primaryTarget.target.targetActor].?.stats);
    const action: c_int = state().unk8.actionIndex;
    state().unk2988 = arg0;
    // vs_main_actions has 256 entries in the views; actionIndex is a u16 the C does not bound: psx.at.
    state().unk29A0 = psx.getBits(psx.at(v.g.actions(), action), "shape");
    const spec = psx.at(v.g.actions(), action);
    state().unk29A1 = 2;
    _ = c.vs_actors_actorPlacement(state().unk8.primaryTarget.target.targetActor, &actorState);
    state().unk29A2 = actorState.location.position.pad;
    var count: c_int = 0;
    // The C tests the whole range word's top 5 bits against 0xFF: never true.
    if ((psx.byteAt(u32, spec, 4).* >> 27) == 0xFF) {
        state().rangeX = psx.getBits(attacker.?, "currentRange");
        state().rangeY = psx.getBits(attacker.?, "unk39");
        state().rangeZ = psx.getBits(attacker.?, "unk3A");
        state().rangeAngle = @as(u8, psx.getBits(attacker.?, "currentAttackShapeAngle")) << 3;
    } else {
        state().rangeX = spec.rangeX;
        state().rangeY = spec.rangeY;
        state().rangeZ = spec.rangeZ;
        state().rangeAngle = @as(u8, psx.getBits(spec, "angle")) << 3;
        state().unk29B2 = 0;
        state().unk29B0 = spec.aoe_24 & 7;
        state().areaX = spec.aoe_0;
        state().areaY = spec.aoe_8;
        state().areaZ = spec.aoe_16;
        state().unk29B7 = spec.aoe_24 & 0xF8;
        const kind = psx.getBits(psx.at(v.g.actions(), action), "unk2_4");
        var i: c_int = 0;
        while (i < 10) : (i += 1) { // i < 10
            const status = c.vs_actors_actorStateFlags(i);
            const actor = actors[@intCast(i)];
            if (actor != null and (status & 0x3000001) == 1 and
                (i != state().unk8.primaryTarget.target.targetActor or psx.GlobalMany(u8, "D_800E81B4").ptr()[kind] != 0) and
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
                            psx.at(&state().previews, count).unk8 = psx.conv(u8, c.vs_actors_boneTagValue(i, part, 0));
                            psx.at(&state().previews, count).unkA = psx.conv(u8, limb);
                            psx.at(&state().previews, count).unk9 = psx.conv(u8, i);
                            count += 1; // at most 60
                        }
                    }
                } else {
                    c.vs_actors_anchorPoint(i, 255, psx.at(&state().previews, count), 0);
                    psx.at(&@as(*v.actionTargetContextView, @ptrCast(state())).targets, count).limb = -1;
                    psx.at(&state().previews, count).unk9 = psx.conv(u8, i);
                    count += 1; // at most 60
                }
            }
        }
    }
    state().previewCount = psx.conv(u16, count);
    c.vs_actors_anchorPoint(state().unk8.primaryTarget.target.targetActor, 250, &state().rangeOrigin, 0);
    if (state().unk8.primaryTarget.target.targetActor != 0) {
        state().unk2998 = unk10Vector(actors[state().unk8.primaryTarget.target.targetActor]);
        state().unk2998.pad = 0;
    }
    if (state().unk2984 != 0) {
        c.func_800C05EC(&state().unk29A0, &state().unk29B0, count, &state().previews);
    }
    v.g._cameraMode().* = 2;
}
