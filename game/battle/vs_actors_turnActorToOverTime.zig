//! Turns a battle actor toward a target angle by the shorter way round (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_turnActorToOverTime (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Takes the signed difference from the actor's facing to `angle` (the shorter way round) and passes it to vs_actors_setActorYawTurn.
pub fn vs_actors_turnActorToOverTime(slot: c_int, angle: c_ushort, arg2: c_int) callconv(.c) void {
    // vs_actors_actorRecords[slot]: the C does not check the slot against 17, so neither do we.
    const actor = psx.at(v.g.vs_actors_actorRecords(), slot).* orelse return;

    const temp_a1: i16 = psx.conv(i16, @as(c_int, angle) -% @as(c_int, actor.unk0.facing));
    var var_v1: i16 = temp_a1;

    if (@as(c_int, temp_a1) > 4096 / 2) {
        var_v1 = psx.conv(i16, @as(c_int, temp_a1) -% 4096);
    }

    if (@as(c_int, var_v1) < -4096 / 2) {
        var_v1 = psx.conv(i16, @as(c_int, var_v1) +% 4096);
    }

    c.vs_actors_setActorYawTurn(slot, var_v1, arg2);
}
