//! Turns a battle actor toward a target angle by the shorter way round (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA850 (src/BATTLE/BATTLE.PRG/40564.c)
/// Takes the signed difference from the actor's facing to `angle` (the shorter way round) and passes it to func_800AA984.
pub fn func_800AA850(slot: c_int, angle: c_ushort, arg2: c_int) callconv(.c) void {
    // D_800F4538[slot]: the C does not check the slot against 17, so neither do we.
    const actor = psx.at(v.g.D_800F4538(), slot).* orelse return;

    const temp_a1: i16 = psx.conv(i16, @as(c_int, angle) -% @as(c_int, actor.unk0.facing));
    var var_v1: i16 = temp_a1;

    if (@as(c_int, temp_a1) > 4096 / 2) {
        var_v1 = psx.conv(i16, @as(c_int, temp_a1) -% 4096);
    }

    if (@as(c_int, var_v1) < -4096 / 2) {
        var_v1 = psx.conv(i16, @as(c_int, var_v1) +% 4096);
    }

    c.func_800AA984(slot, var_v1, arg2);
}
