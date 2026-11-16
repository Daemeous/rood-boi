//! An event script opcode that hands an actor four bytes of the script and two optional values (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptWalkActorToTile (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_moveActorToTileAtRate for the actor of getShort(script + 1) with script[3..6], and 0, 0 while
/// vs_event_skipState is 2, else script[7] and script[8] (0xFF as -1); then vs_actors_clearActorMovementModes and its vs_event_scriptControlledActors mark.
pub fn vs_event_scriptWalkActorToTile(script: [*c]u8, _: c_short) callconv(.c) c_int {
    // The C's buffer is 10 bytes; only the first 4 are set and read.
    var bytes: [10]u8 = undefined;
    const actor: u16 = psx.conv(u16, c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    bytes[0] = script[3];
    bytes[1] = script[4];
    bytes[2] = script[5];
    bytes[3] = script[6];
    const fields: ?*c.func_8006EBF8_t_fields = @ptrCast(&bytes);
    if (v.g.vs_event_skipState().* == 2) {
        c.vs_actors_moveActorToTileAtRate(actor, fields, 0, 0);
    } else {
        const a: c_int = if (script[7] != 0xFF) script[7] else -1;
        const b: c_int = if (script[8] != 0xFF) script[8] else -1;
        c.vs_actors_moveActorToTileAtRate(actor, fields, a, b);
    }
    c.vs_actors_clearActorMovementModes(actor);
    v.g.vs_event_scriptControlledActors()[actor] = 1;
    return 0;
}
