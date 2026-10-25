//! zig_diff spec: func_800A0BA0 -> func_800A0BA0.func_800A0BA0 (30DB0.c).
//! Inputs: an index 0..16 into the D_800F4538 pointer table. prepare sets the
//! entry at the index (null or a fresh arena object with edge-biased unk5C0
//! and unk5C2, so the u16 fields cover 0, 0x7FFF, 0x8000, 0xFFFF and random
//! bytes). It also sets the entry at index & 7 to a valid object, so a port
//! that aliases the index reads a real object rather than random bytes. The
//! out-parameters are null only for indices below 8 with a null entry, where
//! the C never writes them. The C makes no calls and has no ordered
//! comparison, so no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0BA0));

pub const classes = [_][:0]const u8{ "null-entry-null-args", "null-entry-args", "set-entry" };

fn makeObj(ctx: *zd.Ctx) *v.D_800F4538_t {
    const obj = ctx.alloc(v.D_800F4538_t);
    obj.unk5C0 = ctx.pick(u16, &.{ 0, 0x7FFF, 0x8000, 0xFFFF });
    obj.unk5C2 = ctx.pick(u16, &.{ 0, 0x7FFF, 0x8000, 0xFFFF });
    return obj;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const tbl = v.g.D_800F4538();
    const idx = ctx.range(c_int, 0, 16);
    const i: usize = @intCast(idx);
    const k: usize = i & 7;
    const a1 = ctx.alloc(c_int);
    const a2 = ctx.alloc(c_int);

    const null_entry = ctx.chance(3);
    if (null_entry) {
        tbl[i] = null;
    } else {
        tbl[i] = makeObj(ctx);
    }
    // The aliased entry is always a valid object, so the aliasing mutant
    // reads real, different values rather than random bytes.
    if (k != i) tbl[k] = makeObj(ctx);

    // Null out-parameters only where the index is below 8 (so index & 7 is
    // the index itself and the aliasing mutant never writes through them).
    const args_null = null_entry and i < 8 and ctx.chance(2);
    return .{ idx, if (args_null) null else a1, if (args_null) null else a2 };
}

pub fn classify(args: Args) usize {
    const tbl = v.g.D_800F4538();
    if (tbl[@intCast(args[0])] != null) return 2;
    return if (args[1] == null) 0 else 1;
}
