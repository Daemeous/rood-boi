//! Frees the heap block held in vs_event_camera (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BBDDC (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Frees the heap block held in vs_event_camera.
pub fn func_800BBDDC() callconv(.c) void {
    c.vs_main_freeHeapR(v.g.vs_event_camera().*);
}
