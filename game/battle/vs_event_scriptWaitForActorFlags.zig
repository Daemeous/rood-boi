//! An event script test of an actor's flags (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptWaitForActorFlags (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Whether the actor of getShort(script + 1) has any flag of the mask getShort(script + 3):
/// its vs_actors_actorStateFlags flags with bits 0 and 7 inverted, bit 0 also set (when the mask asks
/// for it) where vs_actors_isScriptedActorPathFinished says 0.
pub fn vs_event_scriptWaitForActorFlags(script: [*c]u8) callconv(.c) c_int {
    const actor: c_int = c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))) & 0xFFFF;
    var flags: u16 = psx.conv(u16, c.vs_actors_actorStateFlags(actor) ^ 0x81);
    const mask: i16 = c.vs_battle_getShort(script + 3);
    if (mask & 1 != 0) {
        if (c.vs_actors_isScriptedActorPathFinished(actor) == 0) flags |= 1;
    }
    const hit: u16 = psx.conv(u16, @as(c_int, flags) & mask);
    return @intFromBool(hit != 0);
}
