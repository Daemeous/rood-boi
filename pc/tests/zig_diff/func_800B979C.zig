//! zig_diff spec: func_800B979C -> func_800B979C.func_800B979C (4A0A8.c).
//! Inputs: the value vs_battle_getShort returns (set by prepare through the
//! spec var `pending`, so classify can see it) covers the sign bit both set
//! and clear, low bits zero and nonzero, and the 0x7FF mask edges. arg1 is
//! unused by the C but is still random. The callee stubs record their calls;
//! func_80098194 may rewrite D_800F4C6A, which the C does not re-read.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B979C));

pub const classes = [_][:0]const u8{ "sign-clear", "sign-set-low-zero", "sign-set-low-bits" };

/// The value vs_battle_getShort returns in this iteration.
pub var pending: c_short = 0;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        _ = p;
        return pending;
    }

    pub fn func_80098194(ctx: *zd.Ctx, n: anytype) void {
        _ = n;
        if (ctx.value(u1) == 1) v.g.D_800F4C6A().* = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    pending = ctx.pick(c_short, &.{ 0, -1, -2048, -32768, 32767, 0x7FF, 0x0800, -0x0800 });
    return .{
        ctx.allocBytes(16, 16),
        ctx.pick(c_short, &.{ 0, -1, 0x7FF, -32768 }),
    };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (pending >= 0) return 0;
    if (((@as(c_int, pending) & 0x7FF) == 0)) return 1;
    return 2;
}
