//! Stores a value in the battle byte global vs_battle_hudHidden (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_setHudHidden (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores its argument in the byte global vs_battle_hudHidden, truncated to 8 bits as the C does.
pub fn vs_battle_setHudHidden(arg0: c_int) callconv(.c) void {
    v.g.vs_battle_hudHidden().* = psx.conv(u8, arg0);
}
