//! Forwards a call to vs_actors_setActorTranslation in the same unit (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_setActorMotion (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Forwards its three arguments unchanged to vs_actors_setActorTranslation.
pub fn vs_actors_setActorMotion(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    const vec: [*c]c.SVECTOR = if (arg1) |p| psx.cptr(c.SVECTOR, p) else null;
    c.vs_actors_setActorTranslation(arg0, vec, arg2);
}
