//! Dispatches the 30DB0 unit's per-slot routine over one slot, or over all 17 when arg0 is -1 (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_setActorSoundOverrides (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs vs_actors_applyActorSoundOverrides on slot arg0, or on slots 0..16 when arg0 is -1.
pub fn vs_actors_setActorSoundOverrides(arg0: c_int, arg1: ?*v.D_800F4538_unk64) callconv(.c) void {
    if (arg0 == -1) {
        var i: c_int = 0;
        while (i < 17) : (i +%= 1) {
            c.vs_actors_applyActorSoundOverrides(i, @ptrCast(arg1));
        }
    } else {
        c.vs_actors_applyActorSoundOverrides(arg0, @ptrCast(arg1));
    }
}
