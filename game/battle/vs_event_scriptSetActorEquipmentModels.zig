//! An event script opcode that sets or clears an actor's state (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetActorEquipmentModels (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// For the actor of getShort(script + 1): vs_actors_refreshActorEquipmentModels when script[3] is 0xFE, else
/// vs_actors_loadActorEquipmentModels with script[3] and script[5].
pub fn vs_event_scriptSetActorEquipmentModels(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const actor: u16 = psx.conv(u16, c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    if (script[3] == 0xFE) {
        c.vs_actors_refreshActorEquipmentModels(actor);
    } else {
        c.vs_actors_loadActorEquipmentModels(actor, script[3], script[5]);
    }
    return 0;
}
