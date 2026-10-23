//! zig_diff spec: func_80092EDC -> func_80092EDC.func_80092EDC (2842C.c).
//! Inputs: an func_80092F74_t in the arena with unk8 (the sign test) drawn from
//! 0, +-1, the i16 extremes and random values, unkC/unk10/unk14 from the
//! division edges (negative values not a multiple of 16, INT_MIN/INT_MAX) and
//! random values, and vx/vy/vz from sign edges and random values. Its unk0
//! points at an 8-word arena block, so the C's stores land in memory the
//! harness owns. Three classes: "negative" (unk8 < 0, nothing written),
//! "plain" (unk8 >= 0, separate target block) and "aliased" (unk8 >= 0 and
//! the target is the object itself, so the stores to temp[2], temp[3] and
//! temp[4] overwrite unk8, unkC and unk10 that later statements read).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092EDC));

pub const classes = [_][:0]const u8{ "negative", "plain", "aliased" };

const edges16 = [_]i16{ 0, -1, 1, 15, -15, 16, -16, 32767, -32768 };
const edges32 = [_]i32{ 0, -1, 1, 15, -15, 16, -16, 17, -17, 2147483647, -2147483648 };

/// Set by prepare: 0 negative, 1 plain, 2 aliased. Read by classify.
var mode: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_80092F74_t);
    obj.unk8 = ctx.pick(i16, &edges16);
    obj.unkC = ctx.pick(i32, &edges32);
    obj.unk10 = ctx.pick(i32, &edges32);
    obj.unk14 = ctx.pick(i32, &edges32);
    obj.unk1C.vx = ctx.pick(i16, &edges16);
    obj.unk1C.vy = ctx.pick(i16, &edges16);
    obj.unk1C.vz = ctx.pick(i16, &edges16);

    if (ctx.chance(4)) {
        // Aliased: the target is the object itself, so the C's stores
        // overwrite fields it still reads.
        mode = 2;
        obj.unk8 = ctx.range(i16, 0, 32767);
        psx.put(&obj.unk0, @as(*i32, @ptrCast(obj)));
    } else {
        const words = ctx.alloc([8]i32);
        psx.put(&obj.unk0, @as(*i32, @ptrCast(words)));
        mode = if (obj.unk8 < 0) 0 else 1;
    }
    return .{obj};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
