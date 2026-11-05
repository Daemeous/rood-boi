//! zig_diff spec: func_80098D6C -> tint_room_packets.tintRoomPackets (D2: pc/src/room_geometry.c).
//! Inputs: a run of packets in the arena (9-word triangles, 12-word quads,
//! the native room's 20-byte translucent markers, which the
//! render_translucent_marker stub recognises by address, and an unknown
//! packet kind that ends the pass), `end` after the last one, signed colour
//! offsets (edges +-127, -128, so both clamps), a fade (zero, or any short,
//! negative included) and flags with bit 1 set or clear (8 more bytes per
//! packet). The GTE colour fade (CTC2/MTC2/doCOP2/MFC2) and the
//! pc_room_native_tint hook are recording stubs.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").tintRoomPackets));

pub const classes = [_][:0]const u8{ "triangles", "quads", "marker-first", "unknown-packet", "empty", "no-fade", "extra-bytes" };

var scenario: usize = 0;
var marker_addresses: [8]usize = undefined;
var marker_count: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    scenario = ctx.range(usize, 0, classes.len - 1);
    marker_count = 0;
    const memory = ctx.allocBytes(0x40 * 8, 16);
    for (memory[0 .. 0x40 * 8]) |*byte| byte.* = ctx.int(u8);
    const params = ctx.allocBytes(8, 8);
    for (params[0..3]) |*byte| byte.* = @bitCast(ctx.pick(i8, &.{ 127, -128, 0, 1, -1 }));
    const fade: i16 = if (scenario == 5) 0 else ctx.edge(i16, &.{ 1, 0x1000, 0x7FFF, -1, -0x8000, ctx.range(i16, 1, 0x1000), ctx.range(i16, -0x1000, -1) });
    std.mem.writeInt(i16, params[4..6], fade, .little);
    const flags: c_int = if (scenario == 6) ctx.edge(c_int, &.{ 2, 3, -1 }) else ctx.edge(c_int, &.{ 0, 1, 2, 3 });
    const extra: usize = if (flags & 2 != 0) 8 else 0;

    const packets: usize = if (scenario == 4) 0 else ctx.range(usize, 1, 5);
    const unknown_at: usize = if (scenario == 3) ctx.range(usize, 0, packets - 1) else packets;
    var position: usize = 0;
    var i: usize = 0;
    while (i < packets) : (i += 1) {
        const words: [*]c_uint = @ptrCast(@alignCast(memory + position));
        var kind: u8 = switch (scenario) {
            0, 5 => 0,
            1 => 1,
            else => ctx.range(u8, 0, 2),
        };
        if (scenario == 2 and i == 0) kind = 2;
        if (kind == 2 and (i == packets - 1 or i >= 4)) kind = 0; // a marker is followed by a packet
        if (i == unknown_at) kind = 3;
        const low = ctx.int(c_uint) & 0xFFFFFF;
        switch (kind) {
            0 => {
                words[0] = low | (9 << 24);
                position += 0x28 + extra;
            },
            1 => {
                words[0] = low | (12 << 24);
                position += 0x34 + extra;
            },
            2 => {
                words[0] = low | (ctx.edge(c_uint, &.{ 9, 12, 3 }) << 24);
                marker_addresses[marker_count] = @intFromPtr(words);
                marker_count += 1;
                position += 20;
            },
            else => {
                words[0] = low | (ctx.edge(c_uint, &.{ 0, 5, 8, 10, 11, 13, 255 }) << 24);
                position += 0x28 + extra;
            },
        }
    }
    return .{ @ptrCast(@alignCast(memory)), @ptrCast(@alignCast(memory + position)), @ptrCast(params), flags };
}

pub fn classify(args: Args) usize {
    _ = args;
    return scenario;
}

pub const stubs = struct {
    pub fn render_translucent_marker(ctx: *zd.Ctx, words: anytype, list: anytype, first: anytype, entries: anytype) c_int {
        _ = .{ ctx, list, first, entries };
        for (marker_addresses[0..marker_count]) |address| {
            if (address == @intFromPtr(words)) return 1;
        }
        return 0;
    }
};
