//! zig_diff spec: func_800B5C30 -> pc.build_blended_actor_packets.buildBlendedActorPackets (D2: pc/src/actor_packets.c).
//! Inputs: three section totals (triangles, quads, mixed records: each 0 a
//! third of the time, else 1 to 5), 24 group-count halfwords (each window of
//! 8 summing to no more than the smallest nonzero total, so no group count is
//! negative, which the C reports with pc_unimplemented), polygon records laid
//! out in section order with valid vertex offsets (multiples of 4 below
//! 0xC00), mixed records of the quad code 0x3C or another, a stride byte
//! equal to the record's size, and flags (0, 1, 0x10, 0x11, 0x60, any). The
//! projected depth words are 0 to 0x2200, so quad depths straddle 0x7C0; the
//! stub of MFC2 gives register 24 (nclip) any sign and register 7 (avsz3)
//! 0 to 0x8FF. The object has a blend mode (byte 9: 0, 1-6, 7+) and a CLUT
//! mask (byte 0x64); the colour word is random. Packet cursor, ordering table
//! and texture word are in the scratchpad. A quarter of the time the last
//! mixed record's stride byte is invalid (under 24, or not a multiple of 4):
//! when that record is culled the C reports pc_unimplemented, whose stub
//! returns, and moves on by the stride; nothing reads the records after it.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").buildBlendedActorPackets));

pub const classes = [_][:0]const u8{ "empty", "plain", "blended" };

/// The C and the port report the unimplemented paths with their own literal.
pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub const stubs = struct {
    pub fn MFC2(ctx: *zd.Ctx, reg: c_int) c_uint {
        const value = ctx.value(c_uint);
        // The boundaries of `nclip <= 0` and `depth >= 0x7c0` come up a quarter of the time.
        if (reg == 24 and value % 4 == 0) return 0;
        if (reg == 7 and value % 4 == 0) return 0x7bf + (value >> 2) % 3;
        return if (reg == 7) value % 0x900 else value;
    }
};

fn word(buffer: [*]u8, index: usize, value: u32) void {
    std.mem.writeInt(u32, buffer[index * 4 ..][0..4], value, .little);
}

fn offset(ctx: *zd.Ctx) u32 {
    return 4 * ctx.range(u32, 0, 0x30);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    var totals: [3]u16 = undefined;
    var limit: u16 = 5;
    for (&totals) |*t| {
        t.* = if (ctx.chance(3)) 0 else ctx.range(u16, 1, 5);
        if (t.* != 0) limit = @min(limit, t.*);
    }
    const object = ctx.allocBytes(0x80, 16);
    for (object[0..0x80]) |*byte| byte.* = ctx.int(u8);
    for (totals, 0..) |t, i| std.mem.writeInt(u16, object[2 + i * 2 ..][0..2], t, .little);
    object[9] = ctx.edge(u8, &.{ 0, 0, 1, 3, 6, 7, 0xA, 0xF, 0x10, 0x1F });
    object[0x64] = ctx.int(u8);

    const model = ctx.allocBytes(0x80, 16);
    for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
    for (0..3) |window| {
        var left = limit;
        for (0..8) |slot| {
            const n = if (ctx.chance(2)) 0 else ctx.range(u16, 0, left);
            left -= n;
            std.mem.writeInt(u16, model[(window * 8 + slot) * 2 ..][0..2], n, .little);
        }
    }

    const records = ctx.allocBytes(0x400, 16);
    for (records[0..0x400]) |*byte| byte.* = ctx.int(u8);
    var w: usize = 0;
    for (0..totals[0]) |_| {
        word(records, w, offset(ctx) | offset(ctx) << 16);
        word(records, w + 1, offset(ctx) | (ctx.int(u32) & 0xFFFF0000));
        w += 6;
    }
    for (0..totals[1]) |_| {
        word(records, w, offset(ctx) | offset(ctx) << 16);
        word(records, w + 1, offset(ctx) | offset(ctx) << 16);
        w += 8;
    }
    for (0..totals[2]) |record| {
        const quad = ctx.chance(2);
        word(records, w, offset(ctx) | offset(ctx) << 16);
        word(records, w + 1, offset(ctx) | (if (quad) offset(ctx) << 16 else ctx.int(u32) & 0xFFFF0000));
        const code: u32 = if (quad) 0x3C else ctx.edge(u32, &.{ 0x34, 0x36, 0x3D, 0 });
        word(records, w + 2, code << 24 | (ctx.int(u32) & 0xFFFFFF));
        var stride: u32 = if (quad) 32 else 24;
        if (record + 1 == totals[2] and ctx.chance(4)) stride = ctx.edge(u32, &.{ 0, 8, 20, 23, 25, 26, 27, 30, 255 });
        word(records, w + 3, stride << 24 | (ctx.int(u32) & 0xFFFFFF));
        const flags = ctx.pick(u32, &.{ 0, 1, 0x10, 0x11, 0x60, 0x0C });
        word(records, w + 4, flags << 24 | (ctx.int(u32) & 0xFFFFFF));
        w += if (quad) 8 else 6;
    }
    std.mem.writeInt(u32, model[0x3c..][0..4], @intCast(@intFromPtr(records)), .little);

    const projected: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(0x1800, 16)));
    for (0..0x300) |i| projected[i] = ctx.int(c_uint);
    // Half the time the depth words sit around 0x1F00, so a quad's four sum to
    // just under, at and over 0x7C00 (depth 0x7C0 after the shift).
    const near_limit = ctx.chance(2);
    for (0..0x40) |i| projected[0x300 + i] = if (near_limit) 0x1eff + ctx.range(c_uint, 0, 4) else ctx.range(c_uint, 0, 0x2200);

    const packets = ctx.allocBytes(0x800, 16);
    const table = ctx.allocBytes(0x2200, 16);
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    scratch[0] = @intCast(@intFromPtr(packets) + 0x20);
    scratch[1] = @intCast(@intFromPtr(table) + 0x40);
    scratch[0x390 / 4] = ctx.int(u32);

    const color: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    color[0] = ctx.int(c_uint);
    return .{ projected, object, color, model };
}

pub fn classify(args: Args) usize {
    const object = args[1];
    const any = std.mem.readInt(u16, object[2..4], .little) | std.mem.readInt(u16, object[4..6], .little) | std.mem.readInt(u16, object[6..8], .little);
    if (any == 0) return 0;
    return if (object[9] & 0xF != 0) 2 else 1;
}
