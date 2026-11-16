//! Runs the per-slot update for each flagged slot, then the shared tail (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: vs_event_releaseScriptActorControl (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Calls func_8007D260 for each of the 17 slots flagged in vs_event_scriptControlledActors, then func_800BBDDC.
pub fn vs_event_releaseScriptActorControl() callconv(.c) void {
    const flags = v.g.vs_event_scriptControlledActors();
    var i: c_int = 0;
    while (i < 17) : (i +%= 1) {
        // i is 0..16 here, so the index is in range.
        if (flags[@intCast(i)] != 0) {
            c.func_8007D260(i);
        }
    }
    c.func_800BBDDC();
}
