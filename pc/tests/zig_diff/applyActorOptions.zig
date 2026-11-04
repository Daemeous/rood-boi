//! zig_diff spec: func_800E72D0 -> pc.actor_list.applyActorOptions (D2: pc/src/battle_helpers.c).
//! Inputs: the linked actor list from vs_battle_actors[0] (0 to 4 records in
//! the arena, each a next guest pointer at 0 and an actor id 0..16 at 4),
//! every slot of the AI entity table D_800F5878 a random-filled block whose
//! signed halfword at 0x42 (the packed options) is an edge (negative ones
//! leave the entity alone; bit 0 calls func_800DC210) or any value, and whose
//! word at 0x1A4 is random. The stub of func_800DC210 records (entity, 1).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").applyActorOptions));

pub const classes = [_][:0]const u8{ "empty-list", "negative", "applied", "applied-with-call" };

var which: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        const block = ctx.allocBytes(0x200, 16);
        for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(i16, block[0x42..][0..2], ctx.pick(i16, &.{ 0, 1, 2, -1, 0x7FFF, -0x8000, 0x3F, 0x40, 0x7F, 0x1FF }), .little);
        table[i] = @ptrCast(block);
    }
    const length = ctx.range(usize, 0, 4);
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
    which = 0;
    if (length == 0) {
        v.g.vs_battle_actors()[0] = null;
    } else {
        const first: [*]const u8 = @ptrFromInt(next);
        const id: usize = @intCast(std.mem.readInt(i32, first[4..8], .little));
        const entity: [*]const u8 = @ptrCast(table[id].?);
        const options = std.mem.readInt(i16, entity[0x42..][0..2], .little);
        which = if (options < 0) 1 else if (options & 1 == 0) 2 else 3;
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
