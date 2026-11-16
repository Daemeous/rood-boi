//! An event script opcode that sets the flags of each actor of a script reference (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetActorGroupFlags (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// For each actor of the reference getShort(script + 1): vs_actors_setActorDetached with script[4] bit 5, then
/// its vs_actors_actorStateFlags flags rewritten: bits 0x800 and 0x1000 kept where script[4] bits 1 and 3 say
/// so, else taken from bits 0 and 2, ORed with script[3]; then vs_actors_resetActorWeaponVisibilityBlend.
pub fn vs_event_scriptSetActorGroupFlags(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var set: v.func_800BB68C_t = undefined;
    c.vs_event_buildScriptActorSelection(psx.conv(u16, c.vs_battle_getShort(script + 1)), psx.cptr(c.func_800BB68C_t, &set));
    while (true) {
        const actor: c_int = c.vs_event_nextScriptSelectedActor(psx.cptr(c.func_800BB68C_t, &set));
        if (actor == 1) break;
        const flags: c_uint = c.vs_actors_actorStateFlags(actor);
        var bit11: c_uint = flags & 0x800;
        var bit12: c_uint = flags & 0x1000;
        const mode: c_uint = script[4];
        if (mode & 2 == 0) bit11 = (mode & 1) << 0xB;
        if (mode & 8 == 0) bit12 = (mode << 0xA) & 0x1000;
        c.vs_actors_setActorDetached(actor, psx.conv(c_int, (mode >> 5) & 1));
        c.vs_actors_setActorDisplayFlags(actor, psx.conv(c_int, bit11 | bit12 | script[3]));
        c.vs_actors_resetActorWeaponVisibilityBlend(actor);
    }
    return 0;
}
