//! zig_diff spec: func_800B8708 -> func_800B8708.func_800B8708 (4A0A8.c).
//! Inputs: a 4-byte buffer as arg0 whose byte 1 is a random byte or one of
//! the edges (bit 2 set or clear, sign bit with and without bit 2, zero,
//! 0xFF, low bits 0..3), an arbitrary arg1 (the C never reads it), and the
//! flag D_800F4C2C at 2 or at other values. The two callees are recording
//! stubs that write the flag after the call, sometimes to 2, so the return
//! value is decided by the state the C sees after the call, not before it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8708));

pub const classes = [_][:0]const u8{ "clear-flag-other", "clear-flag2", "set-flag-other", "set-flag2" };

pub const stubs = struct {
    pub fn func_8007CBBC(ctx: *zd.Ctx, x: anytype) void {
        _ = x;
        touchFlag(ctx);
    }
    pub fn func_8007CC4C(ctx: *zd.Ctx, x: anytype) void {
        _ = x;
        touchFlag(ctx);
    }
};

/// The callees may change the flag the C reads back: sometimes to 2, else any byte.
fn touchFlag(ctx: *zd.Ctx) void {
    const flag = v.g.D_800F4C2C();
    if (ctx.value(u2) == 0) {
        flag.* = 2;
    } else {
        flag.* = ctx.value(u8);
    }
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[0] = ctx.int(u8);
    buf[1] = if (ctx.chance(2))
        ctx.pick(u8, &.{ 0x04, 0x84, 0x80, 0x00, 0xFF, 0x7B, 0x08, 0x07 })
    else
        ctx.int(u8);
    buf[2] = ctx.int(u8);
    buf[3] = ctx.int(u8);
    v.g.D_800F4C2C().* = if (ctx.chance(2)) 2 else ctx.pick(u8, &.{ 0, 1, 3, 255 });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const set = (args[0][1] & 4) != 0;
    const f2 = v.g.D_800F4C2C().* == 2;
    return (@as(usize, @intFromBool(set)) * 2) + @as(usize, @intFromBool(f2));
}
