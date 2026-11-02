//! zig_diff spec: func_800BA800 -> func_800BA800.func_800BA800 (4A0A8.c).
//! Inputs: a 8-byte buffer in the arena whose byte 1 selects one of the six
//! D_800E9B34 entries (0-5, the table's own length, so no read leaves the
//! table), and random bytes 0, 2, 3 and 4 (byte 3 covers both signs for the
//! (signed char) conversion), a random table, and a random arg1 that the C
//! never reads. The callee func_80045C74 is void and the C reads nothing back
//! from it, so the recording stub needs no behaviour. Classes split the
//! selector into low (0-2) and high (3-5) table entries.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA800));

pub const classes = [_][:0]const u8{ "index-low", "index-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(8, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5 });
    buf[2] = ctx.int(u8);
    buf[3] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    buf[4] = ctx.int(u8);
    buf[5] = ctx.int(u8);
    const table = v.g.D_800E9B34();
    for (0..6) |i| table[i] = ctx.int(i32);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const idx = args[0][1];
    return if (idx < 3) 0 else 1;
}
