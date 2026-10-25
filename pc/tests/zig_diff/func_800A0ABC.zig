//! zig_diff spec: func_800A0ABC -> func_800A0ABC.func_800A0ABC (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (the table's length; the C does not
//! check it), and the table entry for that index either null or an arena
//! D_800F4538_t with random bytes. The C calls nothing. For a non-null entry
//! it stores 0 to unk1840, 0 to unk1846 and 0 to the unk5AC_15 bit, leaving
//! every other byte alone. Slot 0 is kept apart so a guard or index slip shows.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0ABC));

pub const classes = [_][:0]const u8{ "null-entry", "live-slot-0", "live-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = @intCast(ctx.range(u8, 0, 16));
    // The entry the C reads must be set here: null for the NULL test, else a live object.
    const table = v.g.D_800F4538();
    table[@as(usize, @intCast(idx))] = if (ctx.chance(4)) null else ctx.alloc(v.D_800F4538_t);
    return .{idx};
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    if (v.g.D_800F4538()[idx] == null) return 0;
    return if (idx == 0) 1 else 2;
}
