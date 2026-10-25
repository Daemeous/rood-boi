//! zig_diff spec: func_800A071C -> func_800A071C.func_800A071C (30DB0.c).
//! Inputs: an index 0..16 into the 17-entry D_800F4538 pointer table, every
//! table entry set to null, a shared object or a second object, so the C
//! reads a real entry at the index and never a random pointer. The output
//! struct is a fresh arena block, null where the C never dereferences it
//! (an empty entry), or an alias into the entry's own bytes at unk6EE..unk6F0,
//! so the three stores overlap the three reads and their order is visible.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A071C));

pub const classes = [_][:0]const u8{ "null-entry", "separate-out", "out-aliases-entry" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const idx = ctx.range(c_int, 0, 16);
    const obj = ctx.alloc(v.D_800F4538_t);
    const other = ctx.alloc(v.D_800F4538_t);

    // Every entry holds a valid object or null: a mutated index reads a
    // neighbour entry, and it must not fault.
    for (table) |*slot| {
        slot.* = if (ctx.chance(2)) null else if (ctx.chance(2)) obj else other;
    }

    if (ctx.chance(4)) {
        table[@intCast(idx)] = null;
        const out: ?*v.D_800F4538_unk58_2 = if (ctx.chance(2)) null else ctx.alloc(v.D_800F4538_unk58_2);
        return .{ idx, out };
    }

    table[@intCast(idx)] = if (ctx.chance(2)) obj else other;
    const entry = table[@intCast(idx)].?;

    if (ctx.chance(3)) {
        // arg1 = the entry's bytes at 0x6F0 - k: the stores land on the reads.
        const k = ctx.range(isize, 0, 2);
        return .{ idx, psx.byteAt(v.D_800F4538_unk58_2, entry, @as(isize, 0x6F0) - k) };
    }
    return .{ idx, ctx.alloc(v.D_800F4538_unk58_2) };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const entry = table[@intCast(args[0])] orelse return 0;
    const out = @intFromPtr(args[1].?);
    const base = @intFromPtr(entry);
    if (out >= base + 0x6EE and out <= base + 0x6F0) return 2;
    return 1;
}
