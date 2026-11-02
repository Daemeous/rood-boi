//! zig_diff spec: func_800D1B80 -> func_800D1B80.func_800D1B80 (5BF94.c).
//! Inputs: an arena effect camera with random words; the trigonometry and
//! rsqrt are stubs returning any value. The setters get the C's stack
//! position (a tag): their stubs log it. vs_battle_setFarClip's second
//! argument is uninitialised in the C (its cast call passes one): excluded.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D1B80));

pub const classes = [_][:0]const u8{"apply"};

var log: ?*[2][3]i32 = null;

pub fn uninitialisedArgument(call: []const u8, index: usize) bool {
    return index == 1 and std.mem.eql(u8, call, "vs_battle_setFarClip");
}

fn logVec(k: usize, p: anytype) void {
    const s: *v.VECTOR = @ptrCast(@alignCast(p));
    log.?[k] = .{ s.vx, s.vy, s.vz }; // the pad is uninitialised in the C
}

pub const stubs = struct {
    pub fn vs_battle_setCameraPosition(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        logVec(0, p);
    }
    pub fn vs_battle_setCameraLookAt(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        logVec(1, p);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(24, 16)));
    return .{ctx.alloc(v.D_800F54D8_t)};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
