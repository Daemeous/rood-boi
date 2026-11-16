//! Battle unit 2842C: forwards a word from the message block to vs_field_hideRoomGeometryGroup (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_executeRoomEffectHideGroup (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Passes the first word of the block's message entry to vs_field_hideRoomGeometryGroup and returns 0.
pub fn vs_field_executeRoomEffectHideGroup(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    _ = c.vs_field_hideRoomGeometryGroup(arg1.?.unk4[0].s32);
    return 0;
}
