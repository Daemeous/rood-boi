//! zig_diff spec: func_800D5260 -> func_800D5260.func_800D5260 (5BF94.c).
//! Inputs: a source object in the arena whose four words are random or set to
//! the edge values 0, -1 and the i32 extremes, or the global itself (the
//! self-copy alias case, a quarter of the runs). The C has no branch: it is a
//! whole-struct copy into the global, and the harness compares the global, the
//! source object and the return value after it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5260));

pub const classes = [_][:0]const u8{ "arena-source", "self-copy" };

const edges = [_]i32{ 0, -1, std.math.maxInt(i32), std.math.minInt(i32) };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(4)) return .{v.g.D_800F5620()};
    const src = ctx.alloc(v.D_800F5620_t);
    src.unk0 = ctx.pick(i32, &edges);
    src.unk4 = ctx.pick(i32, &edges);
    src.unk8 = ctx.pick(i32, &edges);
    src.unkC = ctx.pick(i32, &edges);
    return .{src};
}

pub fn classify(args: Args) usize {
    return if (args[0].? == v.g.D_800F5620()) 1 else 0;
}
