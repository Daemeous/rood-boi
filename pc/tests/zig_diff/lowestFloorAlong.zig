//! zig_diff spec: func_800E6A6C -> lowest_floor.lowestFloorAlong (D2: pc/src/battle_movement.c).
//! Inputs: two points whose coordinates are small, large, odd or extreme
//! ints (so the sixteenth step truncates both ways and wraps). The floor
//! sampler func_800E4318 is a recording stub returning 0, 15-bit edges
//! (0x3FFF, 0x4000, 0x8000, -1), small values, or any int, so heights
//! repeat (the `<=` keeps the later of two equal) and the 15-bit sign
//! extension is told from a 16-bit one.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").lowestFloorAlong));

pub const classes = [_][:0]const u8{ "sampled", "all-positive" };

// Chosen per iteration by prepare: every sampled height is positive, so the
// start value of the minimum (0) decides the result, and the running minimum
// (not a test against 0) is told apart.
var positive_only = false;

pub const stubs = struct {
    pub fn func_800E4318(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        const picks = [_]c_int{ 0, 0, 0x8000, -1, -2, 1, 0x3fff, 0x4000, 0x1ffff, -0x4000, 0xc000 };
        const r = ctx.value(u8);
        if (positive_only) return ([_]c_int{ 1, 2, 7, 0x3fff, 0x1234 })[r % 5];
        if (r < 200) return picks[r % picks.len];
        return ctx.value(c_int);
    }
};

fn coordinate(ctx: *zd.Ctx) c_int {
    if (ctx.chance(4)) return ctx.pick(c_int, &.{ 0x7FFFFFFF, -0x80000000, 0x40000000, -1 });
    return ctx.pick(c_int, &.{ 0, 1, 127, 128, 129, 0x1000, 0x7fff, -0x1000 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    positive_only = ctx.chance(4);
    return .{ coordinate(ctx), coordinate(ctx), coordinate(ctx), coordinate(ctx) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (positive_only) 1 else 0;
}
