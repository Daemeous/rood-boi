//! An action effect: raises the target's agility by 1..4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _increaseAgilityRand (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds vs_main_getRand(4) + 1 to the target's agility and marks its agilityEffect 2 (up).
pub fn increaseAgilityRand(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    // mod/hooks.h: always +4 in the Merlose build and the extras (VS_CALLO_STAT_ROLL)
    const amount = (if (comptime @hasDecl(c, "vs_mod_statItemsAlwaysMax")) @as(c_int, 3) else c.vs_main_getRand(4)) +% 1;
    const entity = target.?;
    entity.agility = psx.conv(i16, @as(c_int, entity.agility) +% amount);
    psx.setBits(entity, "agilityEffect", 2);
    return 0; // no return statement in the C (the result register's leftover)
}
