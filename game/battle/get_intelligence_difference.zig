//! Intelligence difference of a hit: the source's and target's intelligence, armour and weapons,
//! with gem buffs (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _getIntelligenceDifference (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Works out a hit's intelligence difference (source over target), clamped to 0..254.
pub fn getIntelligenceDifference(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hitNumber: c_int, addVariance: c_int) callconv(.c) c_int {
    const targetLimb: c_int = @as(c_int, target.?.target.targetLimb);

    const sourceActor = psx.get(v.g.vs_battle_actors()[source.?.target.targetActor].?.stats).?;
    const targetActor = psx.get(v.g.vs_battle_actors()[target.?.target.targetActor].?.stats).?;

    var sourceActorValue: c_int = @as(c_int, sourceActor.intelligence) +% @as(c_int, sourceActor.accessory.currentInt);
    var targetActorValue: c_int = @as(c_int, targetActor.intelligence) +% @as(c_int, targetActor.accessory.currentInt);

    for (0..6) |i| {
        sourceActorValue +%= @as(c_int, sourceActor.limbs[i].armor.currentInt);
        targetActorValue +%= @as(c_int, targetActor.limbs[i].armor.currentInt);
    }

    if ((v.g.vs_battle_actors()[sourceActor.actorId].?.weaponDrawn & 1) != 0) {
        sourceActorValue +%= @as(c_int, sourceActor.weapon.currentInt) +% @as(c_int, sourceActor.shield.currentInt);
    }

    if ((v.g.vs_battle_actors()[targetActor.actorId].?.weaponDrawn & 1) != 0) {
        targetActorValue +%= @as(c_int, targetActor.weapon.currentInt) +% @as(c_int, targetActor.shield.currentInt);
    }

    sourceActorValue = @divTrunc(sourceActorValue *% (100 -% @as(c_int, sourceActor.risk)), 100);
    // targetLimb is read unchecked, as the C indexes the limbs array without a bound.
    targetActorValue +%= @as(c_int, psx.at(&targetActor.limbs, targetLimb).agilityDefenseBonus);
    targetActorValue = @divTrunc(targetActorValue *% (100 -% @as(c_int, targetActor.risk)), 100);
    targetActorValue = @divTrunc(targetActorValue *% @as(c_int, targetActor.currentHP), @as(c_int, targetActor.maxHP));

    var intDiff: c_int = sourceActorValue -% targetActorValue;

    if (addVariance != 0) {
        const variance = c.vs_main_getRandSmoothed(11);
        const v1 = intDiff -% 5;
        intDiff = v1 +% variance;
    }

    intDiff +%= @as(c_int, c._getAttackGemBuff(@ptrCast(action), @ptrCast(sourceActor)));
    intDiff -%= @as(c_int, c._getDefenseGemBuff(@ptrCast(action), @ptrCast(targetActor), hitNumber));

    if (intDiff < 0) {
        intDiff = 0;
    } else if (intDiff == 255) {
        intDiff = 254;
    }

    return intDiff;
}
