//! zig_diff spec: func_800D4B90 -> func_800D4B90.func_800D4B90 (5BF94.c).
//! Inputs: a random D_800F53B8_t in the arena, its unk8 drawn from the edges
//! 0, 1, 254 and 255. The callee func_800D5170 returns the object's unk8
//! about half the time (so the comparison is equal often) and any byte
//! otherwise. func_800D46DC may rewrite unk9 and is passed the comparison
//! flag. The function has no branch, so the classes only name the unk8 edge.
//! func_800D5170 does not write unk8: the C reads unk8 and calls the callee
//! in an order the standard leaves open, so a write there would make the
//! spec depend on that order.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4B90));

pub const classes = [_][:0]const u8{ "unk8-zero", "unk8-one", "unk8-max", "unk8-other" };

pub const stubs = struct {
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        if (ctx.value(u1) == 1) return a.unk8;
        return ctx.value(u8);
    }

    pub fn func_800D46DC(ctx: *zd.Ctx, flag: anytype, p: anytype) void {
        _ = flag;
        const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        if (ctx.value(u1) == 1) a.unk9 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F53B8_t);
    obj.unk8 = ctx.edge(u8, &.{ 0, 1, 254, 255 });
    return .{obj};
}

pub fn classify(args: Args) usize {
    const u = args[0].?.unk8;
    if (u == 0) return 0;
    if (u == 1) return 1;
    if (u == 255) return 2;
    return 3;
}
