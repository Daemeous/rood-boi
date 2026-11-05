//! zig_diff spec: func_8009820C -> pc.draw_room_background.drawRoomBackground (D2: pc/src/boot_sdk.c).
//! Inputs: the mesh block in the arena; the scratchpad's packet cursor (word 0),
//! ordering table (word 4, plus 2047 words in the call) and neighbouring words
//! are random (word-aligned, the table zero a quarter of the time). The stub of
//! pc_background_packets records the mesh, cursor, table and vertex buffer
//! (0x1F800100) and returns null or an arena block, which the function must
//! store back as the cursor.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawRoomBackground));

pub const classes = [_][:0]const u8{ "table-null", "table-set" };

var table_null = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    scratch[0] = if (ctx.chance(4)) 0 else @as(u32, @intCast(@intFromPtr(ctx.allocBytes(64, 16))));
    table_null = ctx.chance(4);
    scratch[1] = if (table_null) 0 else ctx.pick(u32, &.{ 0x1000, 0x80000000, 0xFFFFE000 }) & ~@as(u32, 3);
    scratch[2] = ctx.int(u32);
    scratch[0x100 / 4] = ctx.int(u32);
    return .{@ptrCast(@alignCast(ctx.allocBytes(64, 16)))};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(!table_null);
}

pub const stubs = struct {
    pub fn pc_background_packets(ctx: *zd.Ctx, mesh: anytype, packet: anytype, ot: anytype, transformed: anytype) [*c]c_uint {
        _ = .{ mesh, packet, ot, transformed };
        if (ctx.value(u8) & 1 != 0) return null;
        return @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    }
};
