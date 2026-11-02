//! zig_diff spec: func_800D21C0 -> func_800D21C0.func_800D21C0 (5BF94.c).
//! No arguments: the keyframes D_800F5520, the frame D_800F54D0 and count
//! D_800F55A0 keep the harness's random bytes. func_800D1884 gets the C's stack
//! vectors (logged as tags): its stub appends start and end to an arena log
//! and writes a result, so the vectors and the shifts are compared;
//! func_800D1904 returns any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D21C0));

pub const classes = [_][:0]const u8{"blend"};

var log: ?[*]v.VECTOR = null;

pub const stubs = struct {
    pub fn func_800D1884(ctx: *zd.Ctx, start: anytype, end: anytype, steps: anytype, step: anytype, out: anytype) void {
        _ = steps;
        _ = step;
        // The vectors' pad words are uninitialised in the C: x, y, z only.
        const a: *v.VECTOR = @ptrCast(@alignCast(start));
        const z: *v.VECTOR = @ptrCast(@alignCast(end));
        const k = 2 * @as(usize, ctx.calls); // the two lerps are this run's first two calls
        log.?[k] = .{ .vx = a.vx, .vy = a.vy, .vz = a.vz, .pad = 0 };
        log.?[k + 1] = .{ .vx = z.vx, .vy = z.vy, .vz = z.vz, .pad = 0 };
        const r: *v.VECTOR = @ptrCast(@alignCast(out));
        r.vx = ctx.value(i32);
        r.vy = ctx.value(i32);
        r.vz = ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v.VECTOR), 16)));
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
