//! Copies the four flag bytes at +5..+8 of a script operand and passes them,
//! with two converted operands, to vs_actors_queueScriptedActorSpawn (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSpawnActor (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_queueScriptedActorSpawn with the operand's id (through vs_event_resolveScriptActorReference), its short, and a copy of its flag bytes.
pub fn vs_event_scriptSpawnActor(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // The C leaves sp10[4..11] unset; only the first four bytes are read by the callee.
    var sp10: [12]u8 = undefined;

    sp10[0] = arg0[5];
    sp10[1] = arg0[6];
    sp10[2] = arg0[7];
    sp10[3] = arg0[8];

    // Operand order as taken for the second argument first (GCC's order for this call).
    const second: c_short = c.vs_battle_getShort(arg0 + 3);
    const first: c_short = c.vs_battle_getShort(arg0 + 1);
    // vs_event_resolveScriptActorReference takes a u_short: the signed short from the operand is converted as C does.
    const id = c.vs_event_resolveScriptActorReference(psx.conv(u16, first));
    _ = c.vs_actors_queueScriptedActorSpawn(psx.conv(c_uint, id), psx.conv(c_int, second), @ptrCast(&sp10));
    return 0;
}
