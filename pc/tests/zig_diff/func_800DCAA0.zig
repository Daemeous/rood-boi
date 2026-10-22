//! zig_diff spec: func_800DCAA0 -> func_800DCAA0.func_800DCAA0 (74088.c).
//! Inputs: a random func_800DCAA0_t1 (its unk88 byte is passed on) and a
//! random func_800DCAA0_t with unkC_0 (the table index) in 0-15 and unkC_7
//! at its edges (0, 1, 2048, 4095). arg3 is 3 two times in three, which takes
//! the actor path: D_800F4538[unkC_0] is a fresh random actor for every table
//! entry, and the position and unkC_7 feed the vectors. Otherwise arg3 is any
//! int and the first callee runs. Both callees return 0 about a third of the
//! time and any 32-bit value otherwise, so the result test sees zero and
//! nonzero values of both signs. func_800863A4 writes the two vectors it is
//! given into an arena log, so the state hash at each call and the final
//! state expose a wrong vector, field, or argument order even when the
//! returned value does not change. It also folds them into its return value.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DCAA0));

pub const classes = [_][:0]const u8{ "other-callee", "actor-low-index", "actor-high-index" };

/// Arena block where func_800863A4 logs the vectors it is given.
var log: *[8]i16 = undefined;

pub const stubs = struct {
    // The first callee changes nothing the C reads back: it only returns.
    pub fn func_8008631C(ctx: *zd.Ctx, a: anytype, b: anytype, c_: anytype, d: anytype, e: anytype) c_int {
        _ = a;
        _ = b;
        _ = c_;
        _ = d;
        _ = e;
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800863A4(ctx: *zd.Ctx, a: anytype, b: anytype, c_: anytype, d: anytype, p20: anytype, p28: anytype, e: anytype) c_int {
        _ = a;
        _ = b;
        _ = c_;
        _ = d;
        _ = e;
        const s20: *const v.SVECTOR = @ptrCast(@alignCast(p20));
        const s28: *const v.SVECTOR = @ptrCast(@alignCast(p28));
        // Record both vectors field by field in the arena: the state hash at
        // this call and the final state now differ for any wrong field.
        log.*[0] = s20.vx;
        log.*[1] = s20.vy;
        log.*[2] = s20.vz;
        log.*[3] = s28.vx;
        log.*[4] = s28.vy;
        log.*[5] = s28.vz;
        const folded: c_int = @as(c_int, s20.vx) +% @as(c_int, s20.vy) +% @as(c_int, s20.vz) +%
            @as(c_int, s28.vx) +% @as(c_int, s28.vy) +% @as(c_int, s28.vz);
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int) +% folded;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.func_800DCAA0_t1);
    const arg2 = ctx.alloc(v.func_800DCAA0_t);
    log = @ptrCast(@alignCast(ctx.allocBytes(16, 2)));
    // Every table entry is a valid actor: the C dereferences D_800F4538[unkC_0].
    for (v.g.D_800F4538()) |*e| e.* = ctx.alloc(v.D_800F4538_t);
    psx.setBits(arg2, "unkC_0", ctx.range(u4, 0, 15));
    psx.setBits(arg2, "unkC_7", ctx.pick(u12, &.{ 0, 1, 2048, 4095 }));
    const arg1 = ctx.int(c_int);
    const arg3: c_int = if (ctx.chance(3)) ctx.int(c_int) else 3;
    return .{ arg0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    if (args[3] != 3) return 0;
    return if (psx.getBits(args[2].?, "unkC_0") < 8) 1 else 2;
}
