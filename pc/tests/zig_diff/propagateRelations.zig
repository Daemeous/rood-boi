//! zig_diff spec: func_800E7370 -> pc.actor_list.propagateRelations (D2: pc/src/battle_helpers.c).
//! Inputs: the linked actor list from vs_battle_actors[0] (0 to 3 records, each
//! a next guest pointer at 0 and an actor id 0..16 at 4), every slot of the AI
//! entity table D_800F5878 a random-filled block (class byte at 0x2C of the
//! peer at the edges around 3), the state block (own id at 0x88 equal to some
//! record's id, class byte at 0x2C 0..3, relation words at 0x1A4 + id * 4)
//! and the relation table D_80069C0C random. The slot of the state's own id
//! is sometimes the state itself, so the reciprocal word read before the
//! write is the word the write replaces. The stub of func_800E7370 (the
//! recursion) records the peer.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").propagateRelations));

pub const classes = [_][:0]const u8{ "empty-list", "own-id", "table-relation" };

var which: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        const block = ctx.allocBytes(0x400, 16);
        for (block[0..0x400]) |*byte| byte.* = ctx.int(u8);
        block[0x2c] = ctx.edge(u8, &.{ 0, 1, 2, 3, 3, 4, 0xff });
        table[i] = @ptrCast(block);
    }
    const state = ctx.allocBytes(0x400, 16);
    for (state[0..0x400]) |*byte| byte.* = ctx.int(u8);
    state[0x2c] = ctx.range(u8, 0, 3);
    state[0x88] = ctx.range(u8, 0, 16);
    if (ctx.chance(3)) table[state[0x88]] = @ptrCast(state);
    const length = ctx.range(usize, 0, 3);
    var next: u32 = 0;
    var first_id: i32 = -1;
    var i = length;
    while (i > 0) : (i -= 1) {
        const node = ctx.allocBytes(0x20, 16);
        for (node[0..0x20]) |*byte| byte.* = ctx.int(u8);
        const id: i32 = if (ctx.chance(3)) state[0x88] else ctx.range(i32, 0, 16);
        std.mem.writeInt(u32, node[0..4], next, .little);
        std.mem.writeInt(i32, node[4..8], id, .little);
        next = @intCast(@intFromPtr(node));
        first_id = id;
        v.g.vs_battle_actors()[0] = @ptrFromInt(next);
    }
    if (length == 0) v.g.vs_battle_actors()[0] = null;
    which = if (length == 0) 0 else if (first_id == state[0x88]) 1 else 2;
    return .{state};
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
