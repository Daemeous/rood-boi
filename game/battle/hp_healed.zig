//! HP healed: the amount a healing action restores (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _hpHealed (src/BATTLE/BATTLE.PRG/146C.c)
/// The HP that hit `nHit` of `action` restores from `source` to `target` (a stat calculator): the source's
/// intelligence with its weapon (if drawn) and accessory against the target's intelligence, armour and shield
/// (if drawn) by the hit's type and affinity, through func_8007FE5C, never below 0; with `withRandMod` a smoothed
/// spread of -5..5 and, on the first hit, the target's affinity reaction (func_80080C9C).
pub fn hpHealed(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, nHit: c_int, withRandMod: c_int) callconv(.c) c_short {
    const act = action.?;
    const tgt = target.?;
    const actors = v.g.vs_battle_actors();
    // The C's sp18 is an uninitialised local; only weaponAffinity is read before it is set (below).
    var sp18 = std.mem.zeroes(v._hitIntermediate);

    // the actor indices are not bounded by the C
    const sourceActor = psx.get(psx.at(actors, source.?.unk0.targetActor).*.?.stats).?;
    const targetActor = psx.get(psx.at(actors, tgt.unk0.targetActor).*.?.stats).?;
    const limb: c_int = tgt.unk0.targetLimb;
    const limbp = psx.at(&targetActor.limbs, limb);
    const hit = psx.at(&act.hitParams, nHit);

    sp18.sourceClass = psx.conv(u8, psx.getBits(sourceActor, "enemyClass"));
    sp18.@"type" = psx.conv(u8, psx.getBits(hit, "type"));
    sp18.sourceAffinity = psx.conv(u8, @as(c_int, psx.getBits(hit, "affinity")) -% 1);
    sp18.value = psx.conv(i16, sourceActor.intelligence);
    // affinity 0 makes sourceAffinity 0xFF: every affinity table below is then read past its end, as in the C
    const affinity = sp18.sourceAffinity;

    if (psx.at(actors, sourceActor.unk957).*.?.weaponDrawn & 1 != 0) {
        sp18.value = psx.conv(i16, @as(c_int, sp18.value) +% sourceActor.weapon.currentInt);
        if (affinity != 0) {
            sp18.weaponAffinity = psx.at(&sourceActor.weapon.classAffinityCurrent.affinity[0], affinity).*;
        }
        // Original bug: with sourceAffinity 0 (the hit's affinity 1) weaponAffinity is read uninitialised (the
        // object keeps a caller's register): 0 here (PORTING.md §11.4).
        sp18.weaponDamage = sp18.weaponAffinity;
        if (sourceActor.weapon.maxDp != 0) {
            sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponAffinity) -% @divTrunc((@as(c_int, sourceActor.weapon.maxDp) -% sourceActor.weapon.currentDp) *% 100, @as(c_int, sourceActor.weapon.maxDp)));
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
    sp18.weaponDamage = psx.conv(i16, @as(c_int, sp18.weaponDamage) +%
        (@as(c_int, psx.at(&sourceActor.accessory.types, sp18.@"type").*) +% psx.at(&sourceActor.accessory.affinities, affinity).*));
    sp18.weaponDamage = psx.conv(i16, @divTrunc(@as(c_int, sp18.weaponDamage), 4) +% 100);
    sp18.mediatingStat = psx.conv(i16, targetActor.intelligence);
    sp18.armorStat = limbp.armor.currentInt;
    sp18.shieldStat = 0;
    sp18.armorStatFactor = 100;
    sp18.shieldStatFactor = 100;
    sp18.statFactor = 100;

    if (sp18.@"type" != 0) {
        sp18.statFactor = psx.conv(i16, @as(c_int, psx.at(&limbp.types, sp18.@"type").*) +% 100);
        sp18.armorStatFactor = psx.conv(i16, @as(c_int, psx.at(&limbp.armor.types, sp18.@"type").*) +% 100);
    }
    if (affinity != 0) {
        sp18.statFactor = psx.conv(i16, @as(c_int, sp18.statFactor) +% psx.at(&limbp.affinities, affinity).*);
        sp18.armorStatFactor = psx.conv(i16, @as(c_int, sp18.armorStatFactor) +% psx.at(&limbp.armor.classAffinityCurrent.affinity[0], affinity).*);
    }
    if (limbp.armor.maxDp != 0) {
        sp18.armorStatFactor = psx.conv(i16, @as(c_int, sp18.armorStatFactor) -% @divTrunc(@divTrunc((@as(c_int, limbp.armor.maxDp) -% limbp.armor.currentDp) *% 100, @as(c_int, limbp.armor.maxDp)), 4));
    }

    if (psx.at(actors, targetActor.unk957).*.?.weaponDrawn & 1 != 0) {
        sp18.shieldStat = targetActor.shield.currentInt;
        if (sp18.@"type" != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% psx.at(&targetActor.shield.types, sp18.@"type").*);
        }
        if (affinity != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% psx.at(&targetActor.shield.classAffinityCurrent.affinity[0], affinity).*);
        }
        if (targetActor.shield.maxDp != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) -% @divTrunc(@divTrunc((@as(c_int, targetActor.shield.maxDp) -% targetActor.shield.currentDp) *% 100, @as(c_int, targetActor.shield.maxDp)), 4));
        }
        if (targetActor.shield.maxPp != 0) {
            sp18.shieldStatFactor = psx.conv(i16, @as(c_int, sp18.shieldStatFactor) +% @divTrunc(@divTrunc(@as(c_int, targetActor.shield.currentPp) *% 100, @as(c_int, targetActor.shield.maxPp)), 4));
        }
    }

    sp18.mediatingStat = psx.conv(i16, @as(c_int, sp18.mediatingStat) +% targetActor.accessory.currentInt);
    sp18.statFactor = psx.conv(i16, @as(c_int, sp18.statFactor) +%
        (@as(c_int, psx.at(&targetActor.accessory.types, sp18.@"type").*) +% psx.at(&targetActor.accessory.affinities, affinity).*));
    var attack = @divTrunc(@as(c_int, sp18.value) *% sp18.weaponDamage, 100);
    attack = @divTrunc(attack *% (@as(c_int, psx.getBits(hit, "statFactor")) *% 10 +% 100), 100);
    var defense = (@as(c_int, sp18.mediatingStat) -% 20) *% sp18.statFactor;
    defense +%= @as(c_int, sp18.shieldStat) *% sp18.shieldStatFactor;
    defense +%= @as(c_int, sp18.armorStat) *% sp18.armorStatFactor;
    defense = @divTrunc(defense, @divTrunc(@as(c_int, targetActor.risk), 2) +% 100);
    // the short parameter truncates attack - defense
    var value: c_short = c.func_8007FE5C(@ptrCast(act), psx.conv(c_short, attack -% defense), @ptrCast(sourceActor), @ptrCast(targetActor));
    if (value < 0) {
        value = 0;
    }
    const spread: c_int = if (withRandMod != 0) c.vs_main_getRandSmoothed(11) -% 5 else 0;
    value = psx.conv(c_short, @as(c_int, value) +% spread);
    if (value < 0) {
        value = 0;
    }
    if (withRandMod != 0 and nHit == 0) {
        if (affinity != 0) {
            if (tgt.unk40 == 0) {
                c.func_80080C9C(@ptrCast(act), @ptrCast(targetActor), 0, @as(c_int, affinity) +% 1, limb);
            }
        }
    }
    return value;
}
