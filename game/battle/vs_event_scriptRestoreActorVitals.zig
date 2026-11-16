//! An event script opcode that hands vs_actors_restoreActorVitals an actor (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptRestoreActorVitals (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_restoreActorVitals with the actor of getShort(script + 1).
pub fn vs_event_scriptRestoreActorVitals(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_actors_restoreActorVitals(c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    return 0;
}
