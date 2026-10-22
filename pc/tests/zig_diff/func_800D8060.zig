//! zig_diff spec: func_800D8060 -> func_800D8060.func_800D8060 (6F014.c).
//! Inputs: a chain of p_file_t blocks in the arena: 0-5 blocks, then a zero
//! terminator (or the terminator alone). Each block's high half is one of the
//! eleven dispatched types or an unmatched one (0x6, 0x7, 0xE, 0xF, 0xFF),
//! with the sign bit set about one time in five. Sizes are multiples of 4
//! from 8 up to 0x1008, so the low-16-bit advance crosses 0xFFF. Every block
//! is at least 8 bytes, so a stub writing its data stays inside the block.
//! The parse callees are recording stubs; _parsePfileBlock10 also writes its
//! block's data, which the loop never reads back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D8060));

pub const classes = [_][:0]const u8{ "empty", "unmatched", "matched", "negative", "big-size" };

const sizes = [_]u32{ 8, 12, 16, 20, 28, 64, 0x1004, 0x1008, 0xFF8 };
const dispatched = [_]u32{ 0x10000, 0x20000, 0x30000, 0x40000, 0x50000, 0x80000, 0x90000, 0xA0000, 0xB0000, 0xC0000, 0xD0000 };
const unmatched = [_]u32{ 0x60000, 0x70000, 0xE0000, 0xF0000, 0x00FF0000 };

pub const stubs = struct {
    pub fn _parsePfileBlock10(ctx: *zd.Ctx, p: anytype) void {
        const w: *align(1) u32 = @ptrFromInt(@intFromPtr(p));
        w.* = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    var metas: [5]u32 = undefined;
    var count: usize = 0;
    if (!ctx.chance(8)) {
        count = ctx.range(usize, 1, 5);
        for (metas[0..count]) |*m| {
            const ty = if (ctx.chance(6))
                unmatched[ctx.range(usize, 0, unmatched.len - 1)]
            else
                dispatched[ctx.range(usize, 0, dispatched.len - 1)];
            const sign: u32 = if (ctx.chance(5)) 0x8000_0000 else 0;
            m.* = ty | sign | sizes[ctx.range(usize, 0, sizes.len - 1)];
        }
    }
    var total: usize = 4;
    for (metas[0..count]) |m| total += @as(usize, m & 0xFFFF);
    const base = ctx.allocBytes(total, 16);
    var off: usize = 0;
    for (metas[0..count]) |m| {
        const p: *align(1) u32 = @ptrCast(base + off);
        p.* = m;
        off += @as(usize, m & 0xFFFF);
    }
    const end: *align(1) u32 = @ptrCast(base + off);
    end.* = 0;
    const hdr: *v.p_file_t = @ptrCast(@alignCast(base));
    return .{hdr};
}

pub fn classify(args: Args) usize {
    const m: u32 = @bitCast(args[0].?.meta);
    if (m == 0) return 0;
    if ((m & 0xFFFF) > 0xFFF) return 4;
    if ((m & 0x8000_0000) != 0) return 3;
    return switch (m & 0xFFFF_0000) {
        0x10000, 0x20000, 0x30000, 0x40000, 0x50000, 0x80000, 0x90000, 0xA0000, 0xB0000, 0xC0000, 0xD0000 => 2,
        else => 1,
    };
}
