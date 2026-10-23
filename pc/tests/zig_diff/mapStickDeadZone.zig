//! zig_diff spec: vs_battle_mapStickDeadZone -> map_stick_dead_zone.mapStickDeadZone (5BE50.c).
//! Inputs: an arbitrary c_int, biased toward the two comparison edges (63/64/65
//! and 191/192/193), zero, -1, the int extremes, and INT_MIN+64 (the smallest
//! argument for which `arg0 - 64` does not overflow). Every input is a plain
//! value: the C reads no globals, calls nothing and dereferences nothing.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BE50");
const c = @import("c_5BE50");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").mapStickDeadZone));

pub const classes = [_][:0]const u8{ "below-64", "at-or-above-192", "middle" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const edges = [_]c_int{
        0,
        1,
        63,
        64,
        65,
        191,
        192,
        193,
        -1,
        -64,
        255,
        std.math.maxInt(c_int),
        std.math.minInt(c_int) + 64,
    };
    return .{ctx.pick(c_int, &edges)};
}

pub fn classify(args: Args) usize {
    const x = args[0];
    if (x < 64) return 0;
    if (x >= 192) return 1;
    return 2;
}
