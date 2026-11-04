//! zig_diff spec: func_800E7608 -> clear_actor_targets.clearActorTargets (D2: pc/src/battle_movement.c).
//! Inputs: the AI entity table D_800F5878 with slot 0 and a majority of the
//! others random-filled blocks (byte 0x16 equal to the id a third of the
//! time) and some empty; the actor list vs_battle_actors[0], 1-4 arena nodes
//! (next pointer at 0, AI slot index at 4) naming non-empty slots, the last
//! with a null next; an id 0..16 (its own slot may be empty: the final call
//! then gets null). func_800D82CC is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").clearActorTargets));

pub const classes = [_][:0]const u8{ "no-match", "match" };

fn word(p: [*]u8, off: usize) *align(1) u32 {
    return @ptrCast(p + off);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const table = v.g.D_800F5878();
    var present: [17]usize = undefined;
    var count: usize = 0;
    for (0..17) |i| {
        if (i != 0 and ctx.chance(4)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x40, 16);
            for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
            if (ctx.chance(3)) block[0x16] = @intCast(id);
            table[i] = @ptrCast(block);
            present[count] = i;
            count += 1;
        }
    }
    const nodes = ctx.range(usize, 1, 4);
    var next: u32 = 0;
    var head: [*]u8 = undefined;
    for (0..nodes) |_| {
        const node = ctx.allocBytes(16, 16);
        word(node, 0).* = next;
        word(node, 4).* = @intCast(present[ctx.range(usize, 0, count - 1)]);
        next = @intCast(@intFromPtr(node));
        head = node;
    }
    v.g.vs_battle_actors()[0] = @ptrCast(head);
    return .{id};
}

pub fn classify(args: Args) usize {
    var node: [*]u8 = @ptrCast(v.g.vs_battle_actors()[0].?);
    while (true) {
        const state: [*]u8 = @ptrCast(v.g.D_800F5878()[word(node, 4).*].?);
        if (@as(c_uint, state[0x16]) == @as(c_uint, @bitCast(args[0]))) return 1;
        const next = word(node, 0).*;
        if (next == 0) return 0;
        node = @ptrFromInt(next);
    }
}
