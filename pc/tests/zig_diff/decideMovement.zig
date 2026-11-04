//! zig_diff spec: func_800DB820 -> pc.decide_movement.decideMovement (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose callback word (0x68) is 0 or the address of a
//! native function of this spec (one that claims the frame, one that declines,
//! each marking the state it was handed). The stubs of func_800DAD9C and
//! func_800DB0BC answer from the spec; that of pc_require_native_callback
//! records the callback address and its text argument (compared by text).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").decideMovement));

pub const classes = [_][:0]const u8{ "dispatch-claims", "unstick-claims", "callback-claims", "callback-declines", "no-callback" };

var dispatch: c_int = 0;
var unstick: c_int = 0;
var scenario: usize = 0;

fn claims(state: ?*anyopaque) callconv(.c) c_int {
    const bytes: [*]u8 = @ptrCast(state.?);
    bytes[0x7f] = 0xA5;
    return 2;
}

fn declines(state: ?*anyopaque) callconv(.c) c_int {
    const bytes: [*]u8 = @ptrCast(state.?);
    bytes[0x7e] = 0x5A;
    return 0;
}

pub const stubs = struct {
    pub fn func_800DAD9C(ctx: *zd.Ctx, state: anytype) c_int {
        _ = ctx;
        _ = state;
        return dispatch;
    }
    pub fn func_800DB0BC(ctx: *zd.Ctx, state: anytype) c_int {
        _ = ctx;
        _ = state;
        return unstick;
    }
};

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 1 and std.mem.eql(u8, call, "pc_require_native_callback");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x100, 16);
    for (state[0..0x100]) |*byte| byte.* = ctx.int(u8);
    dispatch = ctx.edge(c_int, &.{ 0, 0, 1, -1, 2 });
    unstick = ctx.edge(c_int, &.{ 0, 0, 1, -1, 2 });
    scenario = ctx.range(usize, 0, 2);
    const callback: u32 = switch (scenario) {
        0 => 0,
        1 => @intCast(@intFromPtr(&claims)),
        else => @intCast(@intFromPtr(&declines)),
    };
    std.mem.writeInt(u32, state[0x68..][0..4], callback, .little);
    return .{state};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (dispatch != 0) return 0;
    if (unstick != 0) return 1;
    return switch (scenario) {
        0 => 4,
        1 => 2,
        else => 3,
    };
}
