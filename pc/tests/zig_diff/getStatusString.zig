//! zig_diff spec: _getStatusString -> get_status_string.getStatusString (5BF94.c).
//! Inputs: arg0 whose shifted value (arg0 >> 5) has its lowest set bit at
//! each position 0-23 (the loop's returns, with random bits above and below),
//! or no bit in 0-23 (the loop runs to 24 and returns the empty string, with
//! random high bits 29-31 that the loop ignores). The status string table
//! is filled, with entries 57-80 (the indices the loop reads) set to edge
//! and in-range values.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getStatusString));

pub const classes = [_][:0]const u8{ "none-set", "bit-0", "bit-mid", "bit-23" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const strings = v.g.vs_battle_statusStrings();
    for (57..81) |i| {
        strings[i] = if (ctx.chance(2))
            ctx.edge(u16, &.{ 0, 141 })
        else
            ctx.range(u16, 0, 141);
    }

    const arg: c_uint = blk: {
        // No bit 0-23 of the shifted value: the empty string.
        if (ctx.chance(4)) {
            const high: u32 = ctx.edge(u32, &.{ 0, 0x2000_0000, 0x4000_0000, 0x8000_0000, 0xE000_0000 });
            break :blk high | ctx.range(u32, 0, 31);
        }
        // Lowest set bit of arg0 >> 5 at position k: bits 5..(k+4) clear,
        // bit k+5 set, random above it and in the low 5 bits.
        const k = ctx.range(u5, 0, 23);
        const shift: u5 = k + 5;
        const bit: u32 = @as(u32, 1) << shift;
        const low_mask: u32 = bit - 1;
        break :blk (ctx.int(u32) & ~low_mask) | bit | (ctx.int(u32) & 31);
    };
    return .{arg};
}

pub fn classify(args: Args) usize {
    const shifted = args[0] >> 5;
    var i: u5 = 0;
    while (i < 24) : (i += 1) {
        if (((shifted >> i) & 1) != 0) {
            if (i == 0) return 1;
            if (i == 23) return 3;
            return 2;
        }
    }
    return 0;
}
