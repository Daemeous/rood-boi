//! zig_diff spec: func_800BAB14 -> func_800BAB14.func_800BAB14 (4A0A8.c).
//! Inputs: arg0 is a 2-byte arena block whose byte 1 is the selector a0 (the C
//! widens it to a short, so it is always 0..255: no sign bit is ever set, and
//! the zero tests see nonzero values only). The selector is 0xFF (one in
//! eight), a value with bit 3 set (the memcpy path, any low three bits), or 0..7
//! (the else path). Indices 8..255 with bit 3 clear are left out: the C would
//! read D_800F4BC0 past its 8 entries. The eight table entries are set by
//! prepare, null one time in three, otherwise pointing at arena blocks; the
//! D_800F4C48 pointer is an arena block. The callees' return values and their
//! writes are never read back by the C, so their default recording stubs are
//! enough.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAB14));

pub const classes = [_][:0]const u8{ "sel-ff", "bit3-entry-null", "bit3-entry-set", "else-entry-null", "else-entry-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.allocBytes(2, 1);
    const sel: u8 = if (ctx.chance(8))
        0xFF
    else if (ctx.chance(2))
        ctx.int(u8) | 8
    else
        ctx.range(u8, 0, 7);
    arg[1] = sel;

    const tbl = v.g.D_800F4BC0();
    var i: usize = 0;
    while (i < 8) : (i += 1) {
        const blk: ?*u8 = @ptrCast(ctx.allocBytes(16, 16));
        tbl[i] = if (ctx.chance(3)) null else blk;
    }

    v.g.D_800F4C48().* = @ptrCast(ctx.allocBytes(0x80, 16));
    return .{arg};
}

pub fn classify(args: Args) usize {
    const sel = args[0][1];
    if (sel == 0xFF) return 0;
    const tbl = v.g.D_800F4BC0();
    if ((sel & 8) != 0) return if (tbl[sel & 7] == null) 1 else 2;
    return if (tbl[sel] == null) 3 else 4;
}
