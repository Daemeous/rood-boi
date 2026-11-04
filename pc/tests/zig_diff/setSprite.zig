//! zig_diff spec: vs_battle_setSprite -> pc.sprite_packet.setSprite (D2: pc/src/battle_helpers.c).
//! Inputs: the primitive cursor word at scratchpad 0x1F800000 (a guest
//! pointer to an arena block, or zero), a brightness, a position and a size
//! (edges or any int; the unsigned parameters of pc_sprite_packet receive
//! the same bits), and the link word pointer `next` (an arena word, or null).
//! The stub of pc_sprite_packet records its six arguments (texture page 0 is
//! checked there) and returns a packet in a different arena block than the
//! cursor's, so the new cursor (the packet + 6 words) and the returned
//! pointer are told apart from the old cursor.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setSprite));

pub const classes = [_][:0]const u8{ "cursor-set", "cursor-null" };

var packet_block: [*c]c_uint = null;
var cursor_null: bool = false;

const edges = [_]c_int{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x80, 0xFF, 0x100, 0x00FF00FF };

pub fn prepare(ctx: *zd.Ctx) Args {
    const cursor: *u32 = @ptrFromInt(0x1F800000);
    const block = ctx.allocBytes(0x40, 16);
    for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
    cursor_null = ctx.chance(6);
    cursor.* = if (cursor_null) 0 else @intCast(@intFromPtr(block));
    // The scratchpad words around the cursor hold other aligned guest
    // pointers (never random bits: a misread word must give a different
    // packet pointer, not an unaligned one that panics the port).
    for ([_]usize{ 0x1F800004, 0x1F800008, 0x1F80000C }) |word| {
        const other = ctx.allocBytes(0x40, 16);
        @as(*u32, @ptrFromInt(word)).* = @intCast(@intFromPtr(other));
    }
    const packet = ctx.allocBytes(0x40, 16);
    for (packet[0..0x40]) |*byte| byte.* = ctx.int(u8);
    packet_block = @ptrCast(@alignCast(packet));
    const next: [*c]c_uint = if (ctx.chance(5)) null else @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges), ctx.pick(c_int, &edges), next };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(cursor_null);
}

pub const stubs = struct {
    pub fn pc_sprite_packet(ctx: *zd.Ctx, packet: anytype, next: anytype, brightness: anytype, xy: anytype, wh: anytype, page: anytype) [*c]c_uint {
        _ = ctx;
        _ = packet;
        _ = next;
        _ = brightness;
        _ = xy;
        _ = wh;
        _ = page;
        return packet_block;
    }
};
