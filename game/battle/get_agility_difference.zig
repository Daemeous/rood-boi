//! Agility difference of a hit from source to target, with gem buffs, clamped to 0..254
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _getAgilityDifference (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Rolls the agility difference of a hit from source to target, with gem buffs, clamped to 0..254.
pub fn getAgilityDifference(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hitNumber: c_int, addVariance: c_int) callconv(.c) c_int {
    const targetLimb: c_int = @as(c_int, target.?.target.targetLimb);

    const sourceActor = psx.get(v.g.vs_battle_actors()[@as(usize, source.?.target.targetActor)].?.stats).?;
    const targetActor = psx.get(v.g.vs_battle_actors()[@as(usize, target.?.target.targetActor)].?.stats).?;

    var sourceActorValue: c_int = @as(c_int, sourceActor.agility) +% @as(c_int, sourceActor.accessory.currentAgility);
    var targetActorValue: c_int = @as(c_int, targetActor.agility) +% @as(c_int, targetActor.accessory.currentAgility);

    for (0..6) |i| {
        sourceActorValue +%= @as(c_int, sourceActor.limbs[i].armor.currentAgility);
        targetActorValue +%= @as(c_int, targetActor.limbs[i].armor.currentAgility);
    }

    if ((v.g.vs_battle_actors()[sourceActor.actorId].?.weaponDrawn & 1) != 0) {
        sourceActorValue +%= @as(c_int, sourceActor.weapon.currentAgility) +% @as(c_int, sourceActor.shield.currentAgility);
    }

    if ((v.g.vs_battle_actors()[targetActor.actorId].?.weaponDrawn & 1) != 0) {
        targetActorValue +%= @as(c_int, targetActor.weapon.currentAgility) +% @as(c_int, targetActor.shield.currentAgility);
    }

    sourceActorValue = @divTrunc(sourceActorValue *% (100 -% @as(c_int, sourceActor.risk)), 100);
    // targetLimb is signed (i8 from the target): psx.at indexes it as C does, without a bounds check.
    targetActorValue +%= @as(c_int, psx.at(&targetActor.limbs, targetLimb).agilityDefenseBonus);
    targetActorValue = @divTrunc(targetActorValue *% (100 -% @as(c_int, targetActor.risk)), 100);
    var agiDiff: c_int = sourceActorValue -% targetActorValue;
    agiDiff +%= 100;

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
