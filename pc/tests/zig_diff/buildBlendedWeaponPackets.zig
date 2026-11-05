//! zig_diff spec: func_800B516C -> pc.build_blended_weapon_packets.buildBlendedWeaponPackets (D2: pc/src/weapon_packets.c, through its statics).
//! Inputs: three section totals (triangles, quads, mixed records: each 0 a
//! third of the time, else 1 to 5), 24 group-count halfwords (each window of 8
//! summing to no more than its section's total, so no group count is negative,
//! which would run the C past the arena), polygon records laid out in section
//! order with vertex offsets multiples of 4 below 0xC00 (the depth array is read
//! at the same offsets 0xC00 on), mixed records whose code byte is the quad code
//! 0x2C or another and whose flags (bits 16 on) are 0, 1, 3, 2, 0x10, 0x60, 0xC
//! or any halfword; the size byte of a mixed record is its advance (0x14 for the
//! quad code or a split record, else 0x10), so the records stay in step however
//! the stub of nclip falls. The depth words are 0 to 0x2200 (a quarter of the
//! time around 0x1F00, so a quad's four sum to just under, at and over 0x7C00),
//! and the stub of MFC2 gives register 24 (nclip) any sign or zero and register 7
//! (avsz3) 0 to 0x8FF, the MVMVA/OP registers (25 to 27) any value, so the split
//! diagonal goes either way. The object has a blend mode (byte 9: 0, 1-6, 7+) and
//! a CLUT mask (byte 0x64); the colour word is random. Packet cursor, ordering
//! table and texture/CLUT words are in the scratchpad.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").buildBlendedWeaponPackets));

pub const classes = [_][:0]const u8{ "empty", "opaque", "subtractive", "additive", "split-quad" };

var has_split = false;

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
    return 4 * ctx.range(u32, 0, 0x2ff);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    var totals: [3]u16 = undefined;
    for (&totals) |*t| t.* = if (ctx.chance(3)) 0 else ctx.range(u16, 1, 5);
    const object = ctx.allocBytes(0x80, 16);
    for (object[0..0x80]) |*byte| byte.* = ctx.int(u8);
    for (totals, 0..) |t, i| std.mem.writeInt(u16, object[2 + i * 2 ..][0..2], t, .little);
    object[9] = ctx.edge(u8, &.{ 0, 0, 1, 3, 6, 7, 0xA, 0xF, 0x10, 0x1F });
    object[0x64] = ctx.int(u8);

    const model = ctx.allocBytes(0x80, 16);
    for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
    for (0..3) |window| {
        var left = totals[window];
        for (0..8) |slot| {
            const n = if (ctx.chance(2)) 0 else ctx.range(u16, 0, left);
            left -= n;
            std.mem.writeInt(u16, model[(window * 8 + slot) * 2 ..][0..2], n, .little);
        }
    }

    const records = ctx.allocBytes(0x400, 16);
    for (records[0..0x400]) |*byte| byte.* = ctx.int(u8);
    has_split = false;
    var w: usize = 0;
    for (0..totals[0]) |_| {
        word(records, w + 1, offset(ctx) | offset(ctx) << 16);
        word(records, w + 2, offset(ctx) | (ctx.int(u32) & 0xFFFF0000));
        w += 4;
    }
    for (0..totals[1]) |_| {
        word(records, w + 1, offset(ctx) | offset(ctx) << 16);
        word(records, w + 2, offset(ctx) | offset(ctx) << 16);
        w += 5;
    }
    for (0..totals[2]) |_| {
        const code: u32 = ctx.edge(u32, &.{ 0x2c, 0x2c, 0x24, 0x25, 0x2d, 0 });
        const flags: u32 = ctx.pick(u32, &.{ 0, 1, 3, 2, 0x10, 0x60, 0xC, 0x1f }) & 0xFFFF;
        const split = flags & 3 == 3;
        const advance: u32 = if (code == 0x2c or split) 0x14 else 0x10;
        has_split = has_split or split;
        word(records, w, code | advance << 8 | flags << 16);
        word(records, w + 1, offset(ctx) | offset(ctx) << 16);
        word(records, w + 2, offset(ctx) | offset(ctx) << 16);
        w += advance / 4;
    }
    std.mem.writeInt(u32, model[0x3c..][0..4], @intCast(@intFromPtr(records)), .little);

    const projected = ctx.allocBytes(0x1800, 16);
    const words: [*]c_uint = @ptrCast(@alignCast(projected));
    for (0..0x300) |i| words[i] = ctx.int(c_uint);
    const near_limit = ctx.chance(4);
    for (0..0x300) |i| words[0x300 + i] = if (near_limit) 0x1eff + ctx.range(c_uint, 0, 4) else ctx.range(c_uint, 0, 0x2200);

    const packets = ctx.allocBytes(0x800, 16);
    const table = ctx.allocBytes(0x2200, 16);
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    scratch[0] = @intCast(@intFromPtr(packets) + 0x20);
    scratch[1] = @intCast(@intFromPtr(table) + 0x40);
    scratch[0x390 / 4] = ctx.int(u32);

    const color = ctx.allocBytes(16, 16);
    for (color[0..16]) |*byte| byte.* = ctx.int(u8);
    return .{ projected, object, color, model };
}

pub fn classify(args: Args) usize {
    const object = args[1];
    const any = std.mem.readInt(u16, object[2..4], .little) | std.mem.readInt(u16, object[4..6], .little) | std.mem.readInt(u16, object[6..8], .little);
    if (any == 0) return 0;
    if (has_split) return 4;
    const mode = object[9] & 0xF;
    return if (mode == 0) 1 else if (mode < 7) 2 else 3;
}
