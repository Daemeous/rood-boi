//! Effect slot dispatch for a script packet: arg0[1] selects a template or a slot
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandPlayVibrationPattern (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Dispatches the packet's slot byte: 0xFF clears through vs_input_stopVibrationPatterns, bit 3 copies a template into the scratch block first, otherwise the slot's block is used directly.
pub fn vs_event_commandPlayVibrationPattern(arg0: [*c]u8) callconv(.c) c_int {
    const a0: c_int = arg0[1];
    if (a0 == 0xFF) {
        _ = c.vs_input_stopVibrationPatterns(0, 2);
    } else if ((a0 & 8) != 0) {
        c.vs_main_memcpy(v.g.vs_event_vibrationPatternBuffer().*, v.g.vs_event_vibrationPatterns()[@intCast(a0 & 7)], 0x80);
        // vs_event_vibrationPatternBuffer + 8: void* arithmetic in bytes, as GNU C does it.
        const base = v.g.vs_event_vibrationPatternBuffer().*;
        _ = c.vs_input_startVibrationPattern(@ptrFromInt(@intFromPtr(base) +% 8), 0xFF, 2);
    } else {
        const entry = v.g.vs_event_vibrationPatterns()[@intCast(a0)];
        if (entry != null) {
            _ = c.vs_input_startVibrationPattern(@ptrFromInt(@intFromPtr(entry) +% 8), 0xFF, 2);
        }
    }
    return 0;
}
