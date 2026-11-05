//! zig_diff spec: vs_battle_initSceneAndGetRoomNames -> room_name_table.initSceneAndGetRoomNames
//! (D2: pc/src/room_geometry.c).
//! Input: a room-name table in the arena: the entry count (0, 1 or up to 6),
//! then 12-byte entries whose word 1 is the name's size (0 to 64, so empty
//! names occur), then the names' bytes. An empty table reaches the
//! pc_unimplemented call (a recording stub, so the walk goes on as in the C).
//! The result pointer and each entry's rewritten word are compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").initSceneAndGetRoomNames));

pub const classes = [_][:0]const u8{ "empty", "one", "several" };

var count: c_uint = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    count = switch (ctx.range(u8, 0, 5)) {
        0 => 0,
        1 => 1,
        else => ctx.range(c_uint, 2, 6),
    };
    const table: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(4 + 6 * 12 + 6 * 64 + 16, 16)));
    table[0] = count;
    var i: usize = 0;
    while (i < count) : (i += 1) {
        table[1 + i * 3 + 1] = ctx.edge(c_uint, &.{ 0, 1, 7, 64, ctx.range(c_uint, 0, 64) });
    }
    return .{table};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (count == 0) 0 else if (count == 1) 1 else 2;
}

/// The message pc_unimplemented gets is the C's literal on one side and the port's on the other: compared by text.
pub fn textArgument(call: []const u8, index: usize) bool {
    return std.mem.eql(u8, call, "pc_unimplemented") and index == 0;
}
