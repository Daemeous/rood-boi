//! Battle overlay wrapper: runs vs_field_releaseTiledBackground, then returns 4 if vs_event_skipState is 2, else 0
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B9F90 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_field_releaseTiledBackground, then returns 4 when vs_event_skipState equals 2, otherwise 0.
pub fn func_800B9F90(_: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_field_releaseTiledBackground();
    return if (v.g.vs_event_skipState().* == 2) 4 else 0;
}
