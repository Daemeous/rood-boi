//! Battle wait check on a script byte: holds while the state is 2 and the byte is 0 or 5 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandWaitEffectState (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Holds a battle wait on byte 1 of the script while the state is 2, releasing it when func_8007BEE4 matches that byte.
pub fn vs_event_commandWaitEffectState(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.vs_event_skipState().* == 2 and (arg0[1] == 0 or arg0[1] == 5)) {
        if (v.g.vs_event_isEffectWaitPending().* != 0) {
            if (c.func_8007BEE4() == @as(c_int, arg0[1])) {
                v.g.vs_event_isEffectWaitPending().* = 0;
                return 0;
            }
        } else {
            c.vs_effects_clearPrimaryEffects();
            v.g.vs_event_isEffectWaitPending().* = 1;
        }
        return 1;
    }
    return @as(c_int, @intFromBool(c.func_8007BEE4() != @as(c_int, arg0[1])));
}
