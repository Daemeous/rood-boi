//! zig_diff spec: func_800B71F4 -> func_800B71F4.func_800B71F4 (4A0A8.c).
//! Inputs: the unk3 bytes of the three D_800F4B30 entries, biased to all zero,
//! exactly one of entries 0, 1 or 2 nonzero, or each byte random (any value,
//! sign bit included). arg0 is null or an arena block (the C never reads it),
//! and arg1 is any short (also unused by the C).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B71F4));

pub const classes = [_][:0]const u8{ "all-zero", "only-0", "only-1", "only-2", "several" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = v.g.D_800F4B30();
    const mode = ctx.range(u8, 0, 4);
    for (0..3) |i| {
        t[i].unk3 = switch (mode) {
            0 => 0,
            1, 2, 3 => if (@as(u8, @intCast(i)) + 1 == mode) ctx.range(u8, 1, 255) else 0,
            else => ctx.int(u8),
        };
    }
    const block = ctx.allocBytes(16, 16);
    return .{ if (ctx.chance(4)) null else block, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    const t = v.g.D_800F4B30();
    var n: usize = 0;
    var which: usize = 0;
    for (0..3) |i| {
        if (t[i].unk3 != 0) {
            n += 1;
            which = i;
        }
    }
    if (n == 0) return 0;
    if (n > 1) return 4;
    return which + 1;
}
