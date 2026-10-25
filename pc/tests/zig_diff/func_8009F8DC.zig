//! zig_diff spec: func_8009F8DC -> func_8009F8DC.func_8009F8DC (30DB0.c).
//! Inputs: a random index into D_800F4538 (the ends 0 and 16 and the middle),
//! an arena D_800F4538_t whose unkC54 has a random unk542 and a unk544 that
//! is either an arena byte or a random raw guest address (the C never
//! dereferences it, only round-trips it). The callee is a recording stub
//! that sometimes rewrites unk542 and unk544 and returns any int, so the
//! saved-and-restored fields, the stored pointer and the returned value are
//! all observable.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F8DC));

pub const classes = [_][:0]const u8{ "index-0", "index-16", "index-mid" };

fn asPtr(comptime T: type, p: anytype) *T {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    pub fn func_800AD714(ctx: *zd.Ctx, a: anytype, s: anytype, n: anytype) c_int {
        _ = a;
        _ = n;
        const sp = asPtr(v.D_800F4538_unkC54, s);
        // The callee may change the saved unk542 (the C restores it) ...
        if (ctx.value(u2) == 0) sp.unk542 = ctx.value(u16);
        // ... and the stored guest pointer (the C writes back what it read).
        if (ctx.value(u2) == 0) sp.unk544.raw = ctx.value(u32);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: u8 = if (ctx.chance(3)) ctx.edge(u8, &.{ 0, 16 }) else ctx.range(u8, 0, 16);
    const obj = ctx.alloc(v.D_800F4538_t);
    v.g.D_800F4538()[idx] = obj;
    obj.unkC54.unk542 = ctx.int(u16);
    if (ctx.chance(4)) {
        obj.unkC54.unk544.raw = ctx.int(u32);
    } else {
        psx.put(&obj.unkC54.unk544, @as(*u8, @ptrCast(ctx.allocBytes(4, 1))));
    }
    return .{@as(c_int, idx)};
}

pub fn classify(args: Args) usize {
    const i = args[0];
    if (i == 0) return 0;
    if (i == 16) return 1;
    return 2;
}
