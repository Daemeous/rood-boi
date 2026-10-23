//! zig_diff spec: func_800C4734 -> func_800C4734.func_800C4734 (5BE50.c).
//! Inputs: D_800EB9B8 is null, or points at a random arena D_800EB9B8_t whose
//! unk2A and unk3E are each zero or nonzero (negative and sign-bit values
//! included), and vs_main_buttonsState is drawn from edges with and without
//! bit 7 and with and without the sign bit. The function makes no calls, so
//! there are no stubs. Each return path (null, unk2A, unk3E, buttons bit 7,
//! and the final return 2) is reached by some class.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BE50");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C4734));

pub const classes = [_][:0]const u8{ "null", "unk2A-nonzero", "unk3E-zero", "bit7-set", "return-2" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800EB9B8_t);
    if (ctx.chance(6)) {
        v.g.D_800EB9B8().* = null;
    } else {
        v.g.D_800EB9B8().* = d;
        d.unk2A = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ 1, -1, 0x7FFF, -32768 });
        d.unk3E = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ 1, -1, 0x7FFF, -32768, 0x80 });
    }
    v.g.vs_main_buttonsState().* = ctx.pick(i32, &.{ 0x80, -128, 0x7F, 0, -2147483648, 0x7FFFFFFF });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const d = v.g.D_800EB9B8().* orelse return 0;
    if (d.unk2A != 0) return 1;
    if (d.unk3E == 0) return 2;
    if ((v.g.vs_main_buttonsState().* & 0x80) != 0) return 3;
    return 4;
}
