//! zig_diff spec: func_8007B378 -> func_8007B378.func_8007B378 (146C.c).
//! The C is three calls of vs_main_loadClutTransition for slots 0, 1 and 2,
//! the source advanced by 0x100 and 0x200 u_shorts; only the first passes
//! arg3 (the other two omit the fourth argument: a bug of the original). The
//! callee is declared without a prototype in the C, so its stub can only take
//! the context: it cannot see the arguments or write the buffer. The spec
//! therefore tests the number and the order of the calls and, through the
//! mutations, the wrapper's own stores into the source buffer, which the
//! buffer compare sees. Inputs: arg0 and arg3 from edge values, arg2 anything
//! (unused), arg1 null or a 1 KiB arena buffer.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8007B378));

pub const classes = [_][:0]const u8{ "null-source", "plain" };

pub const stubs = struct {
    pub fn vs_main_loadClutTransition(ctx: *zd.Ctx) void {
        _ = ctx;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0 = ctx.pick(c_int, &.{ 0, -1, 1, 0x7FFFFFFF, -2147483648 });
    const a2 = ctx.int(c_int);
    const a3 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -2147483648 });
    var buf: [*c]c_ushort = null;
    if (!ctx.chance(4)) buf = @ptrCast(@alignCast(ctx.allocBytes(1024, 2)));
    return .{ a0, buf, a2, a3 };
}

pub fn classify(args: Args) usize {
    if (args[1] == null) return 0;
    return 1;
}
