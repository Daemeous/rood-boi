//! zig_diff spec: func_800BA108 -> func_800BA108.func_800BA108 (4A0A8.c).
//! Inputs: a 2-byte buffer in the arena whose byte 1 is zero or one of
//! 1/0x7F/0x80/0xFF (the flag test with and without the sign bit), an
//! arbitrary short (the C never reads it), and the two globals: D_800F4C08
//! zero or 1/0x7F/0x80/0xFF, D_800F4C4C any i16. func_8008E470 returns any
//! c_int, sometimes a multiple of 256 (low byte zero, so the u8 store of the
//! result reads as zero) or zero; it may also write D_800F4C4C, which the C
//! never reads back in that call. func_800913BC returns any c_int, which
//! the C stores into an i16.
const std = @import("std");
const zd = @import("zdiff");
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA108));

pub const classes = [_][:0]const u8{ "arg1-flag-set", "arg1-flag-clear", "global-set-no-arg1", "idle" };

pub const stubs = struct {
    pub fn func_8008E470(ctx: *zd.Ctx) c_int {
        const r = ctx.value(c_int);
        // A multiple of 256 (low byte zero) or zero: the C's u8 store reads it as zero.
        if (ctx.value(u2) == 0) v.g.D_800F4C4C().* = ctx.value(i16);
        return switch (ctx.value(u2)) {
            0 => r & ~@as(c_int, 0xFF),
            1 => 0,
            else => r,
        };
    }

    pub fn func_800913BC(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(2, 1);
    const p: [*c]u8 = buf;
    p[1] = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    v.g.D_800F4C08().* = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    v.g.D_800F4C4C().* = ctx.int(i16);
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const flag = args[0][1] != 0;
    const set = v.g.D_800F4C08().* != 0;
    if (flag) return if (set) 2 else 0;
    return if (set) 1 else 3;
}
