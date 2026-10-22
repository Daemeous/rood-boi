//! zig_diff spec: func_800BFEBC -> func_800BFEBC.func_800BFEBC (573B8.c).
//! Inputs: mode 0-3 (the four direct curves) or 4-7 (a row of D_800F4B28,
//! whose power and type bytes prepare sets: power zero, positive or negative;
//! type 0-3 or the default). Frame is below, equal to, or above duration, and
//! duration is any nonzero short (negatives and edges), so the 12-bit ratio,
//! the power loops (value * value wraps) and the rsin/rcos rounding of
//! negative sums are all reached. Every path divides by duration, so it is
//! never 0. The table has 4 rows, so mode stays in 0..7.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BFEBC));

pub const classes = [_][:0]const u8{
    "mode-0",
    "mode-1",
    "mode-2",
    "mode-3",
    "default-power-zero",
    "default-power-pos",
    "default-power-neg",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // The table has 4 rows: type byte and power byte for each.
    const table = v.g.D_800F4B28();
    for (0..4) |i| {
        table[i][0] = ctx.pick(i8, &.{ 0, 1, 2, 3, 4, -1, -128, 127 });
        table[i][1] = ctx.pick(i8, &.{ 0, 1, -1, 2, -2, 3, -4, 127, -128 });
    }

    // Direct modes half the time, table rows (mode - 4 in 0..3) otherwise.
    const mode: c_short = if (ctx.chance(2))
        ctx.range(c_short, 0, 3)
    else
        ctx.range(c_short, 4, 7);

    var duration = ctx.pick(c_short, &.{ 1, -1, 2, -2, 4096, 32767, -32768, 0 });
    if (duration == 0) duration = 1;

    // frame == duration in about a third of the runs; otherwise random or edge.
    const frame: c_short = if (ctx.chance(3))
        duration
    else
        ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768, duration });

    return .{ mode, frame, duration };
}

pub fn classify(args: Args) usize {
    const mode = args[0];
    if (mode < 4) return @intCast(mode);
    const power = v.g.D_800F4B28()[@intCast(mode - 4)][1];
    if (power == 0) return 4;
    return if (power > 0) 5 else 6;
}
