//! Per-tick easing of a value pair toward its targets (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");

/// orig: func_800AEEC4 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Moves the eased pair toward its targets by a share of the tick speed and counts its timer down.
pub fn func_800AEEC4(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const s = arg0.?;
    const temp_state: c_int = s.unk17E4.unk0;
    if (temp_state != 1 or s.unk17E4.unk3 == 0) return;

    var var_a2: c_int = @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
    if (@as(c_int, s.unk17E4.unk3) < var_a2) {
        var_a2 = @as(c_int, s.unk17E4.unk3);
    }

    var temp_v1: c_int = @as(c_int, s.unk17E8.unk2) -% @as(c_int, s.unk17E4.unk2);
    if (temp_v1 != 0) {
        temp_v1 *%= var_a2;
        const v0: c_int = @divTrunc(temp_v1, @as(c_int, s.unk17E4.unk3));
        s.unk17E4.unk2 = psx.conv(u8, @as(c_int, s.unk17E4.unk2) +% v0);
    }

    temp_v1 = @as(c_int, s.unk17E8.unk1) -% @as(c_int, s.unk17E4.unk1);
    if (temp_v1 != 0) {
        temp_v1 *%= var_a2;
        const v0: c_int = @divTrunc(temp_v1, @as(c_int, s.unk17E4.unk3));
        s.unk17E4.unk1 = psx.conv(u8, @as(c_int, s.unk17E4.unk1) +% v0);
    }

    s.unk17E4.unk3 = psx.conv(u8, @as(c_int, s.unk17E4.unk3) -% var_a2);
}
