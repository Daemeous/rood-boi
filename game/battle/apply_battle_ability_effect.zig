//! Applies a hit entity's ability effects to its target actor (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _applyBattleAbilityEffect (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Applies a hit entity's HP, MP, stat, equipment and status effects to its target actor.
pub fn applyBattleAbilityEffect(arg0: ?*v._hitEntity_t) callconv(.c) void {
    c.func_800971D4();

    const hit = arg0.?;
    const targetActorId: c_int = @as(c_int, hit.target.targetActor);
    const targetLimb: c_int = @as(c_int, hit.target.targetLimb);
    const actor = psx.get(psx.at(v.g.vs_battle_actors(), targetActorId).*.?.stats);

    if (hit.target.unk3 == 4) {
        c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, 0, 2);
    }

    switch (psx.getBits(hit, "hpEffect")) {
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @as(c_int, hit.target.hp), 0);
            const a = actor.?;
            a.currentHP +%= hit.target.hp;
            if (a.maxHP < a.currentHP) a.currentHP = a.maxHP;

            var i: usize = 0;
            while (i < 6) : (i += 1) {
                a.limbs[i].hp +%= hit.limbs[i].effects.value0;
                if (a.limbs[i].maxHp < a.limbs[i].hp) a.limbs[i].hp = a.limbs[i].maxHp;
            }
        },
        3, 1 => {
            // hp | 0x80000000: unsigned in C, passed as an int.
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @bitCast(psx.conv(c_uint, @as(c_int, hit.target.hp)) | 0x8000_0000), 0);
            const a = actor.?;
            if (a.currentHP > hit.target.hp) {
                a.currentHP -%= hit.target.hp;
            } else {
                a.currentHP = 0;
            }

            var i: usize = 0;
            while (i < 6) : (i += 1) {
                if (a.limbs[i].hp > hit.limbs[i].effects.value0) {
                    a.limbs[i].hp -%= hit.limbs[i].effects.value0;
                } else {
                    a.limbs[i].hp = 0;
                }
                if (@as(c_int, a.limbs[i].armor.currentDp) > @as(c_int, hit.limbs[i].effects.value1)) {
                    a.limbs[i].armor.currentDp = psx.conv(u16, @as(c_int, a.limbs[i].armor.currentDp) -% @as(c_int, hit.limbs[i].effects.value1));
                } else {
                    a.limbs[i].armor.currentDp = 0;
                }
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "mpEffect")) {
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @as(c_int, hit.target.mp), 1);
            const a = actor.?;
            a.currentMP +%= hit.target.mp;
            if (a.maxMP < a.currentMP) a.currentMP = a.maxMP;
        },
        1 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @bitCast(psx.conv(c_uint, @as(c_int, hit.target.mp)) | 0x8000_0000), 1);
            const a = actor.?;
            if (a.currentMP > hit.target.mp) {
                a.currentMP -%= hit.target.mp;
            } else {
                a.currentMP = 0;
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "riskEffect")) {
        1 => {},
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @bitCast(psx.conv(c_uint, @as(c_int, hit.risk)) | 0x8000_0000), 8);
            const a = actor.?;
            if (@as(c_int, a.risk) > @as(c_int, hit.risk)) {
                a.risk = psx.conv(u16, @as(c_int, a.risk) -% @as(c_int, hit.risk));
            } else {
                a.risk = 0;
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "strengthEffect")) {
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @as(c_int, hit.strength), 5);
            const a = actor.?;
            a.totalStrength = psx.conv(u16, @as(c_int, a.totalStrength) +% @as(c_int, hit.strength));
            if (a.totalStrength > 999) a.totalStrength = 999;
            a.strength = psx.conv(u16, @as(c_int, a.strength) +% @as(c_int, hit.strength));
            if (a.strength > 999) a.strength = 999;
        },
        1 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @bitCast(psx.conv(c_uint, @as(c_int, hit.strength)) | 0x8000_0000), 5);
            const a = actor.?;
            if (@as(c_int, a.totalStrength) > @as(c_int, hit.strength)) {
                a.totalStrength = psx.conv(u16, @as(c_int, a.totalStrength) -% @as(c_int, hit.strength));
            } else {
                a.totalStrength = 0;
            }
            if (@as(c_int, a.strength) > @as(c_int, hit.strength)) {
                a.strength = psx.conv(u16, @as(c_int, a.strength) -% @as(c_int, hit.strength));
            } else {
                a.strength = 0;
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "intelligenceEffect")) {
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @as(c_int, hit.intelligence), 6);
            const a = actor.?;
            a.totalIntelligence = psx.conv(u16, @as(c_int, a.totalIntelligence) +% @as(c_int, hit.intelligence));
            if (a.totalIntelligence > 999) a.totalIntelligence = 999;
            a.intelligence = psx.conv(u16, @as(c_int, a.intelligence) +% @as(c_int, hit.intelligence));
            if (a.intelligence > 999) a.intelligence = 999;
        },
        1 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @bitCast(psx.conv(c_uint, @as(c_int, hit.intelligence)) | 0x8000_0000), 6);
            const a = actor.?;
            if (@as(c_int, a.totalIntelligence) > @as(c_int, hit.intelligence)) {
                a.totalIntelligence = psx.conv(u16, @as(c_int, a.totalIntelligence) -% @as(c_int, hit.intelligence));
            } else {
                a.totalIntelligence = 0;
            }
            if (@as(c_int, a.intelligence) > @as(c_int, hit.intelligence)) {
                a.intelligence = psx.conv(u16, @as(c_int, a.intelligence) -% @as(c_int, hit.intelligence));
            } else {
                a.intelligence = 0;
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "agilityEffect")) {
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @as(c_int, hit.agility), 7);
            const a = actor.?;
            a.totalAgility = psx.conv(u16, @as(c_int, a.totalAgility) +% @as(c_int, hit.agility));
            if (a.totalAgility > 999) a.totalAgility = 999;
            a.agility = psx.conv(u16, @as(c_int, a.agility) +% @as(c_int, hit.agility));
            if (a.agility > 999) a.agility = 999;
        },
        1 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @bitCast(psx.conv(c_uint, @as(c_int, hit.agility)) | 0x8000_0000), 7);
            const a = actor.?;
            if (@as(c_int, a.totalAgility) > @as(c_int, hit.agility)) {
                a.totalAgility = psx.conv(u16, @as(c_int, a.totalAgility) -% @as(c_int, hit.agility));
            } else {
                a.totalAgility = 0;
            }
            if (@as(c_int, a.agility) > @as(c_int, hit.agility)) {
                a.agility = psx.conv(u16, @as(c_int, a.agility) -% @as(c_int, hit.agility));
            } else {
                a.agility = 0;
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "maxHpEffect")) {
        1 => {},
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @as(c_int, hit.maxHp), 3);
            const a = actor.?;
            a.maxHP +%= hit.maxHp;
            if (a.maxHP > 999) a.maxHP = 999;
        },
        else => {},
    }

    switch (psx.getBits(hit, "maxMpEffect")) {
        1 => {},
        2 => {
            c.vs_battle_queueActorStatChangeNumber(targetActorId, targetLimb, @as(c_int, hit.maxMp), 4);
            const a = actor.?;
            a.maxMP +%= hit.maxMp;
            if (a.maxMP > 999) a.maxMP = 999;
        },
        else => {},
    }

    switch (psx.getBits(hit, "weaponDpEffect")) {
        1 => {},
        2 => {
            const a = actor.?;
            a.weapon.currentDp = psx.conv(u16, @as(c_int, a.weapon.currentDp) +% @as(c_int, hit.weaponDp));
            if (a.weapon.maxDp < a.weapon.currentDp) a.weapon.currentDp = a.weapon.maxDp;
        },
        else => {},
    }

    switch (psx.getBits(hit, "weaponPpEffect")) {
        2 => {
            const a = actor.?;
            a.weapon.currentPp = psx.conv(u16, @as(c_int, a.weapon.currentPp) +% @as(c_int, hit.weaponPp));
            if (a.weapon.maxPp < a.weapon.currentPp) a.weapon.currentPp = a.weapon.maxPp;
        },
        1 => {
            const a = actor.?;
            if (@as(c_int, a.weapon.currentPp) > @as(c_int, hit.weaponPp)) {
                a.weapon.currentPp = psx.conv(u16, @as(c_int, a.weapon.currentPp) -% @as(c_int, hit.weaponPp));
            } else {
                a.weapon.currentPp = 0;
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "armorDpEffect")) {
        1 => {
            const a = actor.?;
            if (@as(c_int, a.shield.currentDp) > @as(c_int, hit.armorDp)) {
                a.shield.currentDp = psx.conv(u16, @as(c_int, a.shield.currentDp) -% @as(c_int, hit.armorDp));
            } else {
                a.shield.currentDp = 0;
            }

            // targetLimb is a signed index: psx.at, as the C indexes without a check.
            const limb = psx.at(&a.limbs, hit.target.targetLimb);
            if (@as(c_int, limb.armor.currentDp) > @as(c_int, hit.armorDp)) {
                limb.armor.currentDp = psx.conv(u16, @as(c_int, limb.armor.currentDp) -% @as(c_int, hit.armorDp));
            } else {
                limb.armor.currentDp = 0;
            }
        },
        else => {},
    }

    switch (psx.getBits(hit, "shieldPpEffect")) {
        1 => {
            const a = actor.?;
            if (@as(c_int, a.shield.currentPp) > @as(c_int, hit.shieldPp)) {
                a.shield.currentPp = psx.conv(u16, @as(c_int, a.shield.currentPp) -% @as(c_int, hit.shieldPp));
            } else {
                a.shield.currentPp = 0;
            }
        },
        else => {},
    }

    var flag: c_int = 1;

    if (hit.inflictStatuses != 0) {
        var i: usize = 0;
        while (i < 32) : (i += 1) {
            const status: c_int = hit.inflictStatuses & flag;

            if (status != 0 and (actor.?.statuses & flag) != 0) {
                if (targetActorId != 0) {
                    if (actor.?.currentHP != 0) {
                        c.func_80093FEC(targetActorId, 0, status, 1);
                    }
                } else {
                    c.func_80093FEC(0, 0, status, 1);
                }

                c.vs_battle_removeActorStatuses(hit.inflictStatuses & flag, @ptrCast(actor));
            }

            flag <<= 1;
        }
    }

    flag = 1;

    if (hit.removeStatuses != 0) {
        var i: usize = 0;
        while (i < 32) : (i += 1) {
            const status: c_int = hit.removeStatuses & flag;

            if (status != 0 and (actor.?.statuses & flag) == 0) {
                if (targetActorId != 0) {
                    if (actor.?.currentHP != 0) {
                        c.func_80093FEC(targetActorId, 0, status, 0);
                    }
                } else {
                    c.func_80093FEC(0, 0, status, 0);
                }

                c.vs_battle_inflictActorStatuses(hit.removeStatuses & flag, @ptrCast(actor));
            }

            flag <<= 1;
        }
    }
}
