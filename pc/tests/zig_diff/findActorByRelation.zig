//! zig_diff spec: func_800DBB2C -> pc.find_actor_by_relation.findActorByRelation (D2: pc/src/battle_movement.c).
//! Inputs: an actor list of 0..4 nodes (link word at 0, actor id at 4, object
//! pointer at 0x44, whose tile word is at 0x5C) with ids 0..15; a state block
//! whose byte 0x88 is the id of one node or another, and whose relation words
//! (0x1A4 + id * 4) have bits 6-7 equal to the wanted relation for some ids
//! and random bits elsewhere; a wanted relation 0..3 or beyond (4, -1:
//! `relation << 6` then matches no bit pair); an output tile pointer or null.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").findActorByRelation));

pub const classes = [_][:0]const u8{ "empty-list", "none-match", "first-match-stored", "first-match-tile-null", "later-match", "match-after-self" };

var found: usize = 0;
var stored: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x400, 16);
    for (state[0..0x400]) |*byte| byte.* = ctx.int(u8);
    const relation = ctx.edge(c_int, &.{ 0, 1, 2, 3, 0, 1, 2, 3, 4, -1 });
    const count = ctx.range(usize, 0, 4);
    var ids: [4]c_int = undefined;
    var previous: ?[*]u8 = null;
    var first: ?[*]u8 = null;
    for (0..count) |i| {
        const node = ctx.allocBytes(0x60, 16);
        for (node[0..0x60]) |*byte| byte.* = ctx.int(u8);
        ids[i] = ctx.range(c_int, 0, 15);
        std.mem.writeInt(u32, node[0..][0..4], 0, .little);
        std.mem.writeInt(i32, node[4..][0..4], ids[i], .little);
        const object = ctx.allocBytes(0x80, 16);
        for (object[0..0x80]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(u32, node[0x44..][0..4], @intCast(@intFromPtr(object)), .little);
        if (previous) |p| std.mem.writeInt(u32, p[0..4], @intCast(@intFromPtr(node)), .little) else first = node;
        previous = node;
        // Relation bits 6-7 of this id: the wanted ones a third of the time.
        const at = 0x1a4 + @as(usize, @intCast(ids[i])) * 4;
        var word = std.mem.readInt(u32, state[at..][0..4], .little);
        if (ctx.chance(3)) word = (word & ~@as(u32, 0xc0)) | (@as(u32, @bitCast(relation)) << 6 & 0xc0);
        std.mem.writeInt(u32, state[at..][0..4], word, .little);
    }
    v.g.vs_battle_actors()[0] = if (first) |f| @ptrCast(f) else null;
    state[0x88] = if (count != 0 and ctx.chance(3)) @intCast(ids[ctx.range(usize, 0, count - 1)]) else ctx.range(u8, 0, 20);
    const tile: ?*u32 = if (ctx.chance(3)) null else @ptrCast(@alignCast(ctx.allocBytes(4, 4)));
    stored = tile != null;
    // Classify by the same walk.
    found = 0;
    var node: ?[*]u8 = first;
    var index: usize = 0;
    var skipped = false;
    while (node) |n| : (index += 1) {
        const id = std.mem.readInt(i32, n[4..][0..4], .little);
        const word = std.mem.readInt(u32, state[0x1a4 + @as(usize, @intCast(id)) * 4 ..][0..4], .little);
        if (id == state[0x88]) {
            skipped = true;
        } else if ((word & 0xc0) == (@as(u32, @bitCast(relation)) << 6)) {
            found = if (index == 0) 2 else if (skipped) 4 else 3;
            break;
        }
        const next = std.mem.readInt(u32, n[0..4], .little);
        node = if (next == 0) null else @ptrFromInt(next);
    }
    if (count == 0) found = 0 else if (found == 0) found = 1;
    return .{ state, tile, relation };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (found == 2) return if (stored) 2 else 3;
    return if (found >= 3) found + 1 else found;
}
