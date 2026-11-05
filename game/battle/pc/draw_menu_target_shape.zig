//! A battle menu's target shape (D2: 58350.s; oracle pc/src/battle_gte.c).
const c = @import("c_battle_gte");
const v = @import("views_battle_gte");
const psx = @import("common").psx;

fn shortAt(base: [*]u8, offset: isize) c_int {
    return psx.byteAt(i16, base, offset).*;
}

/// orig: func_800C0B50 (pc/src/battle_gte.c)
/// Draws the shape `shape` of the battle menu state block (D_800EB9B8): its
/// 0x32-style point list at +0x48 is turned by the shape's scale and rotation
/// (composed at scratchpad 0x398) and projected by the GTE; each point with a
/// nonzero pad draws a segment from the previous point (func_800C0990) with
/// an intensity from the pair's depth. Scratchpad 0x398.. holds the current
/// and previous projected point (SXY, depth / 4).
pub fn drawMenuTargetShape(shape: [*]u8, color_in: c_uint) callconv(.c) void {
    const buffer: [*]u8 = @ptrFromInt(@as(usize, v.g.D_800EB9B8().*));
    var point: usize = @intFromPtr(buffer) +% 0x48;
    const scratch: [*]u32 = @ptrFromInt(0x1F800398);
    const halves: [*]u16 = @ptrFromInt(0x1F800398);
    const matrix: [*c]c.MATRIX = @ptrFromInt(0x1F800398);
    const scale = c.func_800C085C(shape + 4, shortAt(shape, 2));
    _ = c.RotMatrixYXZ_gte(@ptrFromInt(@intFromPtr(buffer) +% 0x24), matrix);
    const factor = c.rcos(shortAt(buffer, 0x2a) << 6);
    var scaling = c.VECTOR{ .vx = factor, .vy = factor, .vz = factor, .pad = 0 };
    _ = c.ScaleMatrix(matrix, &scaling);
    _ = c.func_80041C68(scale, matrix);
    c.func_800C02A8();
    var i: usize = 0;
    while (i < 5) : (i += 1) c.CTC2(scratch[i], @intCast(8 + i));
    c.CTC2(@bitCast(shortAt(shape, 8)), 13);
    c.CTC2(@bitCast(shortAt(shape, 0xa)), 14);
    c.CTC2(@bitCast(shortAt(shape, 0xc)), 15);
    const color = color_in | 0x44000000;
    var count: c_uint = psx.byteAt(u16, buffer, 0x32).*;
    const divisor: c_int = if (shape[6] < shape[4]) shape[4] else shape[6];
    const bias: c_int = shortAt(buffer, 0x20) << 1;
    while (count != 0) {
        count -%= 1;
        c.MTC2(psx.byteAt(c_uint, @as([*]u8, @ptrFromInt(point)), 0).*, 0);
        c.MTC2(psx.byteAt(c_uint, @as([*]u8, @ptrFromInt(point)), 4).*, 1);
        _ = c.doCOP2(0x04A2012); // gte_mvmva(1, 1, 0, 1, 0)
        const x = c.MFC2(9);
        const y = c.MFC2(10);
        const z = c.MFC2(11);
        c.MTC2((x & 0xffff) | (y << 16), 0);
        c.MTC2(z, 1);
        const pad: c_int = psx.byteAt(i16, @as([*]u8, @ptrFromInt(point)), 6).*;
        point +%= 8;
        _ = c.doCOP2(0x0180001); // gte_rtps
        scratch[0] = c.MFC2(14);
        scratch[1] = @bitCast(@as(c_int, @bitCast(c.MFC2(19))) >> 2);
        if (pad != 0) {
            const sum: c_int = @as(c_int, @as(i16, @bitCast(halves[2]))) +% @as(c_int, @as(i16, @bitCast(halves[6]))) -% bias;
            // `div $zero` has no zero check: the R3000 then leaves LO = -1 for a non-negative dividend and 1 for a negative one.
            const depth: c_int = if (divisor != 0) @divTrunc(sum, divisor) else if (sum < 0) 1 else -1;
            c.func_800C0990(@ptrFromInt(0x1F800398), @ptrFromInt(0x1F8003A0), color -% (@as(c_uint, @bitCast(pad)) << 25), 0x20 -% depth);
        }
        var j: usize = 0;
        while (j < 3) : (j += 1) halves[4 + j] = halves[j];
    }
}
