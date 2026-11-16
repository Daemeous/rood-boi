//! Battle helper: passes a three-byte record and its slot to vs_actors_translateActorToTileAtRate, then marks the slot in vs_event_scriptControlledActors (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSlideActorToTile (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes a three-byte record and its slot to vs_actors_translateActorToTileAtRate, then marks the slot in vs_event_scriptControlledActors.
pub fn vs_event_scriptSlideActorToTile(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    // The fourth byte is 0xFF; the record's last byte is uninitialised in the original.
    var sp10: [16]u8 = undefined;
    const short_arg: c_ushort = psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 1));
    const temp_s0: c_ushort = psx.conv(c_ushort, c.vs_event_resolveScriptActorReference(short_arg));
    sp10[0] = arg0[3];
    sp10[1] = arg0[4];
    sp10[2] = arg0[5];
    sp10[3] = 0xFF;
    if (v.g.vs_event_skipState().* == 2) {
        c.vs_actors_translateActorToTileAtRate(temp_s0, @ptrCast(&sp10), 0);
    } else {
        const third: c_int = if (arg0[6] != 0xFF) @as(c_int, arg0[6]) else -1;
        c.vs_actors_translateActorToTileAtRate(temp_s0, @ptrCast(&sp10), third);
    }
    c.vs_actors_clearActorMovementModes(temp_s0);
    v.g.vs_event_scriptControlledActors()[temp_s0] = 1;
    return 0;
}
