//! An action effect: lowers the target's agility by 1..4 (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _decreaseAglityRand (src/BATTLE/BATTLE.PRG/146C.c)
/// Adds vs_main_getRand(4) + 1 to the target's agility and marks its agilityEffect 1 (down).
pub fn decreaseAglityRand(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    const amount = c.vs_main_getRand(4) +% 1;
    const entity = target.?;
    // The C adds the amount here too: effect 1 is what marks it a decrease.
    entity.agility = psx.conv(i16, @as(c_int, entity.agility) +% amount);
    psx.setBits(entity, "agilityEffect", 1);
    return 0; // no return statement in the C (the result register's leftover)
}
