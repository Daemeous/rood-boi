//! zig_diff spec: func_800A07B4 -> func_800A07B4.func_800A07B4 (30DB0.c).
//! Inputs: an index 0-16 into D_800F4538 (all 17 entries set by prepare), each
//! entry either null or a random D_800F4538_t in the arena. The out-parameter
//! is null only where the entry is null (the C never tests it, but writes it on
//! the valid path). Both branches are covered: the -1 return for a null entry,
//! and the 4-byte copy of unk0.unk58 with random bytes in the source and in the
//! out-parameter. The C has no callees and no ordered comparisons.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A07B4));

pub const classes = [_][:0]const u8{ "null-entry", "valid-entry" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    for (table) |*entry| {
        entry.* = if (ctx.chance(3)) null else ctx.alloc(v.D_800F4538_t);
    }
    const idx = ctx.range(c_int, 0, 16);
    const null_entry = table[@intCast(idx)] == null;
    const out: ?*v.D_800F4538_unk58_2 = if (null_entry and ctx.chance(2))
        null
    else
        ctx.alloc(v.D_800F4538_unk58_2);
    return .{ idx, out };
}

pub fn classify(args: Args) usize {
    return if (v.g.D_800F4538()[@intCast(args[0])] == null) 0 else 1;
}
