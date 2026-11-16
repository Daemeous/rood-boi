//! Tests or clears one slot of vs_event_threadCursors, chosen by a script byte (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_event_commandWaitScriptContext (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears slot arg0[1] & 3 of vs_event_threadCursors when bit 7 of arg0[1] is set; otherwise reports whether that slot is set.
pub fn vs_event_commandWaitScriptContext(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const slot: usize = arg0[1] & 3;
    const slots = v.g.vs_event_threadCursors();
    if ((arg0[1] & 0x80) != 0) {
        slots[slot] = null;
        return 0;
    }
    return @intFromBool(slots[slot] != null);
}
