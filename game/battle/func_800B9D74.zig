//! An event script opcode that hands vs_actors_startActorAppearanceTransition an actor and a byte (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B9D74 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_startActorAppearanceTransition with the actor of getShort(script + 1) and script[3].
pub fn func_800B9D74(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_actors_startActorAppearanceTransition(c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))), script[3]);
    return 0;
}
