//! Ground clearance test for a battle actor, along one axis
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A69B4 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Tests the ground either side of arg0, a step out along x or z: returns 1 when both ends are within 64 of its height.
pub fn func_800A69B4(arg0: ?*v.D_800F4538_t) callconv(.c) c_int {
    var vec: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    var ret: c_int = 0;
    // rsin(ONE / 8): the product wraps in int, then truncates toward zero (signed /).
    var h: c_int = @divTrunc(c.rsin(4096 / 8) *% @as(c_int, arg0.?.unk63C), 4096);

    if (((@as(c_int, v.g.D_800F49F9().*) >> 1) & 1) != 0) {
        vec.vz = psx.conv(i16, h);
    } else {
        vec.vx = psx.conv(i16, h);
    }

    const base = psx.cptr(c.SVECTOR, v.g.D_800F4B08());
    // The call is made before D_800F4B08.vy is read, as the C orders it.
    h = c.func_800A6EE8(base, @as(c_int, vec.vx), @as(c_int, vec.vz), 1) -% @as(c_int, v.g.D_800F4B08().vy);
    if (h > -64) {
        h = c.func_800A6EE8(base, -%@as(c_int, vec.vx), -%@as(c_int, vec.vz), 1) -% @as(c_int, v.g.D_800F4B08().vy);
        if (h > -64) {
            ret = 1;
        }
    }
    return ret;
}
