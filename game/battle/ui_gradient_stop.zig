//! A colour stop of a menu gradient (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_uiGradientStop (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The two end colours for `color_index` (a blend of two fixed pairs below 9, else table D_800EBBB8 / D_800EBBC0), lerped by `gradient`.
pub fn uiGradientStop(gradient: c_uint, color_index: c_uint, brightness: c_int) callconv(.c) c_int {
    var color0: c_uint = undefined;
    var color1: c_uint = undefined;
    if (color_index < 9) {
        color0 = c._lerpColor(0x6B4100, 0x6C8219, 0x80, @bitCast(color_index));
        color1 = c._lerpColor(0x330500, 0x262801, 0x80, @bitCast(color_index));
    } else {
        // An unsigned index: 9..15 give 0xFFFFFFFF, as in the C.
        color0 = psx.at(v.g.D_800EBBB8(), (color_index >> 3) -% 2).*;
        color1 = psx.at(v.g.D_800EBBC0(), (color_index >> 3) -% 2).*;
    }
    return @bitCast(c._lerpColor(color0, color1, brightness, @bitCast(gradient)));
}
