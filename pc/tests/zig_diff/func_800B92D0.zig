//! zig_diff spec: func_800B92D0 -> func_800B92D0.func_800B92D0 (4A0A8.c).
//! The C has no branches and no comparisons: it reads arg0[1], calls
//! func_8007BD8C(arg0[1] ^ 1) and returns 0. The inputs cover a 4-byte arena
//! buffer whose second byte is drawn from the edges (0, 1, 0x7F, 0x80, 0xFF)
//! or left random, and arg1 across the short edges, so a return of arg1 or a
//! changed argument shows. The callee's return value is random per call and
//! ignored by the C; the classes split on the sign bit of arg0[1].
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B92D0));

pub const classes = [_][:0]const u8{ "byte1-high", "byte1-low" };

pub const stubs = struct {
    pub fn func_8007BD8C(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    const a1 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    return .{ buf, a1 };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    return if (p[1] >= 128) 0 else 1;
}
