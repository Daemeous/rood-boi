//! Battle unit 146C: removes the actor at an index (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_146C");

/// orig: vs_actors_removeActorAtSlot (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Removes the actor at index `arg0`, passing the C's extra 0 argument.
pub fn vs_actors_removeActorAtSlot(arg0: c_int) callconv(.c) void {
    _ = c._removeActorAtIndex(psx.conv(c_uint, arg0), 0);
}
