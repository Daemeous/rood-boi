//! zig_diff spec: func_800D7A90 -> func_800D7A90.func_800D7A90 (6F014.c).
//! Inputs: a D_800F569C_t in the arena whose block2Data points into a 4 KB
//! arena block. The element at arg0 + 2 is set to an edge value (0, 1, 0x7FFF,
//! 0x8000, 0xFFFF or random), so the offset's top bit is sometimes set.
//! arg0 runs from -200 to 200, with the edges -200, -3..2 and 200 chosen often.
//! Negative arg0 reaches below the block's base, which is the sign edge the C
//! has. The C has no branches and no calls, so the classes are the sign of arg0.
//! arg0 + 2 is kept far from INT_MAX, since that overflow is undefined in C.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7A90));

pub const classes = [_][:0]const u8{ "index-nonnegative", "index-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F569C_t);
    const bytes = ctx.allocBytes(4096, 16);
    const base: [*]u16 = @ptrCast(@alignCast(bytes + 2048));
    psx.put(&obj.block2Data, base);
    v.g.D_800F569C().* = obj;

    const edges = [_]c_int{ -200, -3, -2, -1, 0, 1, 2, 200 };
    const arg0: c_int = if (ctx.chance(2))
        edges[ctx.range(usize, 0, edges.len - 1)]
    else
        ctx.range(c_int, -200, 200);
    psx.at(base, arg0 + 2).* = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
    return .{arg0};
}

pub fn classify(args: Args) usize {
    return if (args[0] < 0) 1 else 0;
}
