//! zig_diff spec: func_800BB504 -> func_800BB504.func_800BB504 (4A0A8.c).
//! Inputs: a two-byte buffer whose byte 1 has bit 7 set or clear (both
//! signs of the flag test), every low-2-bit index (0-3), and a
//! D_800F4C38 table whose entries are each null or pointing into the
//! buffer, so the "!= 0" result takes both values. The unused short arg1
//! is random. The C calls nothing, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB504));

pub const classes = [_][:0]const u8{ "bit7-set-clears-entry", "bit7-clear-entry-null", "bit7-clear-entry-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(2, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.int(u8);
    if (ctx.chance(2)) {
        buf[1] |= 0x80;
    } else {
        buf[1] &= 0x7F;
    }
    const tbl = v.g.D_800F4C38();
    for (0..5) |i| {
        tbl[i] = if (ctx.chance(2)) null else &buf[0];
    }
    return .{ buf, ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    if ((arg0[1] & 0x80) != 0) return 0;
    const tbl = v.g.D_800F4C38();
    const idx: usize = @as(usize, arg0[1] & 3);
    return if (tbl[idx] != null) 2 else 1;
}
