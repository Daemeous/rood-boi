//! Battle overlay unit 4A0A8: a small call-through that returns 0.
const c = @import("c_4A0A8");

/// orig: vs_event_scriptClearAshleyStatusEffects (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_battle_preserveAshleyStatuses and returns 0.
pub fn vs_event_scriptClearAshleyStatusEffects(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_battle_preserveAshleyStatuses();
    return 0;
}
