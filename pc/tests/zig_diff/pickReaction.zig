//! zig_diff spec: func_800E4764 -> pc.reaction_pick.pickReaction (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose byte 1 is clear or set and whose gait word
//! (0x224) has flags 0x20 and 0x10 in its low byte and in byte 1, each armed
//! or not (the four combinations in both bytes), with random bytes 2 and 3
//! (the value) and random other flag bits; value and code are arena ints. The
//! final path (byte 1 set, nothing armed) calls pc_unimplemented (a stub) and
//! returns 0.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").pickReaction));

pub const classes = [_][:0]const u8{ "first-armed", "second-armed", "none", "none-endless" };

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x300, 16);
    for (block[0..0x300]) |*byte| byte.* = ctx.int(u8);
    block[1] = ctx.edge(u8, &.{ 0, 1 });
    const word: *align(1) u32 = @ptrCast(block + 0x224);
    const armed = [_]u32{ 0, 0x10, 0x20, 0x30 };
    word.* = (ctx.int(u32) & ~@as(u32, 0x3030)) | armed[ctx.range(usize, 0, 3)] | (armed[ctx.range(usize, 0, 3)] << 8);
    const ints: [*]c_int = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    ints[0] = ctx.int(c_int);
    ints[1] = ctx.int(c_int);
    return .{ block, &ints[0], &ints[1] };
}

pub fn classify(args: Args) usize {
    const block = args[0];
    const word = @as(*align(1) const u32, @ptrCast(block + 0x224)).*;
    const armed = (word | (word >> 8)) & 0x30;
    if (armed == 0) return if (block[1] != 0) 3 else 2;
    const first: u32 = if (block[1] != 0) 0x20 else 0x10;
    return if (armed & first != 0) 0 else 1;
}
