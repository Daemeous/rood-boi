//! Pass-through to the battle routine at 0x800AAA88 (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_setActorRotationMotion (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Forwards its three arguments unchanged to vs_actors_setActorRotation.
pub fn vs_actors_setActorRotationMotion(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    c.vs_actors_setActorRotation(arg0, @ptrCast(arg1), arg2);
}
