//! Stores a value in vs_effects_tintedActor and calls vs_render_transformActorColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006D028 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores `arg0` in vs_effects_tintedActor, then calls vs_render_transformActorColors with 0xE, 8, `arg0`, 0x10, 0, 0.
pub fn func_8006D028(arg0: c_int) callconv(.c) void {
    v.g.vs_effects_tintedActor().* = arg0;
    c.vs_render_transformActorColors(0xE, 8, arg0, 0x10, 0, 0);
}
