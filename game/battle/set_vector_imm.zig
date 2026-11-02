//! Vector loading from immediate values in a script stream (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: _setVectorImm (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets a vector from three 16-bit immediates in a script stream, each scaled by ONE (4096).
pub fn setVectorImm(arg0: ?*v.VECTOR, arg1: [*c]u8) callconv(.c) ?*v.VECTOR {
    arg0.?.vx = @as(c_int, c.vs_battle_getShort(arg1)) *% 4096;
    arg0.?.vy = @as(c_int, c.vs_battle_getShort(arg1 + 2)) *% 4096;
    arg0.?.vz = @as(c_int, c.vs_battle_getShort(arg1 + 4)) *% 4096;
    return arg0;
}
