//! zig_diff spec: func_8009DBDC -> func_8009DBDC.func_8009DBDC (30DB0.c).
//! Inputs: arg0 a slot of D_800F4538 (0..16), that slot either null or an
//! arena D_800F4538_t whose unk5B1 selects one of the 12 D_800E9090 entries;
//! arg1 any int around 0, the sign edges and the INT_MAX wrap of the sum.
//! The callee func_800AE6C0 is a recording stub that sometimes rewrites the
//! actor's unk5BC and unk5B1 (the C never rereads either).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009DBDC));

pub const classes = [_][:0]const u8{ "null-slot", "offset-negative", "offset-near-max", "offset-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = ctx.range(c_int, 0, 16);
    const actor = ctx.alloc(v.D_800F4538_t);
    // The D_800E9090 table has 12 entries: the index the C reads stays below it.
    actor.unk5B1 = ctx.range(u8, 0, 11);
    const table = v.g.D_800F4538();
    if (ctx.chance(4)) {
        table.*[@intCast(idx)] = null;
    } else {
        table.*[@intCast(idx)] = actor;
    }
    const offset: c_int = ctx.pick(c_int, &.{ 0, 1, -1, 255, -256, 0x7FFFFFFF, -0x80000000 });
    return .{ idx, offset };
}

pub fn classify(args: Args) usize {
    const t = v.g.D_800F4538().*[@intCast(args[0])];
    if (t == null) return 0;
    if (args[1] < 0) return 1;
    if (args[1] > 0x7FFFFF00) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn func_800AE6C0(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        const t: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) t.unk5BC = ctx.value(i16);
        if (ctx.value(u1) == 1) t.unk5B1 = ctx.value(u8);
    }
};
