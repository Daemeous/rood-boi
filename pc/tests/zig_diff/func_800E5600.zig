//! zig_diff spec: func_800E5600 -> func_800E5600.func_800E5600 (7CD68.c).
//! Inputs: an action index in vs_main_actions (0, 255 or any byte), flagsE_13
//! and flagsE_3 set independently (the first test wins when both are set),
//! aoe_8 at its edges, and an arg2 that is 0, 1, INT_MAX, 0x80000000,
//! 0xFFFFFFFF or random. The callee returns the same value as arg2 a third of
//! the time (so `v < arg2` sees equal sides), negative values half the time
//! (so the signed/unsigned difference of `v < arg2` shows), and random values
//! otherwise. The callee may overwrite aoe_0 and the C never reads it back.
//! Note: flagsE_13 << 0x1D and flagsE_3 << 0x17 are 0 or a single bit below
//! bit 31, so their sign bit is never set; only the zero test is exercised.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E5600));

pub const classes = [_][:0]const u8{ "flagsE13", "flagsE3-only", "neither" };

/// The arg2 the callee echoes back when it returns the equal value.
var want: c_uint = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = @intCast(ctx.pick(u8, &.{ 0, 255 }));
    const arg0: c_int = ctx.int(c_int);
    const arg2: c_uint = ctx.pick(c_uint, &.{ 0, 1, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF });
    want = arg2;

    const action = &v.g.actions()[@intCast(idx)];
    psx.setBits(action, "flagsE_13", @intFromBool(ctx.chance(3)));
    psx.setBits(action, "flagsE_3", ctx.int(u1));
    action.aoe_8 = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });

    return .{ arg0, idx, arg2 };
}

pub fn classify(args: Args) usize {
    const action = &v.g.actions()[@intCast(args[1])];
    if (psx.getBits(action, "flagsE_13") != 0) return 0;
    if (psx.getBits(action, "flagsE_3") != 0) return 1;
    return 2;
}

pub const stubs = struct {
    pub fn func_800E5568(ctx: *zd.Ctx, p: anytype) i32 {
        // The callee may change the bytes it was handed (the C never reads them back).
        const b: *u8 = @ptrCast(p);
        if (ctx.value(u1) == 1) b.* = ctx.value(u8);
        // Echo arg2 a third of the time (equal sides of `v < arg2`), else a
        // random signed value (negative half the time).
        if (ctx.value(u8) < 85) return @bitCast(want);
        return ctx.value(i32);
    }
};
