//! zig_diff spec: func_800DC810 -> pc.actor_list.evaluateActors (D2: pc/src/battle_helpers.c).
//! Inputs: the linked actor list from vs_battle_actors[0] (0 to 4 records in
//! the arena, each a next guest pointer at 0 and an actor id at 4; an empty
//! list is reported through pc_unimplemented, compared by its text), the
//! AI entity table D_800F5878 (each slot null or a block), and a context
//! block. The stubs of func_800DD000 and func_800DD344 record their
//! arguments, so the two passes, their order and the entity per id show.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").evaluateActors));

pub const classes = [_][:0]const u8{ "empty-list", "one-actor", "several-actors" };

var length: usize = 0;

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(5)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x40, 16);
            for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
            table[i] = @ptrCast(block);
        }
    }
    length = ctx.range(usize, 0, 4);
    var next: u32 = 0;
    var i = length;
    while (i > 0) : (i -= 1) {
        const node = ctx.allocBytes(0x20, 16);
        for (node[0..0x20]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(u32, node[0..4], next, .little);
        std.mem.writeInt(i32, node[4..8], ctx.range(i32, 0, 16), .little);
        next = @intCast(@intFromPtr(node));
        v.g.vs_battle_actors()[0] = @ptrFromInt(next);
    }
    if (length == 0) v.g.vs_battle_actors()[0] = null;
    const context = ctx.allocBytes(0x20, 16);
    for (context[0..0x20]) |*byte| byte.* = ctx.int(u8);
    return .{context};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @min(length, 2);
}
