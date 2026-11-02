//! zig_diff spec: func_800B77DC -> func_800B77DC.func_800B77DC (4A0A8.c).
//! Inputs: a 32-byte arena buffer of random bytes (the C reads bytes 1..6) and
//! a random short (the C ignores it). prepare picks what func_800BFE50
//! returns: 0x2001 (the C passes -1 on), a near miss (0x12001, 0x2002, 0x2000,
//! 0xFFFF2001: the compare's width and value edges) or any 32-bit value; the
//! classes are those three. func_8009F990 copies its pointer argument's four
//! bytes and its int argument into an arena sink, which the harness compares.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B77DC));

pub const classes = [_][:0]const u8{ "sentinel", "near-miss", "any" };

var sink: [*]u8 = undefined;
/// The class prepare chose (func_800BFE50's result).
var kind: usize = 0;

pub const stubs = struct {
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return switch (kind) {
            0 => 0x2001,
            1 => ctx.edge(c_int, &.{ 0x12001, 0x2002, 0x2000, @bitCast(@as(c_uint, 0xFFFF2001)) }),
            else => ctx.value(c_int),
        };
    }

    pub fn func_8009F990(ctx: *zd.Ctx, k: anytype, p: anytype) void {
        _ = ctx;
        const bytes: [*]const u8 = @ptrCast(p);
        @memcpy(sink[0..4], bytes[0..4]);
        std.mem.writeInt(i32, sink[4..8], @as(i32, k), .little);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    sink = ctx.allocBytes(8, 4);
    kind = ctx.random.uintLessThan(usize, 3);
    const buf = ctx.allocBytes(32, 1);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return kind;
}
