//! zig_diff spec: func_80093764 -> func_80093764.func_80093764 (2842C.c).
//! Inputs: arg0 across the 96-entry table D_800F1DD8, with the first entry, the
//! last entry and the middle entries drawn. Each entry's unk0 byte is random
//! and, in some runs, forced to an edge (0x00, 0x7F, 0x80, 0xFF) so that the
//! high bit (a signed-vs-unsigned read) is exercised both ways. The C has no
//! calls and no branches: it returns the byte at arg0, zero-extended.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093764));

pub const classes = [_][:0]const u8{ "first-entry", "last-entry", "middle-entry" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = if (ctx.chance(3))
        ctx.edge(c_int, &.{ 0, 95 })
    else
        ctx.range(c_int, 0, 95);
    const table = v.g.D_800F1DD8();
    table[@intCast(idx)].unk0 = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    return .{idx};
}

pub fn classify(args: Args) usize {
    const idx = args[0];
    if (idx == 0) return 0;
    if (idx == 95) return 1;
    return 2;
}
