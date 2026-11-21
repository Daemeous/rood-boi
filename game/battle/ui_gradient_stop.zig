//! A colour stop of a menu gradient (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_uiGradientStop (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
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
    return @bitCast(panelTint(c._lerpColor(color0, color1, brightness, @bitCast(gradient))));
}

const PanelTint = *const fn (c_uint) callconv(.c) c_uint;

/// The menu tint (settings ui_menu_tint, pc/src/ui/ui_tint.cpp): every classic menu panel's gradient takes its colours from here, so a
/// tint other than the default (Original) gives the dark-blue ones the tint's hue at their own brightness, wherever a classic menu draws.
/// Text, icons and the 3D never come through here. Without the host hook, or at the Original tint, the colour is the game's.
fn panelTint(color: c_uint) c_uint {
    const hook = psx.weak(PanelTint, "pc_ui_classic_tint") orelse return color;
    return hook(color);
}
