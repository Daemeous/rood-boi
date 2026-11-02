//! zig_diff spec: func_800BA8BC -> func_800BA8BC.func_800BA8BC (4A0A8.c).
//! Inputs: a byte pointer into the arena and any short arg1. The C only forms
//! arg0+1 and arg0+3 and passes them to the stubbed vs_battle_getShort, so it
//! never dereferences arg0. arg1 is unused by the C, so it is only a value the
//! port must ignore. The getShort stub returns any 16-bit value, which makes a
//! signed/unsigned slip visible in the traced call arguments, and the trace
//! records the pointers passed to getShort, so a wrong offset shows there too.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA8BC));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-nonnegative" };

pub const stubs = struct {
    /// Reads a short from guest memory; the C only passes the result on.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    /// Returns a value the C passes to func_8007BE20 as its second argument.
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = ctx.allocBytes(8, 1);
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ p, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 0) 0 else 1;
}
