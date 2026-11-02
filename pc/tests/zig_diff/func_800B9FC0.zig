//! zig_diff spec: func_800B9FC0 -> func_800B9FC0.func_800B9FC0 (4A0A8.c).
//! Inputs: a 5-byte-plus arena block whose bytes 1-3 are random and whose byte 4
//! is zero about a third of the time (and 0x80, 0xFF, 1, 0x7F otherwise), so both
//! sides of the flag test and its sign bit occur. D_800F4B88.unk4 is chosen so that
//! delta = arg0[2]*16 - unk4 lands exactly on -2048 and 2048 (the ordered
//! comparisons' equal sides), beyond them (the wrap branches), or anywhere
//! (any i16). The three callees are void or int recording stubs: none of them
//! changes a field the C reads back, so the default stubs model them.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9FC0));

pub const classes = [_][:0]const u8{
    "flag-zero",
    "delta-equal-low",
    "delta-equal-high",
    "delta-wrap-low",
    "delta-wrap-high",
    "delta-mid",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*]u8 = ctx.allocBytes(8, 1);
    p[1] = ctx.int(u8);
    p[2] = ctx.int(u8);
    p[3] = ctx.int(u8);
    p[4] = ctx.edge(u8, &.{ 0, 0, 1, 0x7f, 0x80, 0xff });

    const g = v.g.D_800F4B88();
    const x: i32 = switch (ctx.range(u8, 0, 4)) {
        0 => @as(i32, p[2]) * 16 - 2048, // delta == 2048
        1 => @as(i32, p[2]) * 16 + 2048, // delta == -2048
        2 => @as(i32, ctx.int(i16)), // any i16
        3 => @as(i32, p[2]) * 16 - @as(i32, ctx.range(u16, 2049, 6000)), // delta > 2048
        else => @as(i32, p[2]) * 16 + @as(i32, ctx.range(u16, 2049, 6000)), // delta < -2048
    };
    g.unk4 = @intCast(x);

    const arg0: [*c]u8 = p;
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    if (p[4] == 0) return 0;
    const delta: i32 = @as(i32, p[2]) * 16 - @as(i32, v.g.D_800F4B88().unk4);
    if (delta == -2048) return 1;
    if (delta == 2048) return 2;
    if (delta < -2048) return 3;
    if (delta > 2048) return 4;
    return 5;
}
