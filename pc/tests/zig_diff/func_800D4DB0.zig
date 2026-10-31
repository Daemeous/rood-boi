//! zig_diff spec: func_800D4DB0 -> func_800D4DB0.func_800D4DB0 (5BF94.c).
//! Inputs: an arg0 object in the arena whose previous link is null or set, a
//! random unk10 table (edges 0-5 and 255 plus random bytes), and the
//! D_800F569C tables: block9 ids from {0-5, 255} and random, block3 null or
//! with a count that is negative, a small value, or large, sometimes equal to
//! a block9 id (so `id < count` sees equal sides). func_800D5170 returns small
//! indices most of the time and any byte otherwise. The id is a byte widened
//! to int, so it never has its sign bit set; that flag cannot be reached.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4DB0));

pub const classes = [_][:0]const u8{ "previous-set", "no-block3", "count-negative", "count-small", "count-large" };

fn asView(p: anytype) *v.D_800F53B8_t {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    /// Returns the index the C looks up in arg0->unk10. Small indices most of
    /// the time (so index < 4 and index == 4 both occur); sometimes it also
    /// rewrites the table the C reads back.
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        const a = asView(p);
        if (ctx.value(u1) == 1) a.unk10[ctx.value(u8) % 4] = ctx.value(u8);
        if (ctx.value(u1) == 1) return ctx.value(u8);
        return ctx.value(u8) % 6;
    }

    /// Void callee: it may change fields of arg0 (not read back by the C,
    /// but compared as state).
    pub fn func_800D55A4(ctx: *zd.Ctx, p: anytype, index: anytype, flag: anytype) void {
        _ = index;
        _ = flag;
        const a = asView(p);
        a.unkA = ctx.value(u16);
        if (ctx.value(u1) == 1) a.unk8 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F53B8_t);
    const block9 = ctx.alloc(v.pFileBlock9);
    const block3 = ctx.alloc(v.pFileBlock3);
    const top = ctx.alloc(v.D_800F569C_t);

    psx.put(&arg.previous, if (ctx.chance(2)) null else ctx.alloc(v.D_800F53B8_t));

    for (&arg.unk10) |*e| e.* = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 255 });

    for (&block9.unk16) |*e| e.* = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 255 });
    psx.put(&top.block9Data, block9);

    block3.count = ctx.pick(i32, &.{ -1, 0, 1, 2, 3, 4, 5, 6, 255, 256, std.math.minInt(i32), std.math.maxInt(i32) });
    if (ctx.chance(3)) block3.count = @as(i32, block9.unk16[ctx.range(usize, 0, 3)]);
    psx.put(&top.block3Data, if (ctx.chance(5)) null else block3);

    v.g.D_800F569C().* = top;
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0].?;
    if (psx.get(arg.previous) != null) return 0;
    const top = v.g.D_800F569C().*.?;
    const b3 = psx.get(top.block3Data) orelse return 1;
    if (b3.count < 0) return 2;
    if (b3.count <= 5) return 3;
    return 4;
}
