//! zig_diff spec: func_800BABCC -> func_800BABCC.func_800BABCC (4A0A8.c).
//! Inputs: a random 8-byte block whose byte 1 (the flag) is zero, has bit 7
//! set, or is nonzero without bit 7; the callee results (the getShort and
//! func_800BFE50 returns) pick edges 0, -1, 1, 32767 and -32768 from spec
//! vars set by prepare. Both flag branches are taken, and the call traces
//! carry the byte indices, the short-to-u16 conversion and the return value.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BABCC));

pub const classes = [_][:0]const u8{ "flag-zero", "flag-high-bit", "flag-low-bit" };

/// Values the callee stubs return, set by prepare for each run.
var get_ret: i16 = 0;
var fe_ret: i16 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const p = ctx.allocBytes(8, 4);
    p[1] = ctx.pick(u8, &.{ 0, 0x80, 0xFF, 0x01, 0x7F });
    get_ret = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    fe_ret = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    return .{ @as([*c]u8, p), ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    return if ((b & 0x80) != 0) 1 else 2;
}

pub const stubs = struct {
    /// Reads the caller's bytes; the returned short is the C's temp input.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        if (ctx.value(u1) == 1) return ctx.value(c_short);
        return get_ret;
    }

    /// Maps the short to a short; the result is what the C passes on.
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_short {
        _ = x;
        if (ctx.value(u1) == 1) return ctx.value(c_short);
        return fe_ret;
    }
};
