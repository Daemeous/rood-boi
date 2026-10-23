//! zig_diff spec: func_8009627C -> func_8009627C.func_8009627C (2842C.c).
//! Inputs: the flag D_800F2270 (zero or one of several nonzero values, with
//! and without the sign bit) and the 16-byte table D_800F2260 (all zero, or
//! random with zero entries and high-bit entries, so the signed and unsigned
//! tests differ). The stub func_80095C18 writes the next table entry and
//! sometimes the flag, so the C's later reads depend on the call and a port
//! that reads the table before the call is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009627C));

pub const classes = [_][:0]const u8{ "flag-set", "clear-all-zero", "clear-mixed" };

pub const stubs = struct {
    pub fn func_80095C18(ctx: *zd.Ctx, i: anytype, x: anytype) void {
        _ = x;
        const k: usize = @intCast(i);
        v.g.D_800F2260()[(k + 1) & 15] = ctx.value(u8);
        if (ctx.value(u2) == 0) v.g.D_800F2270().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const flag = v.g.D_800F2270();
    const tbl = v.g.D_800F2260();

    flag.* = if (ctx.chance(4)) 0 else ctx.pick(i16, &.{ 1, -1, 256, -32768 });

    if (ctx.chance(4)) {
        @memset(tbl[0..16], 0);
    } else {
        for (0..16) |i| {
            tbl[i] = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 127, 128, 255 });
        }
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F2270().* != 0) return 0;
    const tbl = v.g.D_800F2260();
    for (0..16) |i| {
        if (tbl[i] != 0) return 2;
    }
    return 1;
}
