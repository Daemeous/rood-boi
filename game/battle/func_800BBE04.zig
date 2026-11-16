//! Stores a byte to the battle global vs_event_skipNextFieldCameraRestore (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: func_800BBE04 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets vs_event_skipNextFieldCameraRestore to the low byte of `arg0`.
pub fn func_800BBE04(arg0: c_int) callconv(.c) void {
    v.g.vs_event_skipNextFieldCameraRestore().* = psx.conv(u8, arg0);
}
