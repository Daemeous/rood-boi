//! Font style globals for the battle text system (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_setFontStyle (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Resets the font table and brightness and stores the style argument.
pub fn setFontStyle(arg0: c_int) callconv(.c) void {
    v.g._fontTable().* = 0;
    v.g._fontBrightness().* = 384;
    v.g.D_800F4CB8().* = psx.conv(u8, arg0);
}
