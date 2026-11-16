//! Agility contest between a hit's source and target, with the gem buffs
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _getAgilityDifference2 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Works out how far the target's agility outweighs the source's for a hit, clamped to 0..254.
pub fn getAgilityDifference2(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hitNumber: c_int, addVariance: c_int) callconv(.c) c_int {
    var agiDiff: c_int = undefined;
    var i: usize = undefined;
    const targetLimb: c_int = @as(c_int, target.?.target.targetLimb);

    const sourceActor = psx.get(v.g.vs_battle_actors()[source.?.target.targetActor].?.stats).?;
    const targetActor = psx.get(v.g.vs_battle_actors()[target.?.target.targetActor].?.stats).?;

    var sourceValue: c_int = @as(c_int, sourceActor.agility) +% @as(c_int, sourceActor.accessory.currentAgility);
    var targetValue: c_int = @as(c_int, targetActor.agility) +% @as(c_int, targetActor.accessory.currentAgility);

    i = 0;
    while (i < 6) : (i += 1) {
        sourceValue +%= @as(c_int, sourceActor.limbs[i].armor.currentAgility);
        targetValue +%= @as(c_int, targetActor.limbs[i].armor.currentAgility);
    }

    if ((v.g.vs_battle_actors()[sourceActor.actorId].?.weaponDrawn & 1) != 0) {
        sourceValue +%= @as(c_int, sourceActor.weapon.currentAgility) +% @as(c_int, sourceActor.shield.currentAgility);
    }

    if ((v.g.vs_battle_actors()[targetActor.actorId].?.weaponDrawn & 1) != 0) {
        targetValue +%= @as(c_int, targetActor.weapon.currentAgility) +% @as(c_int, targetActor.shield.currentAgility);
    }

    sourceValue = @divTrunc(sourceValue *% (100 -% @as(c_int, sourceActor.risk)), 100);
    targetValue +%= @as(c_int, psx.at(&targetActor.limbs, targetLimb).*.agilityDefenseBonus);
    targetValue = @divTrunc(targetValue *% (100 -% @as(c_int, targetActor.risk)), 100);
    agiDiff = sourceValue -% targetValue;
    const shifted: c_int = agiDiff +% 130;
    agiDiff = shifted -% @as(c_int, action.?.cost);

    if (addVariance != 0) {
        const variance: c_int = c.vs_main_getRandSmoothed(11);
        const v1: c_int = agiDiff -% 5;
        agiDiff = v1 +% variance;
    }

    agiDiff +%= @as(c_int, c._getAttackGemBuff(@ptrCast(action), @ptrCast(sourceActor)));
    agiDiff -%= @as(c_int, c._getDefenseGemBuff(@ptrCast(action), @ptrCast(targetActor), hitNumber));

    if (agiDiff < 0) {
        agiDiff = 0;
    } else if (agiDiff == 255) {
        agiDiff = 254;
    }

    return agiDiff;
}
