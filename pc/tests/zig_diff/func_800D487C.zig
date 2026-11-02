//! zig_diff spec: func_800D487C -> func_800D487C.func_800D487C (5BF94.c).
//! Inputs: a script whose effect list unkD1C.unk34 is null or an arena
//! block; func_800D46DC is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D487C));

pub const classes = [_][:0]const u8{ "no-list", "list" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    if (ctx.random.boolean()) psx.put(&s.unkD1C.unk34, null) else psx.put(&s.unkD1C.unk34, ctx.alloc(v.func_800D4910_t));
    return .{s};
}

pub fn classify(args: Args) usize {
    return if (psx.get(args[0].?.unkD1C.unk34) == null) 0 else 1;
}
