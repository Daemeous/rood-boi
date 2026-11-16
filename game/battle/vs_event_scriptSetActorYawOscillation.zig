//! An event script opcode that hands func_8009D7E8 an actor and a byte (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetActorYawOscillation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8009D7E8 with the actor of getShort(script + 1) and script[3].
pub fn vs_event_scriptSetActorYawOscillation(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8009D7E8(c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))), script[3]);
    return 0;
}
