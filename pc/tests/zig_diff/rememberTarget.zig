//! zig_diff spec: func_800E4C8C -> remember_target.rememberTarget (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes whose candidate count (0x470)
//! is an edge around zero or any int, whose lock byte 0x19 is zero two
//! thirds of the time, and whose candidate pointer (0x430) is a 4-word arena
//! block of random words; the actor object table D_800F4538 has all 17 slots
//! random-filled blocks, so any target nibble (0..15) has a tile word at 0x5C.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").rememberTarget));

pub const classes = [_][:0]const u8{ "no-candidate", "locked", "remembered" };

fn word(p: [*]u8, off: usize) *align(1) u32 {
    return @ptrCast(p + off);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x500, 16);
    for (state[0..0x500]) |*byte| byte.* = ctx.int(u8);
    word(state, 0x470).* = ctx.pick(u32, &.{ 0, 1, 0xffffffff, 0x7fffffff, 0x80000000 });
    if (ctx.chance(3)) state[0x19] = ctx.edge(u8, &.{ 1, 0x80, 0xff }) else state[0x19] = 0;
    const candidate = ctx.allocBytes(16, 16);
    for (candidate[0..16]) |*byte| byte.* = ctx.int(u8);
    word(state, 0x430).* = @intCast(@intFromPtr(candidate));
    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const block = ctx.allocBytes(0x80, 16);
        for (block[0..0x80]) |*byte| byte.* = ctx.int(u8);
        table[i] = @ptrCast(block);
    }
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0];
    if (@as(i32, @bitCast(word(state, 0x470).*)) <= 0) return 0;
    return if (state[0x19] != 0) 1 else 2;
}
