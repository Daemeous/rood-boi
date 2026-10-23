//! zig_diff spec: func_800AA600 -> func_800AA600.func_800AA600 (40564.c).
//! The C body is one call, func_800AA698(arg0, arg1, arg2), so the inputs are
//! the three arguments: arg0 and arg2 are random ints drawn from edges and
//! extremes (negative, 16-bit boundaries, INT_MIN/INT_MAX), and arg1 is null
//! or a fresh SVECTOR in the arena. The callee's external effect on the
//! SVECTOR (its three fields) is modelled by the stub, which writes random
//! values into it when it is non-null and returns void. The C has no branches
//! of its own, so coverage is the single call and the null/non-null split.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA600));

pub const classes = [_][:0]const u8{ "null-vector", "vector" };

pub const stubs = struct {
    /// The callee may rewrite the vector it is given; it never returns a value.
    pub fn func_800AA698(ctx: *zd.Ctx, a0: anytype, a1: anytype, a2: anytype) void {
        _ = a0;
        _ = a2;
        if (a1 == null) return;
        const s: *v.SVECTOR = @ptrCast(@alignCast(a1));
        s.vx = ctx.value(i16);
        s.vy = ctx.value(i16);
        s.vz = ctx.value(i16);
    }
};

const edges_int = [_]c_int{
    0,
    1,
    -1,
    255,
    256,
    32767,
    32768,
    -32768,
    65535,
    65536,
    -65536,
    std.math.minInt(c_int),
    std.math.maxInt(c_int),
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const vec: ?*v.SVECTOR = if (ctx.chance(6)) null else ctx.alloc(v.SVECTOR);
    const a0 = ctx.pick(c_int, &edges_int);
    const a2 = ctx.pick(c_int, &edges_int);
    return .{ a0, vec, a2 };
}

pub fn classify(args: Args) usize {
    return if (args[1] == null) 0 else 1;
}
