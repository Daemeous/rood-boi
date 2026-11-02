//! An action effect: the target actor's statuses 0xE000 inflicted, unk1C_16 = 2 when any (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: func_800841C8 (src/BATTLE/BATTLE.PRG/146C.c)
/// Adds the target actor's statuses 0xE000 to the entity's inflicted statuses; when any, its unk1C_16 becomes 2.
pub fn func_800841C8(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    const entity = target.?;
    const statuses = psx.get(v.g.vs_battle_actors()[entity.unk0.targetActor].?.stats).?.statuses & 0xE000;
    entity.inflictStatuses |= statuses;
    if (statuses != 0) {
        psx.setBits(entity, "unk1C_16", 2);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
