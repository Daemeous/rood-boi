//! An event script opcode that points an actor at another actor or a target (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetActorAttachment (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// The actor of getShort(script + 1) and the target of getShort(script + 3), both resolved by
/// vs_event_resolveScriptActorReference and taken as 16 bits: func_8009FFA8 for a target at or above 0x2000, else
/// vs_actors_attachActorToParentBone with script[5] and script[6].
pub fn vs_event_scriptSetActorAttachment(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const actor: u16 = psx.conv(u16, c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    const target: c_uint = psx.conv(c_uint, c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 3)))) & 0xFFFF;
    if (target >= 0x2000) {
        _ = c.func_8009FFA8(actor, psx.conv(c_int, target));
    } else {
        c.vs_actors_attachActorToParentBone(actor, psx.conv(c_int, target), script[5], script[6]);
    }
    return 0;
}
