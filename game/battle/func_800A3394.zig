//! Lowers a point above the floor until it clears the solid check, writing its height back
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A3394 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Lowers the height of a position above the floor until it clears `func_800A3310`, retrying once from a lower start if it sinks under the floor, then stores the height into `arg1`.
pub fn func_800A3394(arg0: c_int, arg1: ?*v.SVECTOR) callconv(.c) void {
    var pos: v.SVECTOR = undefined;
    const actor = psx.at(v.g.D_800F4538(), arg0).*;
    const out = arg1.?;

    pos.vx = out.vx;
    pos.vz = out.vz;
    pos.vy = psx.conv(i16, @as(c_int, out.vy) -% 0xC0);
    const floor = c.func_800E75EC();
    var retried = false;

    while (true) {
        if (c.func_800A3310(arg0, psx.cptr(c.SVECTOR, &pos)) != 0) {
            pos.vy = psx.conv(i16, @as(c_int, pos.vy) -% 0xC0);
        } else if (floor != 0 and @as(c_int, pos.vy) -% @as(c_int, actor.?.menuCameraHeightOffset) < floor) {
            if (retried) return;
            retried = true;
            pos.vy = psx.conv(i16, @as(c_int, out.vy) -% 0x60);
        } else {
            out.vy = pos.vy;
            return;
        }
    }
}
