//! zig_diff spec: func_80074120 -> func_80074120.func_80074120 (146C.c).
//! Inputs: the global D_800F1910 with random bytes (its SVECTOR at offset 8 is
//! what the C copies; any u16 values, so the casts wrap). The C has no
//! arguments. Three classes: _stepTowardTarget returns nonzero (the C returns 1
//! without calling func_800A0BE0), returns 0 with func_800A0BE0's bit 27 clear,
//! or returns 0 with that bit set (the C returns 0). The stubs copy the vector
//! they were given into the scratchpad, so a wrong field or wrong copy shows,
//! and they can rewrite it, as the real callee may.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80074120));

pub const classes = [_][:0]const u8{ "step-nonzero", "step-zero-bit-clear", "step-zero-bit-set" };

/// Set by prepare: whether _stepTowardTarget returns 0, and whether
/// func_800A0BE0 returns a value with bit 27 set.
var step_zero: bool = false;
var bit_set: bool = false;

pub const stubs = struct {
    pub fn _stepTowardTarget(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        const s: *v.SVECTOR = @ptrCast(@alignCast(p));
        // The callee may rewrite the vector it was given.
        if (ctx.value(u1) == 1) s.vx = ctx.value(i16);
        // Record what the callee was passed, for the state comparison.
        @as(*v.SVECTOR, @ptrFromInt(0x1F800400)).* = s.*;
        if (step_zero) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800A0BE0(ctx: *zd.Ctx, a: anytype) c_uint {
        _ = a;
        const r = ctx.value(c_uint);
        return if (bit_set) r | 0x08000000 else r & ~@as(c_uint, 0x08000000);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    step_zero = !ctx.chance(3);
    bit_set = ctx.chance(2);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (!step_zero) return 0;
    return if (bit_set) 2 else 1;
}
