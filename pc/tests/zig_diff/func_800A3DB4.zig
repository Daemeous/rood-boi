//! zig_diff spec: func_800A3DB4 -> func_800A3DB4.func_800A3DB4 (3A1A0.c).
//! Inputs: x and z over edges and random values (negative ones, so x / 128
//! truncates toward zero where a shift would floor), minimumHeight over edges
//! and near the height the stubbed func_800A6EE8 returns, and the flag word at
//! *D_800F49F0 with bit 20 set about half the time. func_800A6EE8 returns the
//! minimum height itself or one below it (the equal and less-than sides of the
//! height test) and sometimes writes its offset out-parameter; func_800A3C34
//! returns NULL about a quarter of the time and an arena object otherwise.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3DB4));

pub const classes = [_][:0]const u8{ "flag-set", "flag-clear-negative-x", "flag-clear-nonnegative-x" };

/// The minimum height of the current iteration: the stubbed height is built
/// from it so the height test sees equal and adjacent values.
var min_h: c_int = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const word = ctx.alloc(u32);
    word.* = ctx.int(u32);
    if (ctx.chance(2)) {
        word.* |= @as(u32, 1) << 20;
    } else {
        word.* &= ~(@as(u32, 1) << 20);
    }
    v.g.D_800F49F0().* = @ptrCast(word);

    const edges = [_]c_int{ 0, 127, 128, -1, -127, -128, -129, std.math.minInt(c_int), std.math.maxInt(c_int) };
    const x = ctx.pick(c_int, &edges);
    const z = ctx.pick(c_int, &edges);
    min_h = ctx.pick(c_int, &.{ 0, -1, 1, 1000, -1000, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{ x, z, min_h };
}

pub fn classify(args: Args) usize {
    const word: [*]u32 = v.g.D_800F49F0().*.?;
    if (((word[0] >> 20) & 1) != 0) return 0;
    return if (args[0] < 0) 1 else 2;
}

pub const stubs = struct {
    pub fn func_800A6EE8(ctx: *zd.Ctx, p: anytype, x: anytype, z: anytype, f: anytype) c_int {
        _ = x;
        _ = z;
        _ = f;
        const o: *v.SVECTOR = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) {
            o.vx = ctx.value(i16);
            o.vy = ctx.value(i16);
        }
        return switch (ctx.value(u2)) {
            0 => min_h,
            1 => min_h -% 1,
            else => ctx.value(c_int),
        };
    }

    pub fn func_800A3C34(ctx: *zd.Ctx, a: anytype, b: anytype, h: anytype, f: anytype) ?*c.D_800F4538_t {
        _ = a;
        _ = b;
        _ = h;
        _ = f;
        if (ctx.value(u2) == 0) return null;
        const p: *c.D_800F4538_t = @ptrCast(ctx.alloc(u64));
        return p;
    }
};
