//! An action effect: the target actor's statuses 0x1FFE1FE0 inflicted (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: func_8008418C (src/BATTLE/BATTLE.PRG/146C.c)
/// Adds the target actor's current statuses, masked 0x1FFE1FE0, to the entity's inflicted statuses.
pub fn func_8008418C(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    const entity = target.?;
    entity.inflictStatuses |= psx.get(v.g.vs_battle_actors()[entity.unk0.targetActor].?.stats).?.statuses & 0x1FFE1FE0;
    return 0; // no return statement in the C (the result register's leftover)
}
