//! zig_diff spec: func_800BA684 -> func_800BA684.func_800BA684 (4A0A8.c).
//! Inputs: a 4-byte arena block for arg0 (every byte random, bytes 1 and 2
//! read by the C; byte 1 takes the sign edges 0x00, 0x7F, 0x80, 0xFF so the
//! `(signed char)` widening is checked both ways), arg1 at its edges, and the
//! global D_800F4C69 at 0, 0x80 and 0xFF. The C has no branches; the classes
//! split the runs by the sign of arg0[1]. The callee's return value is ignored
//! by the C and it reads nothing back, so its stub only draws its return.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA684));

pub const classes = [_][:0]const u8{ "byte1-negative", "byte1-positive" };

pub const stubs = struct {
    pub fn func_800455AC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(4, 1);
    bytes[0] = ctx.int(u8);
    bytes[1] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    bytes[2] = ctx.int(u8);
    bytes[3] = ctx.int(u8);
    v.g.D_800F4C69().* = ctx.pick(u8, &.{ 0, 0x80, 0xFF });
    return .{ @as([*c]u8, bytes), ctx.pick(c_short, &.{ 0, -1, 0x7FFF, -0x8000 }) };
}

pub fn classify(args: Args) usize {
    if (args[0][1] >= 0x80) return 0;
    return 1;
}
