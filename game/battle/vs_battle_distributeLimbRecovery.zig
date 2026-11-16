//! Spreads an incoming hit's damage over a target's limbs by their lost HP
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_battle_distributeLimbRecovery (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Splits `amount` over the target's damaged limbs in proportion to each limb's lost HP.
pub fn vs_battle_distributeLimbRecovery(_: ?*v.vs_action_t, entity: ?*v._hitEntity_t, amount: c_short) callconv(.c) void {
    const hit = entity.?;
    const actor = psx.get(v.g.vs_battle_actors()[hit.target.targetActor].?.stats).?;
    var limbDamageTaken: c_int = 0;

    var i: usize = 0;
    while (i < 6) : (i += 1) {
        if (actor.limbs[i].maxHp != 0) {
            limbDamageTaken +%= @as(c_int, actor.limbs[i].maxHp) -% @as(c_int, actor.limbs[i].hp);
        }
    }

    if (limbDamageTaken != 0) {
        i = 0;
        while (i < 6) : (i += 1) {
            if (actor.limbs[i].maxHp != 0) {
                const lost: c_int = @as(c_int, actor.limbs[i].maxHp) -% @as(c_int, actor.limbs[i].hp);
                const temp_lo: c_int = @divTrunc(@as(c_int, amount) *% lost, limbDamageTaken);
                v.g._armorDpAdjustmentAmounts()[i] = psx.conv(i16, temp_lo);
                // (temp_lo << 16) >> 15: the shift is done on the bits, as GNU C does for negative values.
                const shifted: c_int = @bitCast(@as(c_uint, @bitCast(temp_lo)) << 16);
                const scaled: c_int = shifted >> 15;
                const value0 = &hit.limbs[i].effects.value0;
                value0.* = psx.conv(i16, @as(c_int, value0.*) +% scaled);
            }
        }
    }
}
