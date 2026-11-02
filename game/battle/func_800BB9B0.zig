//! Advances the three battle timing tracks and writes their interpolated values
//! into the matching entries of D_800EB9BC (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BB9B0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Steps each active track by half a tick, evaluates its curve and stores the result; a finished track is deactivated.
pub fn func_800BB9B0() callconv(.c) void {
    const tracks = v.g.D_800F4B30();
    // The C's var_s1 starts at the value of D_800EB9BC and steps by the struct size; 0 when it is null.
    const s1_start: usize = if (v.g.D_800EB9BC().*) |p| @intFromPtr(p) else 0;

    var i: c_int = 0;
    while (i < 3) : (i +%= 1) {
        const s0 = psx.at(tracks, i);
        const idx: usize = @intCast(i);
        const s1_addr: usize = s1_start +% idx *% @sizeOf(v.D_800EB9BC_t);

        if (s0.unk3 != 0) {
            s0.unk2 = psx.conv(u8, @as(c_int, s0.unk2) +% (v.g.vs_gametime_tickspeed().* >> 1));
            const temp_a0: i16 = psx.conv(i16, c.func_800BFEBC(
                @as(c_int, s0.unk1),
                @as(c_int, s0.unk2),
                @as(c_int, s0.unk3),
            ));
            const t: c_int = temp_a0;

            if (v.g.D_800EB9BC().* != null) {
                const s1: *align(1) v.D_800EB9BC_t = @ptrFromInt(s1_addr);
                s1.unk8 = psx.conv(i16, @as(c_int, s0.unk4) +% ((@as(c_int, s0.unk8) *% t) >> 12));
                s1.unkA = psx.conv(i16, @as(c_int, s0.unk6) +% ((@as(c_int, s0.unkA) *% t) >> 12));
                s1.unkC = psx.conv(i16, @as(c_int, s0.unkC) +% ((@as(c_int, s0.unkE) *% t) >> 12));
                psx.setBits(s1, "unk0_24", psx.conv(u8, @as(c_int, s0.unk10) +% ((@as(c_int, s0.unk12) *% t) >> 12)));
            }

            if (s0.unk2 >= s0.unk3) {
                s0.unk3 = 0;
            }
        }
    }
}
