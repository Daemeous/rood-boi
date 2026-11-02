//! zig_diff spec: func_800BB30C -> func_800BB30C.func_800BB30C (4A0A8.c).
//! Inputs: arg0 is null or an arena byte the C never reads, arg1 any short
//! (also unread), and the global D_800F4C2C is drawn from 0, 1, 2, 3, 255 or
//! a random byte, so the `== 2` test goes both ways. func_800CB7DC is a void
//! stub that sometimes rewrites D_800F4C2C (to 2, 1 or a random byte), so a
//! port that reads the global before the call differs from the C.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB30C));

pub const classes = [_][:0]const u8{ "eq-two", "not-two" };

pub const stubs = struct {
    pub fn func_800CB7DC(ctx: *zd.Ctx) void {
        const g = psx.global(u8, "D_800F4C2C");
        switch (ctx.value(u2)) {
            0 => g.* = 2,
            1 => g.* = 1,
            else => g.* = ctx.value(u8),
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    psx.global(u8, "D_800F4C2C").* = ctx.pick(u8, &.{ 0, 1, 2, 3, 255 });
    const arg0: [*c]u8 = if (ctx.chance(4)) null else ctx.allocBytes(1, 1);
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (psx.global(u8, "D_800F4C2C").* == 2) 0 else 1;
}
