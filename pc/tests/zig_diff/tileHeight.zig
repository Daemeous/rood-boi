//! zig_diff spec: func_800E71DC -> pc.tile_height.tileHeight (D2: pc/src/navigation.c).
//! Inputs: the tile (x, z), edges or any int (so `* 128` and the sample offsets
//! wrap). func_8008DC7C is the only callee and is called 17 times: once for the
//! middle sample, then for samples 0-15 in order; its stub draws a height and
//! sets its sign (a blocked sample) from a 16-bit pattern prepare chose, and
//! half the time narrows the height to a few values so equal 15-bit parts
//! (with different high flags) meet. The patterns: none blocked (the first
//! return), a free 2x2 block (any of the squares 0-9, including the skipped 3
//! and 7, whose blocks are not looked at), and random ones (the centre
//! height, mostly).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").tileHeight));

pub const classes = [_][:0]const u8{ "none-blocked", "square-open", "centre" };

/// Bit i set: sample i (0-15) is returned negative.
var pattern: u16 = 0;
var narrow: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    switch (ctx.range(u8, 0, 3)) {
        0 => pattern = 0,
        1 => {
            // Everything blocked except one 2x2 block of squares at j (j = 3 or 7 included).
            const j = ctx.range(u5, 0, 9);
            pattern = ~@as(u16, @truncate(@as(u32, 0x33) << j));
        },
        else => pattern = ctx.int(u16),
    }
    narrow = ctx.chance(2);
    const x = ctx.pick(c_int, &.{ 0, 1, -1, 31, 32, 0x7FFFFFFF, -0x80000000, 0x1000000 });
    const z = ctx.pick(c_int, &.{ 0, 1, -1, 31, 32, 0x7FFFFFFF, -0x80000000, 0x1000000 });
    return .{ x, z };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (pattern == 0) return 0;
    const open: u32 = ~@as(u32, pattern) & 0xFFFF;
    var j: u5 = 0;
    while (j < 10) : (j += 1) {
        if (j == 3 or j == 7) continue;
        const square = @as(u32, 0x33) << j;
        if (open & square == square) return 1;
    }
    return 2;
}

pub const stubs = struct {
    pub fn func_8008DC7C(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        var height = ctx.value(c_int);
        if (narrow) height &= 0x1C003; // bits 0, 1 and 14 (the 15-bit part) and the flags 15, 16
        // Call 0 is the middle sample (never part of the blocked set); calls 1-16 are samples 0-15.
        const k = ctx.calls;
        const blocked = k >= 1 and k <= 16 and (pattern >> @as(u4, @intCast(k - 1))) & 1 != 0;
        const bits: u32 = @bitCast(height);
        return @bitCast(if (blocked) bits | 0x80000000 else bits & 0x7FFFFFFF);
    }
};
