//! CLUT transform over the three colour slots (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_146C");

/// orig: vs_render_transformRoomPalettes (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the CLUT transform on colour slots 0, 1 and 2 in turn, with the same arguments.
pub fn vs_render_transformRoomPalettes(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_short, arg4: c_short) callconv(.c) void {
    // The prototype's 4th parameter is a short: arg2 is converted to it at the call.
    c.vs_main_transformClutSlot(arg0, arg1, 0, psx.conv(c_short, arg2), @as(c_int, arg3), @as(c_int, arg4));
    c.vs_main_transformClutSlot(arg0, arg1, 1, psx.conv(c_short, arg2), @as(c_int, arg3), @as(c_int, arg4));
    c.vs_main_transformClutSlot(arg0, arg1, 2, psx.conv(c_short, arg2), @as(c_int, arg3), @as(c_int, arg4));
}
