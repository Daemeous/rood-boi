//! zig_diff spec: func_800AAA68 -> func_800AAA68.func_800AAA68 (40564.c).
//! The C body is a single forwarding call to func_800AAA88, so there are no
//! branches or value comparisons of its own. The inputs cover every argument
//! shape the call can see: arg0 and arg2 take edge values (0, -1, the int
//! extremes, values with the high half set), and arg1 is either null or a
//! random SVECTOR in the arena. The callee stub records its arguments and
//! sometimes rewrites the fields of a non-null SVECTOR with ctx.value, so a
//! port that reads arg1 back after the call is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AAA68));

pub const classes = [_][:0]const u8{ "arg1-null", "arg1-set" };

pub const stubs = struct {
    /// What func_800AAA88 may change: the fields of the SVECTOR it was given
    /// (when it was given one). Null is never dereferenced. The argument
    /// arrives as the C-side pointer type in the C run and as the port's
    /// pointer type in the Zig run, so it is converted by address.
    pub fn func_800AAA88(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = arg0;
        _ = arg2;
        const raw: usize = @intFromPtr(arg1);
        if (raw == 0) return;
        const s: *v.SVECTOR = @ptrFromInt(raw);
        if (ctx.value(u1) == 1) s.vx = ctx.value(i16);
        if (ctx.value(u1) == 1) s.vy = ctx.value(i16);
        if (ctx.value(u1) == 1) s.vz = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const vec: ?*v.SVECTOR = if (ctx.chance(4)) null else ctx.alloc(v.SVECTOR);
    const a0 = ctx.pick(c_int, &.{ 0, -1, 1, std.math.minInt(c_int), std.math.maxInt(c_int), 0xFFFF, 0x10000 });
    const a2 = ctx.pick(c_int, &.{ 0, -1, 1, std.math.minInt(c_int), std.math.maxInt(c_int), 0xFFFF, 0x10000 });
    return .{ a0, vec, a2 };
}

pub fn classify(args: Args) usize {
    return if (args[1] == null) 0 else 1;
}
