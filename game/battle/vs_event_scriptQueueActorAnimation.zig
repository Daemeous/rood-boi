//! Battle script opcode helper: calls vs_actors_queueActorAnimation with a function of the script's short and a table-based value (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptQueueActorAnimation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_queueActorAnimation with the short at arg0+1 (through vs_event_resolveScriptActorReference), a table value plus arg0[3], and arg0[5]'s low six bits.
pub fn vs_event_scriptQueueActorAnimation(arg0: [*c]u8) callconv(.c) c_int {
    // Argument evaluation order: vs_battle_getShort, then vs_event_resolveScriptActorReference, then the table read and the rest.
    _ = c.vs_actors_queueActorAnimation(
        c.vs_event_resolveScriptActorReference(psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 1))),
        @as(c_int, v.g.D_800E9B58()[@as(usize, arg0[5] >> 6)]) +% @as(c_int, arg0[3]),
        @as(c_int, arg0[5] & 0x3F),
        0,
    );
    return 0;
}
