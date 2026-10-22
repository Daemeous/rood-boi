//! zig_diff spec: func_800C1D84 -> func_800C1D84.func_800C1D84 (58578.c).
//! Inputs: D_800EB9B8 points at a D_800EB9B8_t in the arena. Its 24 unk990
//! entries are 0/1 (the C returns 1) or hold one value above 1 at a chosen
//! index (the C returns 0 at that index), with that value drawn from
//! 2, 3, 128 and 255 so the signed-char slip shows. The hit index covers the
//! first entry, the last entry, the middle and none. The callee-free C has no
//! stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C1D84));

pub const classes = [_][:0]const u8{ "all-small", "hit-first", "hit-last", "hit-middle" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = obj;

    const all_small = ctx.chance(3);
    // 24 means no entry is above 1.
    const hit: usize = if (ctx.chance(4)) 24 else ctx.range(usize, 0, 23);
    for (&obj.unk990, 0..) |*e, i| {
        e.unk0 = if (all_small or i != hit)
            ctx.range(u8, 0, 1)
        else
            ctx.pick(u8, &.{ 2, 3, 128, 255 });
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const obj = v.g.D_800EB9B8().*.?;
    for (&obj.unk990, 0..) |*e, i| {
        if (e.unk0 > 1) {
            if (i == 0) return 1;
            if (i == 23) return 2;
            return 3;
        }
    }
    return 0;
}
