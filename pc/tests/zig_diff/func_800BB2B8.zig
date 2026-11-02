//! zig_diff spec: func_800BB2B8 -> func_800BB2B8.func_800BB2B8 (4A0A8.c).
//! Inputs: a pointer into the arena at offsets 0-32 (the C only forms arg0 + 1
//! and hands it to vs_battle_getShort, which never dereferences it here), and
//! any short for arg1, which the C never reads. The unit has no branches and no
//! ordered comparisons, so the coverage is just the one path. vs_battle_getShort
//! returns any i16 (negative about half the time, so a signed/unsigned slip at
//! the call to pc_screff2_start shows), and pc_screff2_start is a recording stub
//! that the trace compares by its argument.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB2B8));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-nonnegative" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) i16 {
        _ = p;
        return ctx.value(i16);
    }

    pub fn pc_screff2_start(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        _ = x;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(64, 16);
    const off = ctx.range(usize, 0, 32);
    const p: [*c]u8 = @ptrCast(buf + off);
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 0) 0 else 1;
}
