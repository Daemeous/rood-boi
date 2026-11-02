//! zig_diff spec: func_800B8764 -> func_800B8764.func_800B8764 (4A0A8.c).
//! Inputs: a 4-byte arena buffer as arg0 (the C reads arg0[1] unconditionally,
//! so arg0 is never null) with random bytes, so arg0[1] covers every byte value,
//! with and without the sign bit, and a random arg1 that the C ignores. The
//! call to func_8007CC6C is a recording stub; the C reads nothing back from
//! the callee, so the stub changes no state the C observes.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8764));

pub const classes = [_][:0]const u8{ "sign-byte", "plain-byte" };

pub const stubs = struct {
    pub fn func_8007CC6C(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        _ = x;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    if (ctx.chance(4)) buf[1] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if ((args[0][1] & 0x80) != 0) 0 else 1;
}
