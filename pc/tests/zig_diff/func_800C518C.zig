//! zig_diff spec: func_800C518C -> func_800C518C.func_800C518C (5BF94.c).
//! Inputs: arg0 is an edge value or any c_int (so its bits 4-7 are zero about
//! half the time, and the sign bit is set and clear both with and without
//! those bits), arg1 is null or a fresh arena block. The callee
//! func_800A07FC is a recording stub that returns an int the C ignores, and
//! may write the first word of arg1 (the only state it can reach through its
//! arguments).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C518C));

pub const classes = [_][:0]const u8{ "masked-zero-nonnegative", "masked-zero-negative", "masked-nonzero" };

pub const stubs = struct {
    pub fn func_800A07FC(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        if (b) |p| {
            // The callee may update the object it is given.
            if (ctx.value(u1) == 1) {
                const q: *align(1) u32 = @ptrCast(p);
                q.* = ctx.value(u32);
            }
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = ctx.pick(c_int, &.{
        0,
        0x10,
        0x08,
        0x0F,
        0x20,
        0x40,
        0x80,
        0xF0,
        0x100,
        -1,
        std.math.minInt(c_int),
        std.math.maxInt(c_int),
        -256,
    });
    const arg1: ?*anyopaque = if (ctx.chance(3)) null else ctx.alloc(u64);
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const x: c_int = args[0];
    if ((x & 0xF0) != 0) return 2;
    if (x < 0) return 1;
    return 0;
}
