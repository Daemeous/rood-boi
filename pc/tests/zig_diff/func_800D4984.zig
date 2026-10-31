//! zig_diff spec: func_800D4984 -> func_800D4984.func_800D4984 (5BF94.c).
//! The C forwards its argument to func_800D0D08 and returns 1. The inputs
//! cover a null argument and a random D_800F53B8_t in the arena. The
//! callee is a recording stub that may overwrite the object's fields (the
//! C never reads them back, but the state compare sees them) and it draws
//! its writes from ctx.value, so both runs write the same bytes. The C has
//! no branches of its own, so the coverage lanes are met by the two classes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4984));

pub const classes = [_][:0]const u8{ "null-arg", "object-arg" };

pub const stubs = struct {
    pub fn func_800D0D08(ctx: *zd.Ctx, p: anytype) void {
        if (p == null) return;
        const obj: *v.D_800F53B8_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) obj.unk8 = ctx.value(u8);
        if (ctx.value(u1) == 1) obj.unkA = ctx.value(u16);
        if (ctx.value(u1) == 1) obj.unk10[0] = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F53B8_t);
    return .{if (ctx.chance(4)) null else obj};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
