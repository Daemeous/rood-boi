//! zig_diff spec: func_800D7814 -> func_800D7814.func_800D7814 (6F014.c).
//! The body takes no arguments and has no branches in the routed C (the
//! ROOD_PC block is compile-time). It stores zero and guest addresses into
//! globals and the D_800F56C8/D_800F57A0 structs, then calls func_800D7890(0)
//! and func_800D8060(&D_800EC4BC). Every global starts as random bytes, so
//! each store shows in the compared state and a dropped or misdirected store
//! is caught. The C reads nothing back from either call. func_800D7890's stub
//! still writes a random value to a global, so the state at the next call
//! differs from run to run only through what the C does. func_800D8060 is a
//! void recording stub, and its argument is in the call trace.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7814));

pub const classes = [_][:0]const u8{"always"};

pub const stubs = struct {
    pub fn func_800D7890(ctx: *zd.Ctx, arg0: c_int) void {
        _ = arg0;
        v.g.D_800F5684().* = ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
