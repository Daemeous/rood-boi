//! zig_diff spec: func_800D01E4 -> func_800D01E4.func_800D01E4 (5BF94.c).
//! Inputs: a mode word whose bit 7 picks the direction and bits 5..6 the
//! curve 0..2 (3 leaves the C's result uninitialised and stores it: waived);
//! the limit and frame at their edges. The lerps get the C's stack vectors
//! (tags): their stubs log start and end and write a result.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D01E4));

var log: ?[*][3]i16 = null;

fn logVec(k: usize, p: anytype) void {
    const s: *v.SVECTOR = @ptrCast(@alignCast(p));
    log.?[k] = .{ s.vx, s.vy, s.vz }; // the pad is uninitialised in the C
}

fn arena(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(8 * 6, 16)));
    const a = ctx.alloc(v.func_800D0548_arg0);
    const b = ctx.alloc(v.func_800D0548_arg1);
    a.mode = ctx.int(u16);
    a.limit = ctx.pick(u8, &.{ 0, 1, 127, 255 });
    b.frame = ctx.pick(i16, &.{ 0, 1, 128, -1 });
    return .{ a, b };
}

pub const classes = [_][:0]const u8{ "linear", "eased", "curve" };

pub const stubs = struct {
    pub fn func_800D1778(ctx: *zd.Ctx, start: anytype, end: anytype, steps: anytype, step: anytype, out: anytype) void {
        _ = steps;
        _ = step;
        tween(ctx, start, end, out);
    }
    pub fn func_800D1718(ctx: *zd.Ctx, start: anytype, end: anytype, steps: anytype, step: anytype, out: anytype) void {
        _ = steps;
        _ = step;
        tween(ctx, start, end, out);
    }
};

fn tween(ctx: *zd.Ctx, start: anytype, end: anytype, out: anytype) void {
    logVec(0, start);
    logVec(1, end);
    const r: *v.SVECTOR = @ptrCast(@alignCast(out));
    r.vx = ctx.value(i16);
    r.vy = ctx.value(i16);
    r.vz = ctx.value(i16);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const args = arena(ctx);
    if ((args[0].?.mode >> 5) & 3 == 3) args[0].?.mode ^= 0x20;
    return args;
}

pub fn classify(args: Args) usize {
    return (args[0].?.mode >> 5) & 3;
}
