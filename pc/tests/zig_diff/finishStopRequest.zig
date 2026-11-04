//! zig_diff spec: func_800DB7B4 -> pc.finish_stop_request.finishStopRequest (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose request byte 0x12D is 0, 1 or another value
//! (2, 0xFF, any byte); the record at word 0x54 whose byte 9 is below, at or
//! above 9. The stubs of func_800DEEFC and func_800E678C record their
//! arguments, so the command (1 or 2) and the release order show.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").finishStopRequest));

pub const classes = [_][:0]const u8{ "no-request", "idle", "release-after-request-1", "release-after-other-request" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    state[0x12d] = ctx.edge(u8, &.{ 0, 1, 1, 1, 2, 0xFF, 3 });
    const command = ctx.allocBytes(0x10, 16);
    for (command[0..0x10]) |*byte| byte.* = ctx.int(u8);
    command[9] = ctx.edge(u8, &.{ 0, 1, 8, 9, 10, 0xFF });
    std.mem.writeInt(u32, state[0x54..][0..4], @intCast(@intFromPtr(command)), .little);
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0];
    if (state[0x12d] == 0) return 0;
    const command: [*]const u8 = @ptrFromInt(std.mem.readInt(u32, state[0x54..][0..4], .little));
    if (state[0x12d] == 1) return if (command[9] < 9) 1 else 2;
    return 3;
}
