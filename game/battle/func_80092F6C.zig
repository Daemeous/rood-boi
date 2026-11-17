//! Stub returning 0 for two unused arguments (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");

/// orig: func_80092F6C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Does nothing and returns 0; both arguments are unused.
pub fn func_80092F6C(_: ?*v.func_80092F74_t, _: ?*v.vs_field_RoomEffectCommand) callconv(.c) c_int {
    return 0;
}
