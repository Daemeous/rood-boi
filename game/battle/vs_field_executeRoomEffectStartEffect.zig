//! Passes a stored word from its second argument to vs_field_startRoomEffect and returns 0
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_executeRoomEffectStartEffect (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Forwards arg1's first unk4 word to vs_field_startRoomEffect; the first argument is unused.
pub fn vs_field_executeRoomEffectStartEffect(_: ?*v.func_80092F74_t, arg1: ?*v.vs_field_RoomEffectCommand) callconv(.c) c_int {
    _ = c.vs_field_startRoomEffect(arg1.?.operands[0].s32);
    return 0;
}
