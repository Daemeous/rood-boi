//! zig_diff spec: vs_battle_configurePlg -> configure_plg.configurePlg (6F014.c).
//! Inputs: the three int arguments, each drawn from one of three regimes: full
//! 32-bit randoms (often negative or past 16 bits), edge values (0, 1, -1, 0x7FFF,
//! 0x8000, 0xFFFF, 0x10000, INT_MIN, INT_MAX), or the non-negative 16-bit range.
//! The C stores lba and size into two u16 fields (truncating modulo 2^16) and
//! entryPointCount into an int global, so the classes separate values that
//! survive the stores from those that wrap or go negative. The function has no
//! branches and makes no calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").configurePlg));

pub const classes = [_][:0]const u8{ "negative", "past-16-bit", "in-16-bit" };

const edges = [_]c_int{ 0, 1, -1, 0x7FFF, -0x8000, 0x8000, 0xFFFF, 0x10000, std.math.minInt(c_int), std.math.maxInt(c_int) };

pub fn prepare(ctx: *zd.Ctx) Args {
    const regime = ctx.range(u8, 0, 2);
    var out: [3]c_int = undefined;
    for (&out) |*x| {
        x.* = switch (regime) {
            0 => ctx.int(c_int),
            1 => ctx.pick(c_int, &edges),
            else => ctx.range(c_int, 0, 0xFFFF),
        };
    }
    // Bias one argument per call toward the 16-bit boundary so each class occurs.
    if (ctx.chance(4)) out[ctx.range(u8, 0, 2)] = ctx.range(c_int, 0x10000, 0x1FFFF);
    return .{ out[0], out[1], out[2] };
}

pub fn classify(args: Args) usize {
    const xs = [_]c_int{ args[0], args[1], args[2] };
    for (xs) |x| if (x < 0) return 0;
    for (xs[0..2]) |x| if (x > 0xFFFF) return 1;
    return 2;
}
