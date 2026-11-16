//! Lowers a point above the floor until it clears the solid check, writing its height back
//! (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: vs_actors_separateActorFlightPlacement (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Lowers the height of a position above the floor until it clears `vs_actors_findActorAtFlightPosition`, retrying once from a lower start if it sinks under the floor, then stores the height into `arg1`.
pub fn vs_actors_separateActorFlightPlacement(arg0: c_int, arg1: ?*v.SVECTOR) callconv(.c) void {
    var pos: v.SVECTOR = undefined;
    const actor = psx.at(v.g.vs_actors_actorRecords(), arg0).*;
    const out = arg1.?;

    pos.vx = out.vx;
    pos.vz = out.vz;
    pos.vy = psx.conv(i16, @as(c_int, out.vy) -% 0xC0);
    const floor = c.vs_battle_aiRoomNavigationCeilingHeight();
    var retried = false;

    while (true) {
        if (c.vs_actors_findActorAtFlightPosition(arg0, psx.cptr(c.SVECTOR, &pos)) != 0) {
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
