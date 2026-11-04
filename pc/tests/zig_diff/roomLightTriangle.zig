//! zig_diff spec: func_80098C3C -> pc.room_light_triangle.roomLightTriangle (D2: pc/src/room_lighting.c).
//! The GTE calls are stubs, so the three areas (the triangle's, then the
//! point's against edges 4-3 and 3-2) are the values `prepare` plans for the
//! MFC2 stub: the triangle empty or flipped (area <= 0, zero included), the
//! first or second sub-area negative (or zero, which is inside), the third
//! area negative from the sub-areas exceeding the total (or exactly zero), a
//! point inside, and sums that overflow 32 bits so the blended value goes
//! negative as an int or above 255 (the clamps). The corner colours are
//! random words; the colour buffer is three bytes in a random-filled block.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").roomLightTriangle));

pub const classes = [_][:0]const u8{ "area-nonpositive", "first-negative", "second-negative", "third-negative", "inside", "clamp-signed", "clamp-high" };

var plan: [3]c_int = .{ 0, 0, 0 };
var last_calls: u32 = 0;
var seen: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const triangle: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    for (0..8) |i| triangle[i] = ctx.int(c_uint);
    const color = ctx.allocBytes(16, 16);
    for (color[0..16]) |*byte| byte.* = ctx.int(u8);
    const small = ctx.range(c_int, 1, 1000);
    switch (ctx.range(u8, 0, 7)) {
        0 => plan = .{ ctx.edge(c_int, &.{ 0, 0, -1, -0x80000000, -5 }), ctx.int(c_int), ctx.int(c_int) },
        1 => plan = .{ small, ctx.edge(c_int, &.{ -1, -small, -0x80000000, -7 }), ctx.int(c_int) },
        2 => plan = .{ small, ctx.range(c_int, 0, small), ctx.edge(c_int, &.{ -1, -small, -0x80000000 }) },
        3 => {
            const first = ctx.range(c_int, 0, small);
            plan = .{ small, first, small - first + ctx.edge(c_int, &.{ 1, 5, small }) };
        },
        4 => {
            const first = ctx.range(c_int, 0, small);
            plan = .{ small, first, ctx.range(c_int, 0, small - first) };
        },
        5 => plan = ctx.edge([3]c_int, &.{ .{ 5, 0, 0 }, .{ 5, 5, 0 }, .{ 5, 0, 5 }, .{ 5, 2, 3 }, .{ 7, 7, 0 } }),
        6 => plan = .{ 1, 1431655766, 1431655765 },
        else => plan = .{ 0x7FFFFFFD, 0x7FFFFFFF, 0x7FFFFFFF },
    }
    // White corners: an inside point blends to exactly 255 in every channel
    // (the clamp's boundary).
    if (ctx.chance(3)) for (5..8) |i| {
        triangle[i] = 0xFFFFFFFF;
    };
    return .{ ctx.int(c_uint), color, triangle };
}

fn blended(args: Args, channel: u5) u32 {
    const total: u32 = @bitCast(plan[0]);
    const a: u32 = @bitCast(plan[1]);
    const c: u32 = @bitCast(plan[2]);
    const b = total -% a -% c;
    const t = args[2];
    const shift = channel * 8;
    return (((t[5] >> shift) & 255) *% a +% ((t[7] >> shift) & 255) *% c +% ((t[6] >> shift) & 255) *% b) / total;
}

pub fn classify(args: Args) usize {
    if (plan[0] <= 0) return 0;
    if (plan[1] < 0) return 1;
    if (plan[2] < 0) return 2;
    if (@as(c_int, @bitCast(@as(u32, @bitCast(plan[0])) -% @as(u32, @bitCast(plan[1])) -% @as(u32, @bitCast(plan[2])))) < 0) return 3;
    var high = false;
    for (0..3) |channel| {
        const value = blended(args, @intCast(channel));
        if (@as(c_int, @bitCast(value)) < 0) return 5;
        if (value > 255) high = true;
    }
    return if (high) 6 else 4;
}

pub const stubs = struct {
    /// The nclip results, in call order: the triangle's area, then the point's two.
    pub fn MFC2(ctx: *zd.Ctx, reg: c_int) c_uint {
        _ = reg;
        if (ctx.calls <= last_calls) seen = 0;
        last_calls = ctx.calls;
        const value = plan[@min(seen, 2)];
        seen += 1;
        return @bitCast(value);
    }
};
