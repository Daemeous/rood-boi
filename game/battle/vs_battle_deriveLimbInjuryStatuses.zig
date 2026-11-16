//! Applies a hit's status effects to the target, decided by the limb HP after the hit (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_deriveLimbInjuryStatuses (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Applies the hit's status effects to the target's statuses, from the target's limb HP after the hit.
pub fn vs_battle_deriveLimbInjuryStatuses(arg0: ?*v._hitEntity_t) callconv(.c) void {
    const hit = arg0.?;
    const target = psx.get(v.g.vs_battle_actors()[hit.unk0.targetActor].?.stats).?;

    var a2: u16 = undefined;
    var a1: i16 = undefined;
    const tu34 = psx.getBits(target, "unk34");
    const tu35 = psx.getBits(target, "unk35");

    // unk34/unk35 are 8-bit indices into 6-entry limb arrays: the C does not bound them.
    switch (psx.getBits(hit, "hpEffect")) {
        2 => {
            a2 = psx.conv(u16, psx.conv(c_uint, psx.at(&target.limbs, tu34).hp) +%
                psx.conv(c_uint, psx.at(&hit.limbs, tu34).hp));
            a1 = psx.conv(i16, psx.conv(c_uint, psx.at(&target.limbs, tu35).hp) +%
                psx.conv(c_uint, psx.at(&hit.limbs, tu35).hp));
        },
        3, 1 => {
            a2 = psx.conv(u16, psx.conv(c_uint, psx.at(&target.limbs, tu34).hp) -%
                psx.conv(c_uint, psx.at(&hit.limbs, tu34).hp));
            a1 = psx.conv(i16, psx.conv(c_uint, psx.at(&target.limbs, tu35).hp) -%
                psx.conv(c_uint, psx.at(&hit.limbs, tu35).hp));
        },
        else => {
            a2 = psx.conv(u16, psx.at(&target.limbs, tu34).hp);
            a1 = psx.at(&target.limbs, tu35).hp;
        },
    }

    if ((target.statuses & 0x10) == 0) {
        if ((tu34 | tu35) != 0) {
            var v1: c_int = 0;
            if (tu34 != 0) {
                const shifted: c_int = @bitCast(@as(c_uint, a2) << 16);
                v1 = @intFromBool(shifted > 0x10000);
            }
            if (tu35 != 0 and a1 >= 2) {
                v1 = 1;
            }
            if (v1 == 0) {
                _ = c._setApplicableStatuses(4, @ptrCast(arg0));
            }
        }
    } else {
        if ((tu34 | tu35) != 0) {
            var v1: c_int = 0;
            if (tu34 != 0) {
                const shifted: c_int = @bitCast(@as(c_uint, a2) << 16);
                v1 = @intFromBool(shifted > 0x10000);
            }
            if (tu35 != 0 and a1 >= 2) {
                v1 = 1;
            }
            if (v1 != 0) {
                _ = c._setInflictedStatuses(4, @ptrCast(arg0));
            }
        }
    }

    if (hit.unk0.targetActor == 0) {
        var i: c_int = 0;
        while (i < 4) : (i +%= 1) {
            const li: usize = @intCast(i);
            switch (psx.getBits(hit, "hpEffect")) {
                2 => {
                    a2 = psx.conv(u16, psx.conv(c_uint, target.limbs[li].hp) +%
                        psx.conv(c_uint, hit.limbs[li].hp));
                },
                3, 1 => {
                    a2 = psx.conv(u16, psx.conv(c_uint, target.limbs[li].hp) -%
                        psx.conv(c_uint, hit.limbs[li].hp));
                },
                else => {
                    a2 = psx.conv(u16, target.limbs[li].hp);
                },
            }
            const param = v.g.vs_main_statusEffectParams()[v.g.D_800E8200()[li]][0];
            if ((target.statuses & param) == 0) {
                const shifted: c_int = @bitCast(@as(c_uint, a2) << 16);
                const v1: c_int = @intFromBool(0x10000 < shifted);
                if (v1 == 0) {
                    _ = c._setApplicableStatuses(@as(c_int, v.g.D_800E8200()[li]), @ptrCast(arg0));
                }
            } else {
                const shifted: c_int = @bitCast(@as(c_uint, a2) << 16);
                const v1: c_int = @intFromBool(shifted > 0x10000);
                if (v1 != 0) {
                    _ = c._setInflictedStatuses(@as(c_int, v.g.D_800E8200()[li]), @ptrCast(arg0));
                }
            }
        }
    }
}
