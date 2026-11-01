//! Places the ability-timing marker above the player (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80096FF0 (src/BATTLE/BATTLE.PRG/2842C.c)
/// With the ability-timing display on, reads the player's screen point into D_800F2280 (kept 40 below the top,
/// shifted sideways by as much) and adds `ticks` to the timer D_800F2272.
pub fn func_80096FF0(ticks: c_int) callconv(.c) void {
    if (psx.getBits(v.g.vs_main_settings(), "abilityTimingDisplay") == 0) return;

    const point = v.g.D_800F2280();
    if (c.func_800A1280(0, 0xFB, @ptrCast(point), 0) != 0) return;

    if (point.vy < 40) {
        const d: c_int = 40 -% @as(c_int, point.vy);
        point.vy = 40;
        if (@as(c_int, point.vx) +% d < 288) {
            point.vx = psx.conv(i16, @as(c_int, point.vx) +% d);
        } else {
            point.vx = psx.conv(i16, @as(c_int, point.vx) -% d);
        }
        point.vy = psx.conv(i16, @as(c_int, point.vy) +% d);
    }

    const timer = v.g.D_800F2272();
    timer.* = psx.conv(i16, @as(c_int, timer.*) +% ticks);
}
