//! Battle overlay unit 4A0A8.c: a single-argument call wrapper.
const c = @import("c_4A0A8");

/// orig: func_800BB450 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes the byte at `arg0[1]` to vs_render_setFramebufferEffectEnabled and returns 0.
pub fn func_800BB450(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_render_setFramebufferEffectEnabled(@as(c_int, arg0[1]));
    return 0;
}
