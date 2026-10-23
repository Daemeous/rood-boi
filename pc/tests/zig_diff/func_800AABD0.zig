//! zig_diff spec: func_800AABD0 -> func_800AABD0.func_800AABD0 (40564.c).
//! Inputs: an index 0..16 into the D_800F4538 table, whose entry is null
//! about one time in seven (then arg1 may be null too, since the C reads it
//! only past the null test). Otherwise a random D_800F4538_t holds the stored
//! vector (edge values included), and arg1 is biased to equal it on all
//! three axes, on vz only, on vy only, or to be random. arg2 covers zero,
//! positive, negative and the extremes, so the `arg2 == 0` test sees the sign
//! bit both ways. The subtraction wraps through i16 storage. The class of each
//! input is read back from the state the C will see.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AABD0));

pub const classes = [_][:0]const u8{ "null-entry", "zero-store", "same-vector", "vz-only", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const tbl = v.g.D_800F4538();
    const idx: c_int = ctx.range(c_int, 0, 16);
    const arg2: c_int = ctx.pick(c_int, &.{ 0, 0, 1, -1, 2, std.math.minInt(c_int), std.math.maxInt(c_int) });
    const arg1 = ctx.alloc(v.SVECTOR);

    if (ctx.chance(7)) {
        tbl[@intCast(idx)] = null;
        return .{ idx, if (ctx.chance(2)) null else arg1, arg2 };
    }

    const obj = ctx.alloc(v.D_800F4538_t);
    tbl[@intCast(idx)] = obj;

    const sx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    const sy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    const sz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk0.unk2C = sx;
    obj.unk0.unk2E = sy;
    obj.unk0.unk30 = sz;

    arg1.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    arg1.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    arg1.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    switch (ctx.range(u8, 0, 4)) {
        0 => {
            arg1.vx = sx;
            arg1.vy = sy;
            arg1.vz = sz;
        },
        1 => {
            arg1.vx = sx;
            arg1.vy = sy;
            arg1.vz = sz +% ctx.pick(i16, &.{ 1, -1, 0x100, -0x8000, 0x7fff });
        },
        2 => {
            arg1.vx = sx;
            arg1.vy = sy +% ctx.pick(i16, &.{ 1, -1, 0x100, -0x8000, 0x7fff });
            arg1.vz = sz;
        },
        else => {},
    }
    return .{ idx, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const tbl = v.g.D_800F4538();
    const obj = tbl[@intCast(args[0])] orelse return 0;
    if (args[2] == 0) return 1;
    const a = args[1].?;
    if (a.vx != obj.unk0.unk2C or a.vy != obj.unk0.unk2E) return 4;
    return if (a.vz == obj.unk0.unk30) 2 else 3;
}
