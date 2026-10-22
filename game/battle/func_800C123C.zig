//! Tests a point against an actor's circle in the actor's frame (src/BATTLE/BATTLE.PRG/58578.c).
const psx = @import("common").psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C123C (src/BATTLE/BATTLE.PRG/58578.c)
/// Returns 1 when the point arg1 (less the actor's offset, raised by arg2 when nonzero) lies inside the actor's circle, else 0.
pub fn func_800C123C(arg0: ?*v.func_800C1564_t, arg1: [*c]c_ushort, arg2: c_int) callconv(.c) c_int {
    var work: v.func_800C0FA8_t2 = undefined;
    var matrix: v.MATRIX = undefined;

    _ = c.func_800C0FA8(@ptrCast(arg0), @ptrCast(&work), @ptrCast(&matrix));

    for (0..3) |i| {
        const diff: c_int = @as(c_int, arg1[i]) -% @as(c_int, psx.conv(u16, work.unk10[i]));
        const dst: *i16 = switch (i) {
            0 => &work.unk0.vx,
            1 => &work.unk0.vy,
            else => &work.unk0.vz,
        };
        dst.* = psx.conv(i16, diff);
    }

    _ = c.ApplyMatrixSV(@ptrCast(&matrix), @ptrCast(&work.unk0), @ptrCast(&work.unk8));

    const flags: *v.func_800C1564_flags = &arg0.?.unk4.flags;
    const unk1: c_int = @as(c_int, psx.getBits(flags, "unk1"));

    if (arg2 != 0) {
        work.unk8.vy = psx.conv(i16, @as(c_int, work.unk8.vy) -% (unk1 << 5));
    }

    const negVy: c_int = @as(c_int, psx.conv(u16, -%@as(c_int, work.unk8.vy)));
    if (!(negVy > (unk1 << 5))) {
        const num: c_int = -%((@as(c_int, work.unk8.vy) << 16) >> 13);
        const divisor: c_int = @divTrunc(num, unk1);
        const temp1: c_int = (@as(c_int, work.unk8.vx) *% @as(c_int, work.unk18[0])) >> 12;
        const temp2: c_int = (@as(c_int, work.unk8.vz) *% @as(c_int, work.unk18[2])) >> 12;
        const radius: c_int = 0x100 -% divisor;

        if ((temp1 *% temp1 +% temp2 *% temp2) < (radius *% radius)) {
            return 1;
        }
    }
    return 0;
}
