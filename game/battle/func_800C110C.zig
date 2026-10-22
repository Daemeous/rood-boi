//! Projects a point offset from an actor's transform and tests its depth against the actor's range
//! (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C110C (src/BATTLE/BATTLE.PRG/58578.c)
/// Transforms the point's offset from the actor by its matrix and returns 1 when its depth is within the actor's range, else 0.
pub fn func_800C110C(actor: ?*v.func_800C1564_t, point: [*c]c_ushort, adjust: c_int) callconv(.c) c_int {
    var work: v.func_800C0FA8_t2 = undefined;
    var matrix: v.MATRIX = undefined;
    var sum: c_int = 0;
    var term: c_int = undefined;

    _ = c.func_800C0FA8(@ptrCast(actor), @ptrCast(&work), @ptrCast(&matrix));

    // Offsets of the three point components from unk10, written to unk0.vx/vy/vz (C's short stores truncate).
    work.unk0.vx = psx.conv(i16, @as(c_int, point[0]) -% @as(c_int, @as(u16, @bitCast(work.unk10[0]))));
    work.unk0.vy = psx.conv(i16, @as(c_int, point[1]) -% @as(c_int, @as(u16, @bitCast(work.unk10[1]))));
    work.unk0.vz = psx.conv(i16, @as(c_int, point[2]) -% @as(c_int, @as(u16, @bitCast(work.unk10[2]))));

    _ = c.ApplyMatrixSV(@ptrCast(&matrix), @ptrCast(&work.unk0), @ptrCast(&work.unk8));

    const flags = &actor.?.unk4.flags;

    if (adjust != 0) {
        work.unk8.vy = psx.conv(i16, @as(c_int, work.unk8.vy) -% (@as(c_int, psx.getBits(flags, "unk1")) << 4));
    }

    const limit: c_int = @as(c_int, psx.getBits(flags, "unk1")) << 5;
    if (!(@as(c_int, psx.conv(u16, -%@as(c_int, work.unk8.vy))) > limit)) {
        // Components unk8.vx and unk8.vz against unk18[0] and unk18[2]; the 4.12 products fit int, their squares wrap.
        term = (@as(c_int, work.unk8.vx) *% @as(c_int, work.unk18[0])) >> 12;
        sum +%= term *% term;
        term = (@as(c_int, work.unk8.vz) *% @as(c_int, work.unk18[2])) >> 12;
        sum +%= term *% term;

        // Unsigned comparison as in C: a negative (sum >> 16) compares as a large value.
        sum = @intFromBool(psx.conv(c_uint, sum >> 16) < 1);
    }

    return sum;
}
