//! zig_diff spec: func_800E48F8 -> pc.approach_point_if_approaching.refreshApproachPointIfApproaching (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose mode byte (0x89) is 1 (approaching), 0, 2, 0xFF
//! or any byte. The stub of func_800E4904 records the call (and the state when
//! it is made).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").refreshApproachPointIfApproaching));

pub const classes = [_][:0]const u8{ "other-mode", "approaching" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    block[0x89] = ctx.pick(u8, &.{ 1, 1, 0, 2, 0xFF, 0x81 });
    return .{block};
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0][0x89] == 1);
}
