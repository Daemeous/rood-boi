//! An event script opcode that turns an actor to, or by, a rotation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptRotateActorAxes (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Hands the actor of getShort(script + 1) the vector (getShort(+4), getShort(+6), getShort(+8))
/// and script[10] (0xFF as -1): vs_actors_rotateActorByAtRate when script[3] is set, else vs_actors_rotateActorToAtRate; then its
/// vs_event_scriptControlledActors mark.
pub fn vs_event_scriptRotateActorAxes(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var vec: v.SVECTOR = undefined;
    const actor: u16 = psx.conv(u16, c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    var third: i16 = -1;
    if (script[10] != 0xFF) third = script[10];
    vec.vx = c.vs_battle_getShort(script + 4);
    vec.vy = c.vs_battle_getShort(script + 6);
    vec.vz = c.vs_battle_getShort(script + 8);
    // pad is uninitialised in the original; neither callee reads it: 0 here (PORTING.md §11.4).
    vec.pad = 0;
    if (script[3] != 0) {
        c.vs_actors_rotateActorByAtRate(actor, psx.cptr(c.SVECTOR, &vec), third);
    } else {
        c.vs_actors_rotateActorToAtRate(actor, psx.cptr(c.SVECTOR, &vec), third);
    }
    v.g.vs_event_scriptControlledActors()[actor] = 1;
    return 0;
}
