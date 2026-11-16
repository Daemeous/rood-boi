//! Battle script opcode: looks up a target and passes it, with three script bytes, to vs_actors_playActorAnimation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptPlayActorAnimation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Script opcode: passes a looked-up target and three bytes from the script on to vs_actors_playActorAnimation.
pub fn vs_event_scriptPlayActorAnimation(arg0: [*c]u8) callconv(.c) c_int {
    const b5: c_int = arg0[5];
    const table = v.g.D_800E9B58();
    _ = c.vs_actors_playActorAnimation(
        c.vs_event_resolveScriptActorReference(psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 1))),
        @as(c_int, table[@intCast(b5 >> 6)]) +% @as(c_int, arg0[3]),
        @as(c_int, arg0[4]),
        b5 & 0x3F,
    );
    return 0;
}
