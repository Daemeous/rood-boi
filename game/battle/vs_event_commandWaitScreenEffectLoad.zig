//! The battle's pending-state test (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_event_commandWaitScreenEffectLoad (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when the global vs_effects_screenEffectOverlay points at a record whose first byte is nonzero, else 0.
pub fn vs_event_commandWaitScreenEffectLoad(_: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.vs_effects_screenEffectOverlay().*) |node| {
        if (node.unk0 != 0) return 1;
    }
    return 0;
}
