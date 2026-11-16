//! Clears the two battle counters at vs_field_pendingGimmickAnimationCount/vs_field_triggerMarkerFrameTicks and the table at vs_field_gimmickAnimationQueue (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_resetGimmickAnimationQueue (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the two counters and zeroes the eight-entry table they sit beside.
pub fn vs_field_resetGimmickAnimationQueue() callconv(.c) void {
    v.g.vs_field_pendingGimmickAnimationCount().* = 0;
    v.g.vs_field_triggerMarkerFrameTicks().* = 0;
    const table = v.g.vs_field_gimmickAnimationQueue();
    c.vs_main_bzero(table, @sizeOf(@TypeOf(table.*)));
}
