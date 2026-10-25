//! zig_diff spec: func_8009DC8C -> func_8009DC8C.func_8009DC8C (30DB0.c).
//! Inputs: every entry of the 17-entry table D_800F4538 is null or one of two
//! arena D_800F4538_t objects (random bytes), so the C never dereferences random
//! table memory. arg0 is 0..16, the indices the table has. The C's only test is
//! the null entry; both classes occur. The stores clear six bitfields and bytes
//! of the object, and the spec compares every byte of it afterwards.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009DC8C));

pub const classes = [_][:0]const u8{ "null-entry", "object-entry" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.D_800F4538_t);
    const b = ctx.alloc(v.D_800F4538_t);
    const tab = v.g.D_800F4538();
    for (tab) |*e| {
        e.* = if (ctx.chance(4)) null else if (ctx.chance(2)) a else b;
    }
    const arg0 = ctx.range(c_int, 0, 16);
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const tab = v.g.D_800F4538();
    return if (tab[@intCast(args[0])] == null) 0 else 1;
}
