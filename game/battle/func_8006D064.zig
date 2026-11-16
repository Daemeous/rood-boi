//! Clears the battle flag vs_effects_tintedActor after handing its value to vs_render_transformActorColors
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006D064 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_render_transformActorColors with the flag vs_effects_tintedActor, then clears the flag.
pub fn func_8006D064() callconv(.c) void {
    c.vs_render_transformActorColors(8, 4, v.g.vs_effects_tintedActor().*, 0, 0, 0);
    v.g.vs_effects_tintedActor().* = 0;
}
