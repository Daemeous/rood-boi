//! An action effect: inflicted statuses 0x1E, hp set to the target actor's current HP (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80084248 (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets the inflicted statuses 0x1E, then the entity's hp to the target actor's current HP (hpEffect 1).
pub fn func_80084248(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    _ = c._setInflictedStatuses(0x1E, @ptrCast(target));
    const entity = target.?;
    entity.unk0.hp = psx.get(v.g.vs_battle_actors()[entity.unk0.targetActor].?.stats).?.currentHP;
    psx.setBits(entity, "hpEffect", 1);
    return 0; // no return statement in the C (the result register's leftover)
}
