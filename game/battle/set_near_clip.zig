//! Sets the battle near clip distance and passes it on to vs_render_setRoomPolygonNearClip
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_setNearClip (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the near clip distance and hands it to vs_render_setRoomPolygonNearClip.
pub fn setNearClip(nearClip: c_int) callconv(.c) void {
    v.g.vs_main_nearClip().* = nearClip;
    c.vs_render_setRoomPolygonNearClip(nearClip);
}
