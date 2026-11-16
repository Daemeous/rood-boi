//! Stores a value into the battle global vs_battle_hudBattleMode (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_setHudBattleMode (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores the low byte of `arg0` in vs_battle_hudBattleMode.
pub fn vs_battle_setHudBattleMode(arg0: c_int) callconv(.c) void {
    v.g.vs_battle_hudBattleMode().* = psx.conv(u8, arg0);
}
