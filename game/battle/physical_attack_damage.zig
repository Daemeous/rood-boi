//! A physical attack's damage, with and without the attacker's status modifiers (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

fn stats(entity: *v._hitEntity_t) *v.vs_battle_actor2 {
    return psx.get(v.g.vs_battle_actors()[entity.unk0.targetActor].?.stats).?;
}

fn drawn(actor: *v.vs_battle_actor2) bool {
    return v.g.vs_battle_actors()[actor.unk957].?.weaponDrawn & 1 != 0;
}

/// The C's `x * 100 / max` wear in percent (u16 operands).
fn wear(lost: c_int, max: u16) c_int {
    return @divTrunc(lost *% 100, @as(c_int, max));
}

/// orig: _getPhysicalAttackDamage (src/BATTLE/BATTLE.PRG/146C.c)
/// The damage of hit `nHit`: the attacker's strength (plus its drawn weapon's, whose damage counts its type, class
/// and affinity values, DP wear and PP charge) and accessory against the target limb's strength, armor and shield
/// (each by type, class, affinity and wear), over risk / 2 + 100, through func_8007FE5C; with the variance 1..5;
/// halved by the attacker's status 2 when asked; doubled on a risk critical (D_800F1A0C) for the basic attacks
/// (ids 0..5), and the first hit on an actor trains both sides' affinities (func_80080F78, func_80080C9C). Static
/// in the C: ported with its callers (PORTING.md §13).
fn physicalAttackDamage(action: *v.vs_action_t, source: *v._hitEntity_t, target: *v._hitEntity_t, nHit: c_int, withRandMod: c_int, applyStatuses: c_int) i16 {
    // sp18 in the C: every field it reads is set first.
    var sp18: v._hitIntermediate = undefined;
    const attacker = stats(source);
    const defender = stats(target);
    const limb_index: c_int = target.unk0.targetLimb;
    // hitParams has 2 entries, limbs 6, the type and affinity tables 4 and 8: the C bounds none of the indices.
    const params = psx.at(&action.hitParams, nHit);
    const limb = psx.at(&defender.limbs, limb_index);
    sp18.sourceClass = psx.getBits(attacker, "enemyClass");
    sp18.targetClass = psx.getBits(defender, "enemyClass");
    if (psx.getBits(params, "type") == 0) {
        sp18.@"type" = attacker.weapon.damageType;
        if (sp18.@"type" == 0) {
            sp18.@"type" = 1;
        }
    } else {
        sp18.@"type" = psx.getBits(params, "type");
    }
    var affinity: c_int = undefined;
    var maxAffinity: i16 = undefined;
    if (psx.getBits(params, "affinity") == 0) {
        if (!drawn(attacker) or attacker.weapon.blade.id == 0) {
            affinity = 0;
        } else {
            affinity = 0;
            maxAffinity = 0;
            var i: c_int = 0;
            while (i < 7) : (i += 1) { // i < 7
                if (maxAffinity < attacker.weapon.classAffinityCurrent.affinity[0][@intCast(i)]) {
                    maxAffinity = attacker.weapon.classAffinityCurrent.affinity[0][@intCast(i)];
                    affinity = i;
                }
            }
        }
        sp18.sourceAffinity = psx.conv(u8, affinity);
    } else {
        sp18.sourceAffinity = psx.conv(u8, @as(c_int, psx.getBits(params, "affinity")) - 1);
    }
    affinity = 0;
    maxAffinity = 0;
    var i: c_int = 0;
    while (i < 7) : (i += 1) { // i < 7
        const k: usize = @intCast(i);
        sp18.tempTargetAffinity = psx.conv(i16, @as(c_int, limb.affinities[k]) +% limb.armor.classAffinityCurrent.affinity[0][k]);
        if (drawn(defender)) {
            sp18.tempTargetAffinity = psx.conv(i16, @as(c_int, sp18.tempTargetAffinity) +% defender.shield.classAffinityCurrent.affinity[0][k]);
        }
        if (maxAffinity < sp18.tempTargetAffinity) {
            maxAffinity = sp18.tempTargetAffinity;
            affinity = i;
        }
    }
    sp18.targetAffinity = psx.conv(u8, affinity);
    sp18.value = psx.conv(i16, attacker.strength);
    if (drawn(attacker)) {
        sp18.value = psx.conv(i16, @as(c_int, sp18.value) +% attacker.weapon.currentStr);
        sp18.typeValue = attacker.weapon.damageTypeValue;
        sp18.weaponAffinity = psx.at(&attacker.weapon.classAffinityCurrent.affinity[0], sp18.sourceAffinity).*;
        sp18.weaponClass = attacker.weapon.classAffinityCurrent.class[0][psx.getBits(defender, "enemyClass")];
        sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponClass) +% (@as(c_int, sp18.typeValue) +% sp18.weaponAffinity));
        if (attacker.weapon.maxDp != 0) {
            sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponDamage) -% wear(@as(c_int, attacker.weapon.maxDp) - attacker.weapon.currentDp, attacker.weapon.maxDp));
        }
        if (attacker.weapon.maxPp != 0) {
            const charge: c_int = psx.at(psx.globalMany(u16, "D_800E82F4"), attacker.weapon.blade.category).*;
            sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponDamage) +% @divTrunc(@as(c_int, attacker.weapon.currentPp) *% charge, @as(c_int, attacker.weapon.maxPp)));
        }
    } else {
        sp18.weaponClass = 0;
        sp18.weaponAffinity = 0;
        sp18.typeValue = 0;
        sp18.weaponDamage = 0;
    }
    sp18.value = psx.conv(i16, @as(c_int, sp18.value) +% attacker.accessory.currentStr);
    sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponDamage) +% (@as(c_int, psx.at(&attacker.accessory.types, sp18.@"type").*) +%
        psx.at(&attacker.accessory.classes, sp18.targetClass).* +% psx.at(&attacker.accessory.affinities, sp18.sourceAffinity).*));
    sp18.weaponDamage = psx.conv(i16, @divTrunc(@as(c_int, sp18.weaponDamage), 4) + 100);
    sp18.mediatingStat = psx.conv(i16, defender.strength);
    sp18.armorStat = limb.armor.currentStr;
    sp18.shieldStat = 0;
    sp18.armorStatFactor = 100;
    sp18.shieldStatFactor = 100;
    sp18.statFactor = 100;
    sp18.statFactor = psx.conv(i16, @as(c_int, psx.at(&limb.types, sp18.@"type").*) +% psx.at(&limb.affinities, sp18.sourceAffinity).* +% 100);
    sp18.armorStatFactor = psx.conv(i16, @as(c_int, psx.at(&limb.armor.classAffinityCurrent.class[0], sp18.sourceClass).*) +%
        (@as(c_int, psx.at(&limb.armor.types, sp18.@"type").*) +% psx.at(&limb.armor.classAffinityCurrent.affinity[0], sp18.sourceAffinity).*) +% 100);
    if (limb.armor.maxDp != 0) {
        sp18.armorStatFactor = psx.conv(i16, @as(c_int, sp18.armorStatFactor) -% @divTrunc(wear(@as(c_int, limb.armor.maxDp) - limb.armor.currentDp, limb.armor.maxDp), 4));
    }
    if (drawn(defender)) {
        sp18.shieldStat = psx.conv(i16, @as(c_int, sp18.shieldStat) +% defender.shield.currentStr);
        sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% ((@as(c_int, psx.at(&defender.shield.types, sp18.@"type").*) +%
            psx.at(&defender.shield.classAffinityCurrent.affinity[0], sp18.sourceAffinity).*) +%
            psx.at(&defender.shield.classAffinityCurrent.class[0], sp18.sourceClass).*));
        if (defender.shield.maxDp != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) -% @divTrunc(wear(@as(c_int, defender.shield.maxDp) - defender.shield.currentDp, defender.shield.maxDp), 4));
        }
        if (defender.shield.maxPp != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% @divTrunc(wear(defender.shield.currentPp, defender.shield.maxPp), 4));
        }
    }
    sp18.mediatingStat = psx.conv(i16, @as(c_int, sp18.mediatingStat) +% defender.accessory.currentStr);
    sp18.statFactor = psx.conv(i16, @as(c_int, sp18.statFactor) +% ((@as(c_int, psx.at(&defender.accessory.types, sp18.@"type").*) +%
        psx.at(&defender.accessory.classes, sp18.sourceClass).*) +% psx.at(&defender.accessory.affinities, sp18.sourceAffinity).*));
    var attack = @divTrunc(@as(c_int, sp18.value) *% sp18.weaponDamage, 100);
    attack = @divTrunc(attack *% (@as(c_int, psx.getBits(params, "statFactor")) * 10 + 100), 100);
    var defense = (@as(c_int, sp18.mediatingStat) -% 20) *% sp18.statFactor;
    defense +%= @as(c_int, sp18.shieldStat) *% sp18.shieldStatFactor;
    defense +%= @as(c_int, sp18.armorStat) *% sp18.armorStatFactor;
    defense = @divTrunc(defense, @divTrunc(@as(c_int, defender.risk), 2) + 100); // risk is a u16: no overflow
    var value = c.func_8007FE5C(@ptrCast(action), psx.conv(c_short, attack -% defense), @ptrCast(attacker), @ptrCast(defender));
    if (value < 0) {
        value = 0;
    }
    if (withRandMod != 0) {
        value = psx.conv(i16, @as(c_int, value) +% (1 +% c.vs_main_getRand(5)));
    }
    if (applyStatuses != 0 and (attacker.statuses & 2) != 0) {
        value = psx.conv(i16, @divTrunc(@as(c_int, value), 2));
    }
    if (withRandMod != 0) {
        if (action.id < 6 and c.vs_main_getRand(100) < @divTrunc(@as(c_int, attacker.risk), 10) + 10) {
            value = psx.conv(i16, (psx.conv(c_uint, @as(c_int, value) << 0x10)) >> 0xF);
            psx.global(c_int, "D_800F1A0C").* = 1;
        }
        if (nHit == 0) {
            if (target.unk40 == 0) {
                c.func_80080F78(@ptrCast(action), @ptrCast(attacker), @as(c_int, psx.getBits(defender, "enemyClass")) + 1, @as(c_int, sp18.targetAffinity) + 1);
                c.func_80080C9C(@ptrCast(action), @ptrCast(defender), @as(c_int, psx.getBits(attacker, "enemyClass")) + 1, @as(c_int, sp18.sourceAffinity) + 1, limb_index);
            }
        }
    }
    return value;
}

/// orig: _getPhysicalAttackDamageWithoutStatusModifiers (src/BATTLE/BATTLE.PRG/146C.c)
/// The physical damage of hit `nHit`, the attacker's statuses not applied.
pub fn getPhysicalAttackDamageWithoutStatusModifiers(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, nHit: c_int, withRandMod: c_int) callconv(.c) i16 {
    return physicalAttackDamage(action.?, source.?, target.?, nHit, withRandMod, 0);
}

/// orig: _getPhysicalAttackDamageWithStatusModifiers (src/BATTLE/BATTLE.PRG/146C.c)
/// The physical damage of hit `nHit`, halved by the attacker's status 2.
pub fn getPhysicalAttackDamageWithStatusModifiers(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, nHit: c_int, withRandMod: c_int) callconv(.c) i16 {
    return physicalAttackDamage(action.?, source.?, target.?, nHit, withRandMod, 1);
}
