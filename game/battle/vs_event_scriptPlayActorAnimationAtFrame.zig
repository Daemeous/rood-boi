//! An event script opcode that hands vs_actors_playActorAnimationAtFrame an actor and a table-based value (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptPlayActorAnimationAtFrame (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_playActorAnimationAtFrame with the actor of getShort(script + 1), D_800E9B58[script[6] >> 6] +
/// script[3], script[6] & 0x3F and script[4].
pub fn vs_event_scriptPlayActorAnimationAtFrame(script: [*c]u8) callconv(.c) c_int {
    const actor: c_int = c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    _ = c.vs_actors_playActorAnimationAtFrame(actor, @as(c_int, v.g.D_800E9B58()[script[6] >> 6]) + script[3], script[6] & 0x3F, script[4]);
    return 0;
}
