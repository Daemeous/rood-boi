//! An event script opcode that hands an actor four bytes of the script (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetActorSoundMix (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_setActorSoundOverrides with the actor of getShort(script + 1) (0x2001 becomes -1) and
/// script[3..6] as a D_800F4538_unk64.
pub fn vs_event_scriptSetActorSoundMix(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var bytes: v.D_800F4538_unk64 = undefined;
    bytes.unk0 = script[3];
    bytes.unk1 = script[4];
    bytes.unk2 = script[5];
    bytes.unk3 = script[6];
    var actor: c_int = c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    if (actor == 0x2001) actor = -1;
    c.vs_actors_setActorSoundOverrides(actor, psx.cptr(c.D_800F4538_unk64, &bytes));
    return 0;
}
