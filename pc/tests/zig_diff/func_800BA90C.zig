//! zig_diff spec: func_800BA90C -> func_800BA90C.func_800BA90C (4A0A8.c).
//! Inputs: a 2-byte buffer in the arena with random bytes, so arg0[1] covers
//! all of u8 (128-255 would read negative through a signed slip). The global
//! D_800F4C2C is set to 2 (the call is taken) or to another value (not taken).
//! The stub of func_8007BEC4 may rewrite D_800F4C2C, which the C reads back
//! after the call. arg1 is unused by the C and drawn at random.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA90C));

pub const classes = [_][:0]const u8{ "flag-not-2", "flag-2" };

pub const stubs = struct {
    /// The callee may change the flag the C tests after the call.
    pub fn func_8007BEC4(ctx: *zd.Ctx, arg: anytype) void {
        _ = arg;
        if (ctx.value(u1) == 1) {
            v.g.D_800F4C2C().* = if (ctx.value(u1) == 1) 2 else ctx.value(u8);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(2, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.int(u8);
    const flag = v.g.D_800F4C2C();
    flag.* = ctx.pick(u8, &.{ 0, 1, 2, 3, 255 });
    if (ctx.chance(2)) flag.* = 2;
    return .{ buf, ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 1 else 0;
}
