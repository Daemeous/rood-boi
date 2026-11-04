//! zig_diff spec: func_800D9DD8 -> pc.choose_movement_mode.chooseMovementMode (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled state block with the callback word at 0x68 either
//! the address of func_800DAA6C (the stub's own, so the comparison can be
//! true) or a random word, the best-candidate pointer at 0x430 (always a valid block: a null one
//! would crash the M4 mutation, which reads it with the callback running, instead
//! of mismatching) and the mask byte
//! at 0x2D against the candidate's word 4 so that they share a bit or not.
//! The stub of func_800DD7CC records (state, state + 0x134).
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").chooseMovementMode));

pub const classes = [_][:0]const u8{ "running-callback", "mask-miss", "search" };

fn put(block: [*]u8, offset: usize, value: u32) void {
    std.mem.writeInt(u32, block[offset..][0..4], value, .little);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x500, 16);
    for (state[0..0x500]) |*byte| byte.* = ctx.int(u8);
    const best = ctx.allocBytes(0x20, 16);
    for (best[0..0x20]) |*byte| byte.* = ctx.int(u8);
    const running = ctx.chance(3);
    put(state, 0x68, if (running) @intCast(@intFromPtr(&c.func_800DAA6C)) else ctx.int(u32));
    put(state, 0x430, @intCast(@intFromPtr(best)));
    const word4 = std.mem.readInt(u32, best[0x10..][0..4], .little);
    state[0x2d] = switch (ctx.range(u8, 0, 2)) {
        0 => @truncate(~word4),
        1 => @truncate(word4 | 1),
        else => ctx.int(u8),
    };
    if (ctx.chance(4)) state[0x2d] = 0;
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0];
    if (std.mem.readInt(u32, state[0x68..][0..4], .little) == @as(u32, @intCast(@intFromPtr(&c.func_800DAA6C)))) return 0;
    const best: [*]const u8 = @ptrFromInt(std.mem.readInt(u32, state[0x430..][0..4], .little));
    const word4 = std.mem.readInt(u32, best[0x10..][0..4], .little);
    return if (word4 & state[0x2d] == 0) 1 else 2;
}
