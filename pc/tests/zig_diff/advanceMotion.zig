//! zig_diff spec: func_800B668C -> pc.turn_request.advanceMotion (D2: pc/src/battle_motion.c).
//! Inputs: an actor id (any int, never dereferenced), a short a (edges or any
//! i16, which is sign-extended to the int the callees take) and b (edges or any
//! int). The stubs of func_800AA8D0 and func_800A249C record their arguments,
//! and so the order of the two calls.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").advanceMotion));

pub const classes = [_][:0]const u8{ "non-negative", "negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.pick(c_int, &.{ 0, 1, 16, -1 });
    const a = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    const b = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 });
    return .{ id, a, b };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[1] < 0);
}

pub const stubs = struct {
    pub fn func_800AA8D0(ctx: *zd.Ctx, id: anytype, a: anytype, b: anytype) void {
        _ = ctx;
        _ = id;
        _ = a;
        _ = b;
    }

    pub fn func_800A249C(ctx: *zd.Ctx, id: anytype, a: anytype) void {
        _ = ctx;
        _ = id;
        _ = a;
    }
};
