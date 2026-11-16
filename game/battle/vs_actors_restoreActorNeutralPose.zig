//! Battle helper at 30DB0: forwards its argument to vs_actors_restoreActorBasePose with mode 6.
const c = @import("c_30DB0");

/// orig: vs_actors_restoreActorNeutralPose (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Calls vs_actors_restoreActorBasePose with the given argument and mode 6.
pub fn vs_actors_restoreActorNeutralPose(arg0: c_int) callconv(.c) void {
    c.vs_actors_restoreActorBasePose(arg0, 6);
}
