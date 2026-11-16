//! Clears the statuses in a mask on a battle actor and undoes what they changed
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_removeActorStatuses (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the statuses in `mask` on an actor and restores the stats, limbs, weapon, shield and armor affinities they changed.
pub fn vs_battle_removeActorStatuses(mask: c_int, actor_ptr: ?*v.vs_battle_actor2) callconv(.c) void {
    const actor = actor_ptr.?;

    actor.statuses &= ~mask;

    if ((mask & 0x7E0) != 0) {
        if ((actor.statuses & 0x7E0) == 0) {
            actor.statusTimers[0] = 0;
        }
    }

    if ((mask & 0x810) != 0) {
        if ((actor.statuses & 0x810) == 0) {
            actor.statusTimers[1] = 0;
        }
    }

    if ((mask & 0x1FF80000) != 0) {
        if ((actor.statuses & 0x1FF80000) == 0) {
            actor.statusTimers[2] = 0;
        }
    }

    if ((mask & 0x2000) != 0) {
        if ((actor.statuses & 0x2000) == 0) {
            actor.statusTimers[3] = 0;
        }
    }

    if ((mask & 0x8000) != 0) {
        if ((actor.statuses & 0x8000) == 0) {
            actor.statusTimers[4] = 0;
        }
    }

    if ((mask & 0x24000) != 0) {
        if ((actor.statuses & 0x24000) == 0) {
            actor.statusTimers[5] = 0;
        }
    }

    if ((mask & 0x60) != 0) {
        actor.strength = actor.totalStrength;
    }

    if ((mask & 0x180) != 0) {
        actor.intelligence = actor.totalIntelligence;
    }

    if ((mask & 0x600) != 0) {
        actor.agility = actor.totalAgility;
    }

    if ((mask & 0x10000) != 0) {
        actor.strength = actor.totalStrength;
        actor.intelligence = actor.totalIntelligence;
        actor.agility = actor.totalAgility;
    }

    if ((mask & 0x8010) != 0) {
        if ((mask & 0x10) != 0) {
            // unk34 and unk35 index the six limbs unchecked, as the C does.
            if (psx.getBits(actor, "unk34") != 0) {
                const limb = psx.at(&actor.limbs, psx.getBits(actor, "unk34"));
                if (limb.maxHp != 0 and limb.hp < 2) {
                    limb.hp = 2;
                }
            }

            if (psx.getBits(actor, "unk35") != 0) {
                const limb = psx.at(&actor.limbs, psx.getBits(actor, "unk35"));
                if (limb.maxHp != 0 and limb.hp < 2) {
                    limb.hp = 2;
                }
            }
        }

        if ((actor.statuses & 0x10) == 0) {
            actor.unk31 = actor.unk2E;
            actor.unk33 = actor.unk2F;
        }

        c.vs_battle_aiSetActorVerticalMovementSteps(@as(c_int, actor.actorId), @as(c_int, actor.unk31), @as(c_int, actor.unk33));
    }

    if ((mask & 0x800) != 0 and (actor.statuses & 0x8010) == 0) {
        actor.unk31 = actor.unk2E;
        actor.unk33 = actor.unk2F;

        c.vs_battle_aiSetActorVerticalMovementSteps(@as(c_int, actor.actorId), @as(c_int, actor.unk31), @as(c_int, actor.unk33));
    }

    if ((mask & 0x180000) != 0) {
        if (actor.weapon.blade.id != 0) {
            actor.weapon.currentStr = psx.conv(i16, actor.weapon.baseStr);
            actor.weapon.currentInt = psx.conv(i16, actor.weapon.baseInt);
        }

        if (actor.shield.base.id != 0) {
            actor.shield.currentStr = psx.conv(i16, actor.shield.baseStr);
            actor.shield.currentInt = psx.conv(i16, actor.shield.baseInt);
        }

        for (0..6) |i| {
            const armor = &actor.limbs[i].armor;
            if (armor.armor.id != 0) {
                armor.currentStr = psx.conv(i16, armor.baseStr);
                armor.currentInt = psx.conv(i16, armor.baseInt);
            }
        }
    }

    const temp1: c_uint = psx.conv(c_uint, mask) & 0x01E00000;

    if (temp1 != 0) {
        var statIncrease: c_int = undefined;
        var statDecrease: c_int = undefined;
        switch (temp1) {
            0x200000 => {
                statIncrease = 1;
                statDecrease = 3;
            },
            0x400000 => {
                statIncrease = 2;
                statDecrease = 4;
            },
            0x800000 => {
                statIncrease = 3;
                statDecrease = 1;
            },
            0x01000000 => {
                statIncrease = 4;
                statDecrease = 2;
            },
            else => {
                statDecrease = 7;
                statIncrease = 7;
            },
        }

        if (statIncrease != 7) {
            // Both indices are 1..4 here: the switch set them together.
            const inc: usize = @intCast(statIncrease);
            const dec: usize = @intCast(statDecrease);

            if (actor.weapon.blade.id != 0) {
                if (actor.weapon.blade.affinities[inc] < 50 and c.vs_main_getRand(8) == 0) {
                    actor.weapon.blade.affinities[inc] +%= 1;

                    if (actor.actorId == 0) {
                        c.func_80096768(240, statIncrease +% 6, 1);
                    }
                }

                if (actor.weapon.blade.affinities[dec] > -100 and c.vs_main_getRand(16) == 0) {
                    actor.weapon.blade.affinities[dec] -%= 1;

                    if (actor.actorId == 0) {
                        c.func_80096768(240, statDecrease +% 6, (std.math.minInt(c_int) | 1));
                    }
                }
            }

            actor.weapon.unk168[inc] = 0;
            actor.weapon.unk168[dec] = 0;

            c._calculateWeaponClassAffinity(@ptrCast(actor));
        }
    }

    const temp2: c_uint = psx.conv(c_uint, mask) & 0x1E000000;

    if (temp2 != 0) {
        var statIncrease: c_int = undefined;
        var statDecrease: c_int = undefined;
        switch (temp2) {
            0x2000000 => {
                statIncrease = 1;
                statDecrease = 3;
            },
            0x4000000 => {
                statIncrease = 2;
                statDecrease = 4;
            },
            0x8000000 => {
                statIncrease = 3;
                statDecrease = 1;
            },
            0x10000000 => {
                statIncrease = 4;
                statDecrease = 2;
            },
            else => {
                statDecrease = 7;
                statIncrease = 7;
            },
        }

        if (statIncrease != 7) {
            // Both indices are 1..4 here: the switch set them together.
            const inc: usize = @intCast(statIncrease);
            const dec: usize = @intCast(statDecrease);

            if (actor.shield.base.id != 0) {
                if (actor.shield.base.affinities[inc] < 50 and c.vs_main_getRand(8) == 0) {
                    actor.shield.base.affinities[inc] +%= 1;

                    if (actor.actorId == 0) {
                        c.func_80096768(241, statIncrease +% 6, 1);
                    }
                }

                if (actor.shield.base.affinities[dec] > -100 and c.vs_main_getRand(16) == 0) {
                    actor.shield.base.affinities[dec] -%= 1;

                    if (actor.actorId == 0) {
                        c.func_80096768(241, statDecrease +% 6, (std.math.minInt(c_int) | 1));
                    }
                }
            }

            actor.shield.unk134[inc] = 0;
            actor.shield.unk134[dec] = 0;

            c._calculateShieldClassAffinity(@ptrCast(actor));

            for (0..6) |i| {
                const limb = &actor.limbs[i];
                if (limb.armor.armor.id != 0) {
                    if (limb.armor.armor.affinities[inc] < 50 and c.vs_main_getRand(8) == 0) {
                        limb.armor.armor.affinities[inc] +%= 1;

                        if (actor.actorId == 0) {
                            c.func_80096768(@as(c_int, limb.nameIndex), statIncrease +% 6, 1);
                        }
                    }

                    if (limb.armor.armor.affinities[dec] > -100 and c.vs_main_getRand(16) == 0) {
                        limb.armor.armor.affinities[dec] -%= 1;

                        if (actor.actorId == 0) {
                            c.func_80096768(@as(c_int, limb.nameIndex), statDecrease +% 6, (std.math.minInt(c_int) | 1));
                        }
                    }
                }

                limb.armor.unk88[inc] = 0;
                limb.armor.unk88[dec] = 0;

                c._calculateArmorClassAffinity(@ptrCast(actor), @as(c_int, @intCast(i)));
            }
        }

        c.func_800971D4();
    }
}
