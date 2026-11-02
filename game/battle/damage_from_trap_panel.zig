//! The HP a trap panel takes: a tenth of max HP times the stat factor, plus the room's trap level, against the target's defense (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _damageFromTrapPanel (src/BATTLE/BATTLE.PRG/146C.c)
/// A tenth of the target's max HP times the hit's stat factor, plus 10 per trap level (+1), less its defense
/// (strength, limb armor and, with the weapon drawn, shield, each by type, affinity and wear, over risk / 2 + 100);
/// with the variance -5..5; at least 0, below the target's HP.
pub fn damageFromTrapPanel(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, hit: ?*v._hitEntity_t, nHit: c_int, withRandMod: c_int) callconv(.c) i16 {
    // sp10 in the C: every field it reads is set first.
    var sp10: v._hitIntermediate = undefined;
    const actor = psx.get(v.g.vs_battle_actors()[hit.?.unk0.targetActor].?.stats).?;
    const limb: c_int = hit.?.unk0.targetLimb;
    // targetLimb, the hit's type and the source affinity (affinity - 1: 255 for 0) are not bounded by the C.
    const params = psx.at(&action.?.hitParams, nHit);
    sp10.value = psx.conv(i16, @divTrunc(@as(c_int, actor.maxHP), 10));
    sp10.value = psx.conv(i16, @as(c_int, sp10.value) *% psx.getBits(params, "statFactor"));
    sp10.value = psx.conv(i16, @as(c_int, sp10.value) +% (getRoomTrapLevel() +% 1) *% 10);
    sp10.@"type" = psx.getBits(params, "type");
    sp10.sourceAffinity = psx.conv(u8, @as(c_int, psx.getBits(params, "affinity")) -% 1);
    sp10.mediatingStat = psx.conv(i16, actor.strength);
    sp10.armorStat = psx.at(&actor.limbs, limb).armor.currentStr;
    sp10.shieldStat = 0;
    sp10.armorStatFactor = 100;
    sp10.shieldStatFactor = 100;
    sp10.statFactor = 100;
    if (sp10.@"type" != 0) {
        sp10.statFactor = psx.conv(i16, @as(c_int, psx.at(&psx.at(&actor.limbs, limb).types, sp10.@"type").*) +% 100);
        sp10.armorStatFactor = psx.conv(i16, @as(c_int, psx.at(&psx.at(&actor.limbs, limb).armor.types, sp10.@"type").*) +% 100);
    }
    if (sp10.sourceAffinity != 0) {
        sp10.statFactor = psx.conv(i16, @as(c_int, sp10.statFactor) +% psx.at(&psx.at(&actor.limbs, limb).affinities, sp10.sourceAffinity).*);
        sp10.armorStatFactor = psx.conv(i16, @as(c_int, sp10.armorStatFactor) +%
            psx.at(&psx.at(&actor.limbs, limb).armor.classAffinityCurrent.affinity[0], sp10.sourceAffinity).*);
    }
    if (psx.at(&actor.limbs, limb).armor.maxDp != 0) {
        const armor = &psx.at(&actor.limbs, limb).armor;
        var dpMod = @divTrunc((@as(c_int, armor.maxDp) - armor.currentDp) *% 100, @as(c_int, armor.maxDp));
        if (dpMod < 0) {
            dpMod +%= 3;
        }
        sp10.armorStatFactor = psx.conv(i16, @as(c_int, sp10.armorStatFactor) -% (dpMod >> 2));
    }
    if (v.g.vs_battle_actors()[actor.unk957].?.weaponDrawn & 1 != 0) {
        sp10.shieldStat = actor.shield.currentStr;
        if (sp10.@"type" != 0) {
            sp10.shieldStatFactor = psx.conv(i16, @as(c_int, sp10.shieldStatFactor) +% psx.at(&actor.shield.types, sp10.@"type").*);
        }
        if (sp10.sourceAffinity != 0) {
            sp10.shieldStatFactor = psx.conv(i16, @as(c_int, sp10.shieldStatFactor) +%
                psx.at(&actor.shield.classAffinityCurrent.affinity[0], sp10.sourceAffinity).*);
        }
        if (actor.shield.maxDp != 0) {
            var dpMod = @divTrunc((@as(c_int, actor.shield.maxDp) - actor.shield.currentDp) *% 100, @as(c_int, actor.shield.maxDp));
            if (dpMod < 0) {
                dpMod +%= 3;
            }
            sp10.shieldStatFactor = psx.conv(i16, @as(c_int, sp10.shieldStatFactor) -% (dpMod >> 2));
        }
        if (actor.shield.maxPp != 0) {
            var dpMod = @divTrunc(@as(c_int, actor.shield.currentPp) *% 100, @as(c_int, actor.shield.maxPp));
            if (dpMod < 0) {
                dpMod +%= 3;
            }
            sp10.shieldStatFactor = psx.conv(i16, @as(c_int, sp10.shieldStatFactor) +% (dpMod >> 2));
        }
    }
    sp10.mediatingStat = psx.conv(i16, @as(c_int, sp10.mediatingStat) +% actor.accessory.currentStr);
    sp10.statFactor = psx.conv(i16, @as(c_int, sp10.statFactor) +%
        (@as(c_int, psx.at(&actor.accessory.types, sp10.@"type").*) +% psx.at(&actor.accessory.affinities, sp10.sourceAffinity).*));
    var defense: c_int = (@as(c_int, sp10.mediatingStat) -% 20) *% sp10.statFactor;
    defense +%= @as(c_int, sp10.shieldStat) *% sp10.shieldStatFactor;
    defense +%= @as(c_int, sp10.armorStat) *% sp10.armorStatFactor;
    defense = @divTrunc(defense, @divTrunc(@as(c_int, actor.risk), 2) + 100); // risk is a u16: no overflow
    const value: c_int = sp10.value;
    var amount = psx.conv(i16, value -% defense);
    if (amount < 0) {
        amount = 0;
    }
    var randMod: c_int = 0;
    if (withRandMod != 0) {
        randMod = c.vs_main_getRandSmoothed(11) -% 5;
    }
    amount = psx.conv(i16, @as(c_int, amount) +% randMod);
    if (amount < 0) {
        amount = 0;
    }
    if (amount >= actor.currentHP) {
        amount = psx.conv(i16, @as(c_int, actor.currentHP) -% 1);
    }
    return amount;
}

/// orig: _getRoomTrapLevel (src/BATTLE/BATTLE.PRG/146C.c)
/// The room's trap level (section B). Static in the C: ported with each caller (PORTING.md §13).
fn getRoomTrapLevel() c_int {
    return psx.get(v.g.vs_battle_roomData().sectionB).?.trapLevel;
}
