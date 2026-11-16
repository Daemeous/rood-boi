//! Forwards a call to vs_actors_setActorTint unless the low flag nibble of arg0 is set (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: vs_battle_setTargetActorColorOverride (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls vs_actors_setActorTint with the same arguments when no bit of arg0's 0xF0 mask is set.
pub fn vs_battle_setTargetActorColorOverride(arg0: c_int, arg1: ?*anyopaque) callconv(.c) void {
    if ((arg0 & 0xF0) == 0) {
        _ = c.vs_actors_setActorTint(arg0, @ptrCast(arg1));
    }
}
