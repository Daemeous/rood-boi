//! Battle unit 4A0A8: forwards to func_8007BEC4 and, when vs_event_skipState is 2, to vs_effects_clearPrimaryEffects (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA90C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007BEC4 with the second byte of arg0, then vs_effects_clearPrimaryEffects if vs_event_skipState is 2; returns 0.
pub fn func_800BA90C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007BEC4(@as(c_int, arg0[1]));
    if (v.g.vs_event_skipState().* == 2) {
        c.vs_effects_clearPrimaryEffects();
    }
    return 0;
}
