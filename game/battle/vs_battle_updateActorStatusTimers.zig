//! Status effects per actor: ticks the status timers, applies the timed HP effects and reports the statuses still in effect (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updateActorStatusTimers (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Ticks an actor's status timers, applies the timed poison and regen effects, and sets the statuses still in effect.
pub fn vs_battle_updateActorStatusTimers(actor: ?*v.vs_battle_actor2) callconv(.c) void {
    const a = actor.?;
    var statuses: c_int = 0;

    if (a.statusTimers[0] != 0) {
        a.statusTimers[0] -%= 1;
        if (a.statusTimers[0] == 0) statuses = a.statuses & 0x7E0;
    }

    if (a.statusTimers[1] != 0) {
        a.statusTimers[1] -%= 1;
        if (a.statusTimers[1] == 0) statuses |= a.statuses & 0x810;
    }

    if (a.statusTimers[2] != 0) {
        a.statusTimers[2] -%= 1;
        if (a.statusTimers[2] == 0) statuses |= a.statuses & 0x1FF80000;
    }

    if (a.statusTimers[3] != 0) {
        a.statusTimers[3] -%= 1;
        if (a.statusTimers[3] == 0) statuses |= a.statuses & 0x2000;
    }

    if (a.statusTimers[4] != 0) {
        a.statusTimers[4] -%= 1;
        if (a.statusTimers[4] == 0) statuses |= a.statuses & 0x8000;
    }

    if (a.statusTimers[5] != 0) {
        a.statusTimers[5] -%= 1;
        if (a.statusTimers[5] == 0) {
            if ((a.statuses & 0x4000) != 0) {
                // poison: lose HP, the damage is flagged in the call's hp argument
                a.statusTimers[5] = v.g.vs_battle_statusParams14TickInterval().*;
                var hp: c_int = @divTrunc(@as(c_int, a.currentHP), 20) +% c.vs_main_getRand(6);
                if (hp <= 0) hp = 1;
                if (@as(c_int, a.currentHP) < hp) {
                    a.currentHP = 0;
                } else {
                    a.currentHP = psx.conv(i16, @as(c_int, a.currentHP) -% hp);
                }
                c.func_80093B68(@as(c_int, a.unk957), 0, hp | @as(c_int, @bitCast(@as(c_uint, 0x8000_0000))), 0);
            } else if ((a.statuses & 0x20000) != 0) {
                // regen: gain HP and heal every limb that is hurt
                a.statusTimers[5] = v.g.vs_battle_statusParams17TickInterval().*;
                const hp: c_int = @divTrunc(@as(c_int, a.maxHP), 20) -% 2 +% c.vs_main_getRandSmoothed(5);
                a.currentHP = psx.conv(i16, @as(c_int, a.currentHP) +% hp);
                if (a.maxHP < a.currentHP) a.currentHP = a.maxHP;

                for (0..6) |i| {
                    if (a.limbs[i].maxHp != 0) {
                        var var_v1: c_int = hp;
                        if (hp < 0) var_v1 = hp +% 7;
                        a.limbs[i].hp = psx.conv(i16, @as(c_int, a.limbs[i].hp) +% (var_v1 >> 3));
                        if (a.limbs[i].maxHp < a.limbs[i].hp) a.limbs[i].hp = a.limbs[i].maxHp;
                    }
                }

                c.func_80093B68(@as(c_int, a.unk957), 0, hp, 0);
            }
        }
    }

    if ((a.statuses & 0x10) != 0) {
        // unk34/unk35 index limbs past the six entries in the original: no bounds check
        var found: c_int = 0;
        if (psx.getBits(a, "unk34") != 0) {
            found = @intFromBool(psx.at(&a.limbs, psx.getBits(a, "unk34")).hp >= 2);
        }
        if (psx.getBits(a, "unk35") != 0 and psx.at(&a.limbs, psx.getBits(a, "unk35")).hp >= 2) {
            found = 1;
        }
        if (found != 0) statuses |= 0x10;
    }

    if (a.unk957 == 0) {
        if ((a.statuses & 0xF) != 0) {
            for (0..4) |i| {
                const flag = v.g.D_800E8204()[i];
                if ((a.statuses & flag) != 0) {
                    var found: c_int = 0;
                    if (psx.getBits(a, "unk34") != 0) {
                        found = @intFromBool(a.limbs[i].hp >= 2);
                    }
                    if (found != 0) statuses |= flag;
                }
            }
        }
    }

    if (statuses != 0) {
        c.func_80093FEC(@as(c_int, a.unk957), 0, statuses, 1);
        c.vs_battle_removeActorStatuses(statuses, @ptrCast(a));
    }
}
