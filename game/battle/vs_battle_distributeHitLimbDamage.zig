//! Spreads a hit's base value over the target's limbs by the action's class (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_distributeHitLimbDamage (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets each limb's armor DP adjustment for an action and adds it to the entity's limb effects.
pub fn vs_battle_distributeHitLimbDamage(action: ?*v.vs_action_t, entity: ?*v._hitEntity_t, baseValue: c_short) callconv(.c) void {
    const adjustments = v.g._armorDpAdjustmentAmounts();
    const ent = entity.?;
    var i: usize = 0;
    while (i < 6) : (i += 1) {
        adjustments[i] = 0;
    }

    const actor = psx.get(v.g.vs_battle_actors()[ent.unk0.targetActor].?.stats).?;

    switch (psx.getBits(action.?, "unk2_4")) {
        0, 1, 2, 3, 4 => {
            var limbs: c_int = 0;
            i = 0;
            while (i < 6) : (i += 1) {
                if (actor.limbs[i].maxHp != 0) {
                    limbs +%= 1;
                }
            }

            i = 0;
            while (i < 6) : (i += 1) {
                if (actor.limbs[i].maxHp != 0) {
                    // limbs >= 1 here: this limb has maxHp != 0.
                    adjustments[i] = psx.conv(i16, @divTrunc(@as(c_int, baseValue), limbs));
                    ent.limbs[i].effects.value0 = psx.conv(i16, @as(c_int, ent.limbs[i].effects.value0) +% @as(c_int, adjustments[i]));
                }
            }
        },
        5, 7, 14 => {
            const splash = psx.at(&actor.dpSplash, ent.unk0.targetLimb);
            i = 0;
            while (i < 6) : (i += 1) {
                const dp: c_int = @as(c_int, baseValue) *% @as(c_int, splash[i]);
                adjustments[i] = psx.conv(i16, @divTrunc(dp, 100));
                ent.limbs[i].effects.value0 = psx.conv(i16, @as(c_int, ent.limbs[i].effects.value0) +% @as(c_int, adjustments[i]));
            }
        },
        6, 8, 11, 12 => {
            const target = ent.unk0.targetLimb;
            psx.at(adjustments, target).* = baseValue;
            const limb = psx.at(&ent.limbs, target);
            limb.effects.value0 = psx.conv(i16, @as(c_int, limb.effects.value0) +% @as(c_int, baseValue));
        },
        else => {},
    }
}
