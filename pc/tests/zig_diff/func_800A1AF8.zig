//! zig_diff spec: func_800A1AF8 -> func_800A1AF8.func_800A1AF8 (38C1C.c).
//! The C is a wrapper: it calls func_800A1C10 with its four arguments and
//! returns that result when it is negative, 0 otherwise. The inputs cover
//! random and edge arguments, with the buffer pointer null or pointing into
//! the arena. The external stub returns edge values (0, -1, INT_MIN, INT_MAX)
//! and random bit patterns, so every sign of the result, zero included, is
//! seen.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1AF8));

pub const classes = [_][:0]const u8{ "null-buf", "buf-neg-arg3", "buf-pos-arg3" };

pub const stubs = struct {
    pub fn func_800A1C10(ctx: *zd.Ctx, arg0: c_int, arg1: c_int, arg2: anytype, arg3: c_int) c_int {
        _ = arg0;
        _ = arg1;
        _ = arg2;
        _ = arg3;
        const z = ctx.value(u32);
        return switch (z % 6) {
            0 => 0,
            1 => -1,
            2 => std.math.minInt(c_int),
            3 => std.math.maxInt(c_int),
            else => @bitCast(z),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]c_ushort = if (ctx.chance(4)) null else @ptrCast(@alignCast(ctx.allocBytes(32, 2)));
    const edges = [_]c_int{ 0, -1, 1, std.math.minInt(c_int), std.math.maxInt(c_int) };
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges), buf, ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    if (args[2] == null) return 0;
    return if (args[3] < 0) 1 else 2;
}
