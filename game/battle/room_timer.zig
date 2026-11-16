//! The room's section-12 timer (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8006CE50 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Starts the section-12 timer at 180 ticks (an event script opcode's action).
pub fn startSection12Timer() callconv(.c) void {
    c.vs_field_freezeMovingPlatforms(0xB4);
}
