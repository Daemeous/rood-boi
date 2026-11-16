//! Loads the event file named by the script operand at arg0+1 into a 0x1800-byte heap buffer (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandLoadNextEvent (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads the event file's index from the script, allocates its buffer and loads it; returns 0.
pub fn vs_event_commandLoadNextEvent(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    v.g._evtFile().* = c.vs_battle_getShort(arg0 + 1);
    v.g.vs_event_chainedEventBuffer().* = c.vs_main_allocHeapR(0x1800);
    c.vs_event_queueEventScriptLoad(v.g._evtFile().*, v.g.vs_event_chainedEventBuffer().*);
    v.g.vs_event_isChainedEventLoaded().* = 1;
    return 0;
}
