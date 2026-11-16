//! Places the ability-timing marker above the player (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80096FF0 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// With the ability-timing display on, reads the player's screen point into vs_battle_abilityTimingCuePosition (kept 40 below the top,
/// shifted sideways by as much) and adds `ticks` to the timer vs_battle_abilityTimingCueFrames.
pub fn func_80096FF0(ticks: c_int) callconv(.c) void {
    if (psx.getBits(v.g.vs_main_settings(), "abilityTimingDisplay") == 0) return;

    const point = v.g.vs_battle_abilityTimingCuePosition();
    if (c.vs_actors_targetPoint(0, 0xFB, @ptrCast(point), 0) != 0) return;

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

    const timer = v.g.vs_battle_abilityTimingCueFrames();
    timer.* = psx.conv(i16, @as(c_int, timer.*) +% ticks);
}
