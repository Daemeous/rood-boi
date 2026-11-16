//! Reads the relative jump target at a script position, or closes a pending
//! state 2 step (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptEndCutsceneAndJump (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears the pending flag vs_event_skipState; if it was 2, runs vs_event_finishSkippedCutscene and returns 4, else returns the address of the script's 16-bit relative target.
pub fn vs_event_scriptEndCutsceneAndJump(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_v1: u8 = v.g.vs_event_skipState().*;
    v.g.vs_event_skipState().* = 0;
    if (temp_v1 == 2) {
        c.vs_event_finishSkippedCutscene(arg0);
        return 4;
    }
    // arg0 + 1 is a plain 32-bit guest address; the C's pointer arithmetic wraps there.
    const base: u32 = psx.conv(u32, @intFromPtr(arg0));
    const next: [*c]u8 = @ptrFromInt(@as(usize, base +% 1));
    const off: c_short = c.vs_battle_getShort(next);
    // &arg0[off]: the signed index sign-extends, and the sum wraps as the 32-bit address does.
    const target: u32 = base +% psx.conv(u32, off);
    return psx.conv(c_int, target);
}
