//! The room's section-12 timer (src/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8006CE50 (src/BATTLE/BATTLE.PRG/146C.c)
/// Starts the section-12 timer at 180 ticks (an event script opcode's action).
pub fn startSection12Timer() callconv(.c) void {
    c.func_8008D5A0(0xB4);
}
