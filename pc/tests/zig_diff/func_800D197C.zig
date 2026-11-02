//! zig_diff spec: func_800D197C -> func_800D197C.func_800D197C (5BF94.c).
//! Inputs: arena position, target and matrix with random words. The libgte
//! calls get the C's stack vectors (tags): VectorNormal's, OuterProduct0's
//! and ApplyMatrixLV's stubs log their inputs' x, y, z (the pads are
//! uninitialised in the C, up's is the folded initialUp's 0) and write
//! their outputs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D197C));

pub const classes = [_][:0]const u8{"view"};

var log: ?[*][3]i32 = null;

fn logVec(k: usize, p: anytype) void {
    const s: *v.VECTOR = @ptrCast(@alignCast(p));
    log.?[k] = .{ s.vx, s.vy, s.vz };
}

fn fill(ctx: *zd.Ctx, p: anytype) void {
    const s: *v.VECTOR = @ptrCast(@alignCast(p));
    s.vx = ctx.value(i32);
    s.vy = ctx.value(i32);
    s.vz = ctx.value(i32);
}

pub const stubs = struct {
    pub fn VectorNormal(ctx: *zd.Ctx, in: anytype, out: anytype) c_int {
        logVec(2 * @as(usize, ctx.calls), in);
        fill(ctx, out);
        return 0;
    }
    pub fn OuterProduct0(ctx: *zd.Ctx, a: anytype, b: anytype, out: anytype) void {
        logVec(2 * @as(usize, ctx.calls), a);
        logVec(2 * @as(usize, ctx.calls) + 1, b);
        fill(ctx, out);
    }
    pub fn ApplyMatrixLV(ctx: *zd.Ctx, m: anytype, in: anytype, out: anytype) ?*anyopaque {
        _ = m;
        logVec(2 * @as(usize, ctx.calls), in);
        fill(ctx, out);
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(12 * 12, 16)));
    return .{ ctx.alloc(v.VECTOR), ctx.alloc(v.VECTOR), ctx.alloc(v.MATRIX) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
