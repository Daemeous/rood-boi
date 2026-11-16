//! Calls vs_effects_branchScriptIf with whether the frame's flagged record is idle, then returns 1
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_branchIfTimelineFinished (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes whether the record's unk30 target has unk4 == 0 to vs_effects_branchScriptIf; always returns 1.
pub fn vs_effects_branchIfTimelineFinished(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const record = arg0.?;
    c.vs_effects_branchScriptIf(@intFromBool(psx.get(record.context.unk30).?.activeTracks == 0), @ptrCast(arg0));
    return 1;
}
