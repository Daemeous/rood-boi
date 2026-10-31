//! zig_diff spec: func_800D47F4 -> func_800D47F4.func_800D47F4 (5BF94.c).
//! Inputs: an arg0 object in the arena whose unkD1C.unk3C is zero, positive
//! or negative (the sign bit set or clear, the extremes included). The table
//! D_800EC324 has one entry in the view, so the index func_800D5170 returns
//! is always 0. Each func_800D5198 call returns any u16 and may overwrite
//! unk3C, so both the override and the shifted sum are reached.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D47F4));

pub const classes = [_][:0]const u8{ "unk3C-zero", "unk3C-positive", "unk3C-negative" };

pub const stubs = struct {
    // Returns the table index: the table has one entry, so the index is 0.
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        if (ctx.value(u1) == 1) a.unkD1C.unk3C = ctx.value(i32);
        _ = ctx.value(u8);
        return 0;
    }

    pub fn func_800D5198(ctx: *zd.Ctx, p: anytype) u16 {
        const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        if (ctx.value(u1) == 1) a.unkD1C.unk3C = ctx.value(i32);
        return ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F53B8_t);
    const entry: ?*const anyopaque = if (ctx.chance(2)) null else @ptrCast(arg);
    v.g.D_800EC324()[0] = entry;
    arg.unkD1C.unk3C = ctx.pick(i32, &.{ 0, 0, 1, -1, std.math.maxInt(i32), std.math.minInt(i32) });
    return .{arg};
}

pub fn classify(args: Args) usize {
    const x = args[0].?.unkD1C.unk3C;
    if (x == 0) return 0;
    return if (x > 0) 1 else 2;
}
