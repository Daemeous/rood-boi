//! zig_diff spec: func_800D4D64 -> func_800D4D64.func_800D4D64 (5BF94.c).
//! Inputs: one D_800F53B8_t in the arena with random bytes everywhere, its
//! unkD1C.unk0.unk0 drawn from {3, 4, 5, 0xFFFF} (so the `== 4` test is taken
//! about a third of the time), unk2 from {3, 4, 0} (so a port that reads the
//! wrong halfword differs), and unk4.unk0 an i32 from the edges 0, 0x7F, 0x80,
//! 0xFF, 0x100, -1, -128, INT_MIN or fully random (the u8 truncation and the
//! sign bit both occur). unk4.unk4 is random, so a port reading the wrong
//! field differs. The C makes no ordered comparison and no zero test, and
//! its only callee, func_8006CDB8, is a void call whose effects the C never
//! reads back, so the stub is the default recording stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4D64));

pub const classes = [_][:0]const u8{ "not-four", "four-byte-below-0x80", "four-byte-0x80-up" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F53B8_t);
    obj.unkD1C.unk0.unk0 = ctx.edge(u16, &.{ 4, 4, 4, 3, 5, 0, 0xFFFF });
    obj.unkD1C.unk0.unk2 = ctx.edge(u16, &.{ 4, 3, 0 });
    obj.unkD1C.unk0.unk4.unk0 = ctx.pick(i32, &.{ 0, 0x7F, 0x80, 0xFF, 0x100, -1, -128, std.math.minInt(i32) });
    obj.unkD1C.unk0.unk4.unk4 = ctx.int(i16);
    return .{obj};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    if (a.unkD1C.unk0.unk0 != 4) return 0;
    const byte: u8 = @truncate(@as(u32, @bitCast(a.unkD1C.unk0.unk4.unk0)));
    return if (byte < 0x80) 1 else 2;
}
