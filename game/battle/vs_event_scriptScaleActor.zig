//! Builds a vector from three shorts in a script and passes it, with a target
//! looked up from a short, to a helper (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptScaleActor (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads a vector and a target id from the script at `arg0` and passes them, with the byte at offset 9, to `vs_actors_scaleActorToOverTime`.
pub fn vs_event_scriptScaleActor(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var sp10: v.SVECTOR = undefined;

    sp10.vx = c.vs_battle_getShort(arg0 + 3);
    sp10.vy = c.vs_battle_getShort(arg0 + 5);
    sp10.vz = c.vs_battle_getShort(arg0 + 7);
    c.vs_actors_scaleActorToOverTime(c.vs_event_resolveScriptActorReference(psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 1))), psx.cptr(c.SVECTOR, &sp10), arg0[9]);
    return 0;
}
