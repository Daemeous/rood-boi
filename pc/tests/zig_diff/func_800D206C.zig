//! zig_diff spec: func_800D206C -> func_800D206C.func_800D206C (5BF94.c).
//! No arguments: the active mask D_800F5518 and the four shakes keep the
//! harness's random bytes; func_800D11F4 returns any int (negative: the
//! shake stops), rsin, func_800D1E2C and func_800D1E78 any values (their
//! order is compared).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D206C));

pub const classes = [_][:0]const u8{"step"};

pub const stubs = struct {
    pub fn func_800D11F4(ctx: *zd.Ctx, curve: anytype, frame: anytype) c_int {
        _ = curve;
        _ = frame;
        return switch (ctx.value(u8) % 4) {
            0 => 0,
            1 => -1,
            else => ctx.value(c_int),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
