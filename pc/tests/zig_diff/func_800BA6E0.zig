//! zig_diff spec: func_800BA6E0 -> func_800BA6E0.func_800BA6E0 (4A0A8.c).
//! Inputs: arg0 is a 2-byte buffer in the arena whose byte 1 is an edge value
//! (0, 1, 0x7F, 0x80, 0xFF or random), so the C's signed/unsigned read and the
//! callee arguments differ between mistakes. arg1 is unused by the C but takes
//! edge shorts. Both callees return zero or nonzero per call (nonzero values
//! with and without the sign bit), so the C takes every branch: the first
//! test false, the second test true and false.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA6E0));

pub const classes = [_][:0]const u8{ "byte-zero", "byte-high", "byte-low" };

pub const stubs = struct {
    /// Zero a quarter of the time, otherwise a nonzero value whose sign bit is random.
    fn result(ctx: *zd.Ctx) c_int {
        if (ctx.value(u2) == 0) return 0;
        const x = ctx.value(c_int);
        return if (x == 0) 1 else x;
    }

    pub fn func_8004550C(ctx: *zd.Ctx, t: anytype) c_int {
        _ = t;
        return result(ctx);
    }

    pub fn func_800454B8(ctx: *zd.Ctx, t: anytype) c_int {
        _ = t;
        return result(ctx);
    }

    /// The C ignores its result; the callee does not touch the C's inputs.
    pub fn func_80044DF4(ctx: *zd.Ctx, t: anytype) c_int {
        _ = t;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(2, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    return if ((b & 0x80) != 0) 1 else 2;
}
