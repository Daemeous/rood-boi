//! Battle script record: resolves a 16-bit index and stores three record bytes
//! for it (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptFaceTile (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Resolves the index at arg0[1..2], passes it with the record bytes at arg0[3..5] and arg0[6] to vs_actors_turnActorTowardTileAtRate, and marks the index in vs_event_scriptControlledActors.
pub fn vs_event_scriptFaceTile(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // vs_battle_getShort returns a short; vs_event_resolveScriptActorReference takes a u_short (C converts implicitly).
    const index: u16 = psx.conv(u16, c.vs_battle_getShort(arg0 + 1));
    const temp_s0: c_int = c.vs_event_resolveScriptActorReference(index) & 0xFFFF;

    var sp10 = std.mem.zeroes(v.func_8006EBF8_t_fields);
    psx.setBits(&sp10, "unk0_0", arg0[3]);
    psx.setBits(&sp10, "unk0_8", arg0[4]);
    psx.setBits(&sp10, "unk0_16", arg0[5]);

    const flag: c_int = if (arg0[6] != 0xFF) @as(c_int, arg0[6]) else -1;
    c.vs_actors_turnActorTowardTileAtRate(temp_s0, @ptrCast(&sp10), flag);
    v.g.vs_event_scriptControlledActors()[@intCast(temp_s0)] = 1;
    return 0;
}
