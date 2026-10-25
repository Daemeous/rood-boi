//! zig_diff spec: func_8009D854 -> func_8009D854.func_8009D854 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (the table's 17 entries, the
//! in-array indices the C reads), with the entry either NULL or a
//! D_800F4538_t object in the arena whose unk5AC_18 bit is random. The C
//! sets that bit to 1 when the entry is non-null, and does nothing when it
//! is null. Classes: null entry, object with the bit clear, object with
//! the bit already set. No stubs: the C makes no calls.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D854));

pub const classes = [_][:0]const u8{ "entry-null", "bit-clear", "bit-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: i32 = ctx.range(i32, 0, 16);
    const tbl = v.g.D_800F4538();
    if (ctx.chance(4)) {
        tbl[@intCast(idx)] = null;
    } else {
        const obj = ctx.alloc(v.D_800F4538_t);
        psx.setBits(obj, "unk5AC_18", ctx.int(u1));
        tbl[@intCast(idx)] = obj;
    }
    return .{@as(c_int, idx)};
}

pub fn classify(args: Args) usize {
    const tbl = v.g.D_800F4538();
    const obj = tbl[@intCast(args[0])] orelse return 0;
    return if (psx.getBits(obj, "unk5AC_18") == 1) 2 else 1;
}
