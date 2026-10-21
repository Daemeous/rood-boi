//! zig_diff spec: func_800E5A74 -> func_800E5A74.func_800E5A74 (7CD68.c).
//! Inputs: a random word at arg0 (its low 21 bits survive, bit 21 and up are
//! replaced) and a func_800E5A74_t whose unk4 comes from edge values (0, 1,
//! 0x7FF, 0x800, 0xFFE, 0x1000, 0xF000, 0xFFFF) or a random u16. Edges cover
//! unk4 bits that the 0xFFE mask drops (bit 0, bit 12 and up), the bit 11 sign
//! case that lands in bit 31 after the shift, and a fully masked-out value.
//! The C has no branches and no calls, so the classes only name the value
//! cases for the harness.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E5A74));

pub const classes = [_][:0]const u8{ "masked-zero", "bit11-set", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const word = ctx.alloc(c_int);
    word.* = ctx.int(c_int);
    const obj = ctx.alloc(v.func_800E5A74_t);
    obj.unk0 = ctx.int(i32);
    obj.unk4 = ctx.pick(u16, &.{ 0, 0x1, 0x7FF, 0x800, 0xFFE, 0x1000, 0xF000, 0xFFFF });
    return .{ word, obj };
}

pub fn classify(args: Args) usize {
    const f: u16 = args[1].?.unk4;
    if ((f & 0xFFE) == 0) return 0;
    if ((f & 0x800) != 0) return 1;
    return 2;
}
