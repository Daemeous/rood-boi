//! zig_diff spec: vs_battle_setSpriteDefault -> pc.sprite_packet.setSpriteDefault (D2: pc/src/battle_helpers.c).
//! Inputs: the default link word at scratchpad 0x1F800008 (a guest pointer
//! to an arena block, or zero) and a size and a position (edges or any int;
//! note the C takes the size first). The stub of
//! vs_battle_setSpriteDefaultTexPage records (0x80, position, size, link) and
//! returns a packet in the arena, which is returned unchanged.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setSpriteDefault));

pub const classes = [_][:0]const u8{ "link-set", "link-null" };

var packet_block: [*c]c_uint = null;
var link_null: bool = false;

const edges = [_]c_int{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x80, 0xFF, 0x100, 0x00FF00FF };

pub fn prepare(ctx: *zd.Ctx) Args {
    const link: *u32 = @ptrFromInt(0x1F800008);
    const block = ctx.allocBytes(0x40, 16);
    for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
    link_null = ctx.chance(5);
    link.* = if (link_null) 0 else @intCast(@intFromPtr(block));
    // The scratchpad words around the link word hold other aligned guest
    // pointers (never random bits: a misread word must give a different
    // argument, not an unaligned pointer that panics the port).
    for ([_]usize{ 0x1F800000, 0x1F800004, 0x1F80000C }) |word| {
        const other = ctx.allocBytes(0x40, 16);
        @as(*u32, @ptrFromInt(word)).* = @intCast(@intFromPtr(other));
    }
    const packet = ctx.allocBytes(0x40, 16);
    packet_block = @ptrCast(@alignCast(packet));
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(link_null);
}

pub const stubs = struct {
    pub fn vs_battle_setSpriteDefaultTexPage(ctx: *zd.Ctx, brightness: anytype, xy: anytype, wh: anytype, next: anytype) [*c]c_uint {
        _ = ctx;
        _ = brightness;
        _ = xy;
        _ = wh;
        _ = next;
        return packet_block;
    }
};
