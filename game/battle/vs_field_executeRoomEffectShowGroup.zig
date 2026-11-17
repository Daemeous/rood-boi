//! Passes a word of the second argument block to vs_field_showRoomGeometryGroup (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_executeRoomEffectShowGroup (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Calls vs_field_showRoomGeometryGroup with the first word of the second argument's table and returns 0.
pub fn vs_field_executeRoomEffectShowGroup(_: ?*v.func_80092F74_t, arg1: ?*v.vs_field_RoomEffectCommand) callconv(.c) c_int {
    _ = c.vs_field_showRoomGeometryGroup(arg1.?.operands[0].s32);
    return 0;
}
