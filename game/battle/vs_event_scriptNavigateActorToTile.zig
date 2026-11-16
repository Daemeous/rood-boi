//! An event script opcode that places an actor on a map cell (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptNavigateActorToTile (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Puts the script's actor at the centre of cell (script[3], script[5]): vs_actors_translateActorToAtRate with a
/// 0x8000 heading while vs_event_skipState is 2, else func_8007CFF8; then vs_actors_clearActorMovementModes and its vs_event_scriptControlledActors mark.
pub fn vs_event_scriptNavigateActorToTile(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var pos: v.SVECTOR = undefined;
    const actor: u16 = psx.conv(u16, c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    pos.vx = psx.conv(i16, (@as(c_int, script[3]) << 7) +% 0x40);
    pos.vy = 0;
    pos.vz = psx.conv(i16, (@as(c_int, script[5]) << 7) +% 0x40);
    if (v.g.vs_event_skipState().* == 2) {
        pos.pad = -0x8000;
        c.vs_actors_translateActorToAtRate(actor, psx.cptr(c.SVECTOR, &pos), 0);
    } else {
        // pad stays unset as in the C; func_8007CFF8 reads only vx and vz.
        pos.pad = 0;
        c.func_8007CFF8(actor, psx.cptr(c.SVECTOR, &pos));
    }
    c.vs_actors_clearActorMovementModes(actor);
    v.g.vs_event_scriptControlledActors()[actor] = 1;
    return 0;
}
