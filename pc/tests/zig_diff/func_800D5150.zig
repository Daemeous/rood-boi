//! zig_diff spec: func_800D5150 -> func_800D5150.func_800D5150 (5BF94.c).
//! The C ignores its argument and returns func_800CE644(0x1E), so the inputs
//! are the argument (null or a random arena object, which the C never reads)
//! and the value the callee returns, which is any c_int and is passed through
//! unchanged. The callee's stub writes nothing the C reads back: the C reads
//! no state after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5150));

pub const classes = [_][:0]const u8{ "null-arg", "object-arg" };

pub const stubs = struct {
    /// Returns any c_int: the C passes the result straight through.
    pub fn func_800CE644(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(2)) return .{null};
    return .{ctx.alloc(v.D_800F53B8_t)};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
