//! zig_diff spec: func_800DC424 -> pc.actor_list.snapshotActorStats (D2: pc/src/battle_helpers.c).
//! Inputs: the linked actor list from vs_battle_actors[0] (0 to 3 records,
//! each a next guest pointer at 0, an id 0..7 at 4 and a guest pointer to a
//! random-filled 0x960-byte stats block at 0x3C; an empty list is reported
//! through pc_unimplemented, compared by its text) and a random-filled
//! snapshot area of eight 0x18-byte records. Two records may share an id.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").snapshotActorStats));

pub const classes = [_][:0]const u8{ "empty-list", "one-actor", "several-actors" };

var length: usize = 0;

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    length = ctx.range(usize, 0, 3);
    var next: u32 = 0;
    var i = length;
    while (i > 0) : (i -= 1) {
        const node = ctx.allocBytes(0x80, 16);
        for (node[0..0x80]) |*byte| byte.* = ctx.int(u8);
        const stats = ctx.allocBytes(0x960, 16);
        for (stats[0..0x960]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(u32, node[0..4], next, .little);
        std.mem.writeInt(i32, node[4..8], ctx.range(i32, 0, 7), .little);
        std.mem.writeInt(u32, node[0x3c..][0..4], @intCast(@intFromPtr(stats)), .little);
        next = @intCast(@intFromPtr(node));
        v.g.vs_battle_actors()[0] = @ptrFromInt(next);
    }
    if (length == 0) v.g.vs_battle_actors()[0] = null;
    const snapshots = ctx.allocBytes(8 * 0x18, 16);
    for (snapshots[0 .. 8 * 0x18]) |*byte| byte.* = ctx.int(u8);
    return .{snapshots};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @min(length, 2);
}
