//! Passes a byte of the caller's record to vs_actors_setOtherActorLogicEnabled (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetNpcUpdatesEnabled (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_setOtherActorLogicEnabled with the byte at offset 1 of its argument; always returns 0.
pub fn vs_event_scriptSetNpcUpdatesEnabled(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_actors_setOtherActorLogicEnabled(@as(c_int, psx.at(arg0, 1).*));
    return 0;
}
