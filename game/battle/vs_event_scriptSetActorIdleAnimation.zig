//! An event script opcode that hands vs_actors_setActorTravelAnimation an actor and a table-based value (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetActorIdleAnimation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_setActorTravelAnimation with the actor of getShort(script + 1) and D_800E9B58[script[3]] +
/// script[4], or -1 when script[4] is 0xFF.
pub fn vs_event_scriptSetActorIdleAnimation(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const actor: c_int = c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    if (script[4] == 0xFF) {
        _ = c.vs_actors_setActorTravelAnimation(actor, -1);
    } else {
        // D_800E9B58 is indexed unchecked by script[3], as in the C.
        _ = c.vs_actors_setActorTravelAnimation(actor, @as(c_int, psx.at(v.g.D_800E9B58(), script[3]).*) + script[4]);
    }
    return 0;
}
