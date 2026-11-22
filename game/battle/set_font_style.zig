//! Font style globals for the battle text system (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_setFontStyle (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Resets the font table and brightness and stores the style argument.
pub fn setFontStyle(arg0: c_int) callconv(.c) void {
    v.g.vs_battle_fontBank().* = 0;
    v.g._fontBrightness().* = 384;
    v.g.D_800F4CB8().* = psx.conv(u8, arg0);
}
