//! Resolves the hits of one battle action into its result record
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_resolveActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resolves action `action`'s hits against its targets into `result`; with `arg3` set, it also commits the result to `source` and charges the action.
pub fn vs_battle_resolveActionTargets(action: c_int, result: ?*v.D_800F19CC_t2, source: ?*v.D_800F19CC_t2, arg3: c_int) callconv(.c) void {
    const spec = psx.at(v.g.actions(), action);
    c.vs_battle_clearActionRequest(result);
    const res = result.?;
    res.actionIndex = psx.conv(u16, action);
    c.vs_battle_initializeActionHit(@ptrCast(&res.unk4));
    const src = source.?;
    res.unk4.unk40 = src.unk4.unk40;

    // origin is read in the original even when unk40 != 0 (uninitialised there): zeroed here.
    var origin: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    var position: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    var impact: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    var attacker: ?*v.vs_battle_actor2 = null;
    var target: c_int = 0;
    var limb: c_int = 0;
    var i: c_int = 0;

    if (res.unk4.unk40 == 0) {
        res.unk4.unk0.targetActor = src.unk4.unk0.targetActor;
        res.unk4.unk0.targetLimb = src.unk4.unk0.targetLimb;
        attacker = psx.get(v.g.vs_battle_actors()[res.unk4.unk0.targetActor].?.stats);
        c.func_800A1AF8(@as(c_int, res.unk4.unk0.targetActor), 0, psx.cptr(c.SVECTOR, &origin), 0);
    } else {
        attacker = null;
    }

    res.unk4A = src.unk4A;
    while (i < src.unk4A) : (i +%= 1) {
        const dst = psx.at(&res.unk4C, i);
        const from = psx.at(&src.unk4C, i);
        c.vs_battle_initializeActionHit(@ptrCast(dst));
        dst.unk40 = from.unk40;
        if (dst.unk40 == 0) {
            dst.unk0.targetActor = from.unk0.targetActor;
            dst.unk0.targetLimb = from.unk0.targetLimb;
        } else if (psx.conv(c_uint, dst.unk40) < 6) {
            const dstVec: *v.SVECTOR = @ptrCast(&dst.unk0);
            dstVec.* = @as(*v.SVECTOR, @ptrCast(&from.unk0)).*;
        }
    }
    res.unk844 = src.unk844;

    var best: c_int = 0;
    var max: i16 = 0;
    if (psx.getBits(&spec.hitParams[0], "affinity") != 0) {
        best = @as(c_int, psx.getBits(&spec.hitParams[0], "affinity")) -% 1;
    } else if (attacker != null) {
        const att = attacker.?;
        if ((v.g.vs_battle_actors()[att.unk957].?.weaponDrawn & 1) != 0 and att.weapon.blade.id != 0) {
            var k: c_int = 0;
            while (k < 7) : (k += 1) {
                const aff = att.weapon.classAffinityCurrent.affinity[0][@intCast(k)];
                if (max < aff) {
                    max = aff;
                    best = k;
                }
            }
        }
    } else {
        best = 0;
    }
    res.unk2 = psx.conv(u8, best);

    var params = spec.hitParams[0];
    const calc = psx.getBits(&params, "statCalculator");
    const ty = psx.getBits(&params, "type");
    if (calc == 2 or calc == 25) {
        if (ty == 0) {
            if (attacker.?.weapon.blade.id != 0) {
                res.unk3 = attacker.?.weapon.damageType;
            } else {
                res.unk3 = 1;
            }
        } else {
            res.unk3 = psx.conv(u8, ty);
        }
    } else {
        res.unk3 = psx.conv(u8, ty);
    }

    var hit: c_int = 0;
    const user: *v._hitEntity_t = &res.unk4;
    v.g._lastValue().* = 0;
    v.g.D_800F1A04().* = 0;
    v.g.D_800F1A00().* = 0;
    while (hit < 2) : (hit += 1) {
        sw: switch (psx.getBits(&spec.hitParams[@intCast(hit)], "prerequisiteFunction")) {
            6, 7, 8 => {
                c.vs_battle_applyActionHitEffect(@ptrCast(spec), @ptrCast(&res.unk4), @ptrCast(&res.unk4C[0]), arg3, hit);
            },
            3 => {
                if (res.unk4.unk40 != 0) break :sw;
                const entry: *v._hitEntity_t = &res.unk4;
                const stats = psx.get(v.g.vs_battle_actors()[res.unk4.unk0.targetActor].?.stats);
                v.g.D_800F1A00().* = 1;
                v.g.D_800F1A04().* = psx.conv(u32, c._doesAttackHit(@ptrCast(spec), @ptrCast(entry), @ptrCast(entry), hit, arg3));
                if (v.g.D_800F1A04().* != 0 or arg3 == 0) {
                    c.vs_battle_applyActionHitEffect(@ptrCast(spec), @ptrCast(entry), @ptrCast(entry), arg3, hit);
                    if (arg3 != 0 and psx.getBits(stats.?, "unk956_0") != 0) {
                        c.vs_battle_initializeActionHit(@ptrCast(entry));
                        entry.unk0.unk3 = 4;
                    }
                    c.vs_battle_deriveLimbInjuryStatuses(@ptrCast(entry));
                }
            },
            0 => {},
            else => {
                i = 0;
                while (i < res.unk4A) : (i +%= 1) {
                    const entry = psx.at(&res.unk4C, i);
                    var redirected: c_int = 0;
                    if (arg3 != 0 and psx.getBits(spec, "flagsD_6") != 0) {
                        if (entry.unk40 == 0) {
                            c.vs_battle_actionTargetLimbPosition(entry.unk0.targetActor, entry.unk0.targetLimb, psx.cptr(c.SVECTOR, &position));
                        } else {
                            position = @as(*v.SVECTOR, @ptrCast(&entry.unk0)).*;
                        }
                        redirected = c.vs_field_traceActorAndTerrainCollision(
                            psx.cptr(c.SVECTOR, &origin),
                            psx.cptr(c.SVECTOR, &position),
                            psx.cptr(c.SVECTOR, &impact),
                            @as(c_int, res.unk4.unk0.targetActor),
                        );
                        if (redirected != 0) {
                            if (impact.pad == 0) {
                                entry.unk40 = 1;
                                @as(*v.SVECTOR, @ptrCast(&entry.unk0)).* = impact;
                                entry.unk0.mp = 0x80;
                                res.unk48 = 1;
                            } else {
                                if (psx.conv(u16, impact.pad) < 17) {
                                    target = impact.pad;
                                    if (target -% 1 != @as(c_int, entry.unk0.targetActor)) {
                                        entry.unk0.targetActor = psx.conv(u8, target -% 1);
                                        entry.unk0.targetLimb = psx.conv(i8, psx.getBits(psx.get(v.g.vs_battle_actors()[entry.unk0.targetActor].?.stats).?, "unk36"));
                                        res.unk48 = 1;
                                    }
                                }
                                redirected = 0;
                            }
                        }
                    }
                    if (redirected != 0) continue;
                    if (entry.unk40 != 0) continue;
                    const stats = psx.get(v.g.vs_battle_actors()[entry.unk0.targetActor].?.stats);
                    v.g.D_800F1A00().* = psx.conv(u32, c._canPerformAttack(@ptrCast(spec), @ptrCast(entry), hit));
                    if (v.g.D_800F1A00().* != 0) {
                        v.g.D_800F1A04().* = psx.conv(u32, c._doesAttackHit(@ptrCast(spec), @ptrCast(user), @ptrCast(entry), hit, arg3));
                        if (v.g.D_800F1A04().* != 0 or arg3 == 0) {
                            c.vs_battle_applyActionHitEffect(@ptrCast(spec), @ptrCast(user), @ptrCast(entry), arg3, hit);
                            target = entry.unk0.targetActor;
                            if (v.g.vs_battle_actors()[@intCast(target)].?.unk27 == 0x80 and target != 0 and psx.getBits(stats.?, "unk956_0") != 0) {
                                limb = psx.conv(u8, entry.unk0.targetLimb);
                                c.vs_battle_initializeActionHit(@ptrCast(entry));
                                entry.unk0.targetActor = psx.conv(u8, target);
                                entry.unk0.unk3 = 4;
                                entry.unk0.targetLimb = psx.conv(i8, limb);
                            }
                            if (arg3 != 0 and psx.getBits(stats.?, "unk956_0") != 0) {
                                limb = psx.conv(u8, entry.unk0.targetLimb);
                                target = entry.unk0.targetActor;
                                c.vs_battle_initializeActionHit(@ptrCast(entry));
                                entry.unk0.unk3 = 4;
                                entry.unk0.targetActor = psx.conv(u8, target);
                                entry.unk0.targetLimb = psx.conv(i8, limb);
                            }
                            c.vs_battle_deriveLimbInjuryStatuses(@ptrCast(entry));
                        }
                    } else if (hit == 0) {
                        entry.unk0.hitThreshold = 0xFF;
                    }
                }
            },
        }
    }

    c.vs_battle_finalizeActionHit(@ptrCast(&res.unk4));
    i = 0;
    while (i < res.unk4A) : (i +%= 1) {
        c.vs_battle_finalizeActionHit(@ptrCast(psx.at(&res.unk4C, i)));
    }
    if (v.g.vs_battle_actionState().*.?.unk0 == 0 and arg3 != 0) {
        src.* = res.*;
    }
    if (arg3 != 0 and attacker != null) {
        c.vs_battle_addActionRecoveryDelay(psx.conv(c_uint, action), @ptrCast(attacker), 1);
        _ = c._getActionCost(psx.conv(c_uint, action), @ptrCast(attacker), 1);
    }
}
