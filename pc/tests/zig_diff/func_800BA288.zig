//! zig_diff spec: func_800BA288 -> func_800BA288.func_800BA288 (4A0A8.c).
//! The C has no branches and no ordered comparisons. Its inputs are a
//! three-byte buffer in the arena whose byte 1 and byte 2 come from edge
//! values (0x00, 0x7F, 0x80, 0xFF) or random bytes, so the signed-char
//! conversion of byte 2 sees both sides of the sign bit, and an arbitrary
//! `arg1`. The callee's return value is discarded by the C and nothing
//! it could change is read back, so its stub only returns a value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA288));

pub const classes = [_][:0]const u8{ "sign-clear", "sign-set" };

pub const stubs = struct {
    pub fn func_80091A78(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(8, 1);
    buf[1] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    buf[2] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    const arg1 = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if ((args[0][2] & 0x80) != 0) 1 else 0;
}
