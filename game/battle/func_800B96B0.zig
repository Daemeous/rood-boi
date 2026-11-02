//! Held-button timer for one slot of D_800F4C58 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800B96B0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Arms slot `arg1` from arg0[2] on first use, then counts it down until a button in arg0[1] is pressed or it runs out.
pub fn func_800B96B0(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    const slot = psx.at(v.g.D_800F4C58(), arg1);
    if (slot.* == 0) {
        slot.* = psx.conv(i16, arg0[2]);
        return 1;
    }
    const pressed = v.g.vs_main_buttonsPressed();
    const buttonHit = (pressed.all & @as(u32, arg0[1])) != 0;
    if (!buttonHit) {
        slot.* -%= 1;
    }
    if (buttonHit or @as(c_int, slot.*) <= 0) {
        slot.* = 0;
        pressed.all = 0;
        return 0;
    }
    return 1;
}
