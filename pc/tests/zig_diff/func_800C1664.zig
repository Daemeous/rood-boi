//! zig_diff spec: func_800C1664 -> func_800C1664.func_800C1664 (58578.c).
//! Inputs: the global D_800EB9B8 is null or an arena object with random
//! status bytes, and its unk2D is often set to the clamped count so the
//! `>=` sees equal sides. arg0 is negative, zero, 25 (the clamp's equal
//! edge), above it, or in range; arg1 and arg2 are random. The callee
//! func_800C58F8 writes fields of the object (unk2D, unk40[1]) but never
//! the global pointer itself, so the C's trailing stores always land on
//! a valid object and a store moved across the call shows as a state
//! difference at the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C1664));

pub const classes = [_][:0]const u8{
    "null-object",
    "arg-negative",
    "arg-zero",
    "arg-at-clamp",
    "arg-over-clamp",
    "arg-in-range",
};

pub const stubs = struct {
    pub fn func_800C58F8(ctx: *zd.Ctx, p: anytype) c_int {
        _ = p;
        if (v.g.D_800EB9B8().*) |obj| {
            if (ctx.value(u1) == 1) obj.unk2D = ctx.value(u8);
            obj.unk40[1] = ctx.value(i32);
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = v.g.D_800EB9B8();
    if (ctx.chance(6)) {
        slot.* = null;
    } else {
        slot.* = ctx.alloc(v.D_800EB9B8_t);
    }

    const arg0: c_int = ctx.pick(c_int, &.{
        0, 1, 24, 25, 26, -1, -2, 255, 256,
        std.math.minInt(c_int), std.math.maxInt(c_int),
    });
    const arg1 = ctx.int(c_int);
    const arg2 = ctx.int(c_int);

    if (slot.*) |obj| {
        // The clamped count the C compares unk2D against (negatives never match).
        const eff: c_int = if (arg0 >= 25) 24 else arg0;
        if (ctx.chance(2)) {
            obj.unk2D = @as(u8, @truncate(@as(u32, @bitCast(eff))));
        } else {
            obj.unk2D = ctx.pick(u8, &.{ 0, 1, 24, 25, 255 });
        }
    }

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800EB9B8().* == null) return 0;
    const a = args[0];
    if (a < 0) return 1;
    if (a == 0) return 2;
    if (a == 25) return 3;
    if (a > 25) return 4;
    return 5;
}
