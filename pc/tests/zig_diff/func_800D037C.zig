//! zig_diff spec: func_800D037C -> func_800D037C.func_800D037C (5BF94.c).
//! Inputs: a mode word whose bit 7 picks the direction and bits 5..6 the
//! timing 0..3 (3 passes the C's uninitialised t: excluded as an argument
//! that iteration); the limit and frame at their edges. The spline gets the
//! C's stack vectors (tags): its stub logs start and end and writes a
//! result.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D037C));

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

pub const classes = [_][:0]const u8{ "linear", "eased", "curve", "uninitialised" };

var mode3 = false;

pub fn uninitialisedArgument(call: []const u8, index: usize) bool {
    return mode3 and index == 4 and std.mem.eql(u8, call, "vs_battle_splineInterpolate");
}

pub const stubs = struct {
    pub fn vs_battle_splineInterpolate(ctx: *zd.Ctx, p0: anytype, p1: anytype, p2: anytype, p3: anytype, t: anytype, out: anytype) void {
        _ = p1;
        _ = p2;
        _ = t;
        logVec(0, p0);
        logVec(1, p3);
        const r: *v.SVECTOR = @ptrCast(@alignCast(out));
        r.vx = ctx.value(i16);
        r.vy = ctx.value(i16);
        r.vz = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const args = arena(ctx);
    mode3 = (args[0].?.mode >> 5) & 3 == 3;
    return args;
}

pub fn classify(args: Args) usize {
    return (args[0].?.mode >> 5) & 3;
}
