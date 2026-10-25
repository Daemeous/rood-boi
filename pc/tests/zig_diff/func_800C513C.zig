//! zig_diff spec: func_800C513C -> func_800C513C.func_800C513C (5BF94.c).
//! Inputs: two random c_ints for arg0 and arg1, with arg0 drawn from edges
//! that hit every side of `arg0 & 0xF0`: zero, the low nibble alone (0x0F,
//! 0x08, 0x07), single bits in the masked nibble (0x10, 0x70, 0x80), all
//! masked bits set, and the sign-bit extremes (-1, minInt, -0x10). The call
//! to func_8009FE74 is a recording stub; it changes the scratchpad word at
//! 0x1F800000, which is what a real callee may write and the C never reads
//! back itself. Its return value is a c_int, as the C prototype declares.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C513C));

pub const classes = [_][:0]const u8{ "masked-zero", "masked-nonzero", "masked-nonzero-negative" };

pub const stubs = struct {
    pub fn func_8009FE74(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        const word: *align(1) u32 = @ptrFromInt(zd.scratchpad);
        word.* = ctx.value(u32);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.pick(c_int, &.{
        0,      0x0F,  0x08, 0x07, 0x10, 0x70,
        0x80,   0xF0,  0x1F0, -1,   -0x10,
        std.math.minInt(c_int), std.math.maxInt(c_int),
    });
    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1, 0x10, std.math.minInt(c_int) });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    if (args[0] & 0xF0 == 0) return 0;
    return if (args[0] < 0) 2 else 1;
}
