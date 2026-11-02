//! zig_diff spec: func_800D27F0 -> func_800D27F0.func_800D27F0 (5BF94.c).
//! Inputs: the free list D_800F55F4 an arena node (never null: the C reads
//! through NULL after func_800CE644 there) whose next is null or another
//! node; the script's list empty or an arena node; the counts random.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D27F0));

pub const classes = [_][:0]const u8{ "empty-list", "list" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const node = ctx.alloc(v.func_800D2904_t);
    psx.put(&node.next, if (ctx.random.boolean()) ctx.alloc(v.func_800D2904_t) else null);
    v.g.D_800F55F4().* = node;
    const s = ctx.alloc(v.D_800F53B8_t);
    psx.put(&s.unk18, if (ctx.random.boolean()) ctx.alloc(v.func_800D2904_t) else null);
    return .{s};
}

pub fn classify(args: Args) usize {
    return if (psx.get(args[0].?.unk18) == null) 0 else 1;
}
