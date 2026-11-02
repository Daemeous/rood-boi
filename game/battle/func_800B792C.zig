//! Places a 16-bit vector (position or offset) for an actor, optionally
//! adding to a base vector, then runs the follow-up call (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B792C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Builds the vector from a script record, applies it through func_800A9988 and marks the slot used.
pub fn func_800B792C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var sp10: v.func_8006EBF8_t = undefined;

    const temp_s4: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(arg0 + 1))));
    const temp_s1: c_int = @as(c_int, c.vs_battle_getShort(arg0 + 3)) >> 1;
    const temp_s2: i16 = c.vs_battle_getShort(arg0 + 5);
    const temp_s3: i16 = c.vs_battle_getShort(arg0 + 7);

    const vec: *v.SVECTOR = &sp10.unk0.unk4;

    if ((arg0[3] & 1) != 0) {
        _ = c.func_800A1108(@as(c_int, temp_s4), &sp10);
        vec.vx = psx.conv(i16, @as(c_int, vec.vx) +% temp_s1);
        vec.vy = psx.conv(i16, @as(c_int, vec.vy) +% @as(c_int, temp_s2));
        vec.vz = psx.conv(i16, @as(c_int, vec.vz) +% @as(c_int, temp_s3));
    } else {
        vec.vx = psx.conv(i16, temp_s1);
        vec.vy = temp_s2;
        vec.vz = temp_s3;
    }

    if (arg0[9] != 0xFF) {
        vec.pad = psx.conv(i16, @as(c_int, arg0[9]) *% 0x10);
    } else {
        vec.pad = -0x8000;
    }

    if (v.g.D_800F4C2C().* == 2) {
        _ = c.func_800A9988(@as(c_int, temp_s4), @ptrCast(vec), 0, 0);
    } else {
        _ = c.func_800A9988(
            @as(c_int, temp_s4),
            @ptrCast(vec),
            if (arg0[10] != 0xFF) @as(c_int, arg0[10]) else -1,
            if (arg0[11] != 0xFF) @as(c_int, arg0[11]) else -1,
        );
    }

    _ = c.func_8009DC8C(@as(c_int, temp_s4));
    v.g.D_800F4B70()[@as(usize, temp_s4)] = 1;
    return 0;
}
