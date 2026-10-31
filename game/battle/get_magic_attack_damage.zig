//! Magic attack damage: the damage a magic action deals from one battle entity to another
//! (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _getMagicAttackDamage (src/BATTLE/BATTLE.PRG/146C.c)
/// Damage of magic action `action` at hit `nHit` from `source` to `target`, with the random spread when `addRandMod` is set.
pub fn getMagicAttackDamage(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, nHit: c_int, addRandMod: c_int) callconv(.c) c_short {
    const act = action.?;
    const src = source.?;
    const tgt = target.?;
    const actors = v.g.vs_battle_actors();

    var sp18: v._hitIntermediate = std.mem.zeroes(v._hitIntermediate);
    var value: c_short = undefined;
    var affinity: c_int = 0;
    var maxAffinity: i16 = 0;
    var i: u8 = 0;
    var attack: c_int = undefined;
    var defense: c_int = undefined;

    const sourceActor = psx.get(actors[src.unk0.targetActor].?.stats).?;
    const targetActor = psx.get(actors[tgt.unk0.targetActor].?.stats).?;
    const limb: c_int = tgt.unk0.targetLimb;
    const limbp = psx.at(&targetActor.limbs, limb);
    const hit = psx.at(&act.hitParams, nHit);

    sp18.sourceClass = psx.conv(u8, psx.getBits(sourceActor, "enemyClass"));
    sp18.@"type" = psx.conv(u8, psx.getBits(hit, "type"));

    if (psx.getBits(hit, "affinity") == 0) {
        if ((actors[sourceActor.unk957].?.weaponDrawn & 1) == 0 or sourceActor.weapon.blade.id == 0) {
            affinity = 0;
        } else {
            affinity = 0;
            maxAffinity = 0;
            i = 0;
            while (i < 7) : (i +%= 1) {
                if (maxAffinity < sourceActor.weapon.classAffinityCurrent.affinity[0][i]) {
                    maxAffinity = sourceActor.weapon.classAffinityCurrent.affinity[0][i];
                    affinity = @as(c_int, i);
                }
            }
        }

        sp18.sourceAffinity = psx.conv(u8, affinity);

        affinity = 0;
        maxAffinity = 0;
        i = 0;
        while (i < 7) : (i +%= 1) {
            sp18.tempTargetAffinity = psx.conv(i16, @as(c_int, limbp.affinities[i]) +% limbp.armor.classAffinityCurrent.affinity[0][i]);
            if ((actors[targetActor.unk957].?.weaponDrawn & 1) != 0) {
                sp18.tempTargetAffinity = psx.conv(i16, @as(c_int, sp18.tempTargetAffinity) +% targetActor.shield.classAffinityCurrent.affinity[0][i]);
            }
            if (maxAffinity < sp18.tempTargetAffinity) {
                maxAffinity = sp18.tempTargetAffinity;
                affinity = @as(c_int, i);
            }
        }

        sp18.targetAffinity = psx.conv(u8, affinity);
    } else {
        sp18.sourceAffinity = psx.conv(u8, @as(c_int, psx.getBits(hit, "affinity")) -% 1);
        sp18.targetAffinity = psx.conv(u8, @as(c_int, psx.getBits(hit, "affinity")) -% 1);
    }

    sp18.value = psx.conv(i16, sourceActor.intelligence);

    if ((actors[sourceActor.unk957].?.weaponDrawn & 1) != 0) {
        sp18.value = psx.conv(i16, @as(c_int, sp18.value) +% sourceActor.weapon.currentInt);
        const wa = sourceActor.weapon.classAffinityCurrent.affinity[0][sp18.sourceAffinity];
        sp18.weaponAffinity = wa;
        sp18.weaponDamage = wa;

        if (sourceActor.weapon.maxDp != 0) {
            sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponDamage) -% @divTrunc((@as(c_int, sourceActor.weapon.maxDp) -% sourceActor.weapon.currentDp) *% 100, @as(c_int, sourceActor.weapon.maxDp)));
        }
        if (sourceActor.weapon.maxPp != 0) {
            sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponDamage) +% @divTrunc(@as(c_int, sourceActor.weapon.currentPp) *% 100, @as(c_int, sourceActor.weapon.maxPp)));
        }
    } else {
        sp18.weaponClass = 0;
        sp18.weaponAffinity = 0;
        sp18.typeValue = 0;
        sp18.weaponDamage = 0;
    }

    sp18.value = psx.conv(i16, @as(c_int, sp18.value) +% sourceActor.accessory.currentInt);
    sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponDamage) +% (@as(c_int, sourceActor.accessory.types[sp18.@"type"]) +% sourceActor.accessory.affinities[sp18.sourceAffinity]));
    sp18.weaponDamage = psx.conv(i16, @divTrunc(@as(c_int, sp18.weaponDamage), 4) +% 100);
    sp18.mediatingStat = psx.conv(i16, targetActor.intelligence);
    sp18.armorStat = limbp.armor.currentInt;
    sp18.shieldStat = 0;
    sp18.armorStatFactor = 100;
    sp18.shieldStatFactor = 100;
    sp18.statFactor = 100;

    if (sp18.@"type" != 0) {
        sp18.statFactor = psx.conv(i16, @as(c_int, limbp.types[sp18.@"type"]) +% 100);
        sp18.armorStatFactor = psx.conv(i16, @as(c_int, limbp.armor.types[sp18.@"type"]) +% 100);
    }

    sp18.statFactor = psx.conv(i16, @as(c_int, sp18.statFactor) +% limbp.affinities[sp18.sourceAffinity]);
    sp18.armorStatFactor = psx.conv(i16, @as(c_int, sp18.armorStatFactor) +% limbp.armor.classAffinityCurrent.affinity[0][sp18.sourceAffinity]);

    if (limbp.armor.maxDp != 0) {
        sp18.armorStatFactor = psx.conv(i16, @as(c_int, sp18.armorStatFactor) -% @divTrunc(@divTrunc((@as(c_int, limbp.armor.maxDp) -% limbp.armor.currentDp) *% 100, @as(c_int, limbp.armor.maxDp)), 4));
    }

    if ((actors[targetActor.unk957].?.weaponDrawn & 1) != 0) {
        sp18.shieldStat = targetActor.shield.currentInt;

        if (sp18.@"type" != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% targetActor.shield.types[sp18.@"type"]);
        }

        sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% targetActor.shield.classAffinityCurrent.affinity[0][sp18.sourceAffinity]);

        if (targetActor.shield.maxDp != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) -% @divTrunc(@divTrunc((@as(c_int, targetActor.shield.maxDp) -% targetActor.shield.currentDp) *% 100, @as(c_int, targetActor.shield.maxDp)), 4));
        }

        if (targetActor.shield.maxPp != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% @divTrunc(@divTrunc(@as(c_int, targetActor.shield.currentPp) *% 100, @as(c_int, targetActor.shield.maxPp)), 4));
        }
    }

    sp18.mediatingStat = psx.conv(i16, @as(c_int, sp18.mediatingStat) +% targetActor.accessory.currentInt);
    sp18.statFactor = psx.conv(i16, @as(c_int, sp18.statFactor) +% (@as(c_int, targetActor.accessory.types[sp18.@"type"]) +% targetActor.accessory.affinities[sp18.sourceAffinity]));

    attack = @divTrunc(@as(c_int, sp18.value) *% @as(c_int, sp18.weaponDamage), 100);
    attack = @divTrunc(attack *% (@as(c_int, psx.getBits(hit, "statFactor")) *% 10 +% 100), 100);
    defense = (@as(c_int, sp18.mediatingStat) -% 20) *% @as(c_int, sp18.statFactor);
    defense +%= @as(c_int, sp18.shieldStat) *% @as(c_int, sp18.shieldStatFactor);
    defense +%= @as(c_int, sp18.armorStat) *% @as(c_int, sp18.armorStatFactor);
    defense = @divTrunc(defense, @as(c_int, targetActor.risk >> 1) +% 100);

    // The C passes `attack - defense` to a `short` parameter: the conversion truncates to 16 bits.
    value = c.func_8007FE5C(@ptrCast(act), psx.conv(c_short, attack -% defense), @ptrCast(sourceActor), @ptrCast(targetActor));

    if (value < 0) {
        value = 0;
    }

    if (addRandMod != 0) {
        value = psx.conv(c_short, @as(c_int, value) +% (1 +% c.vs_main_getRand(5)));
        if (nHit == 0) {
            if (sp18.sourceAffinity != 0) {
                if (tgt.unk40 == 0) {
                    _ = c.func_80080C9C(@ptrCast(act), @ptrCast(targetActor), 0, @as(c_int, sp18.sourceAffinity) +% 1, limb);
                }
            }
        }
    }

    return value;
}
