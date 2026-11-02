//! zig_diff spec: func_800BD610 -> func_800BD610.func_800BD610 (4A0A8.c).
//! Inputs: a D_800F4BA4 object in the arena, pointed to by the global (the C
//! never tests it for null). Each of the seven zero tests is taken or not
//! with its field random-zero about half the time. The result's bit 7 is set
//! only when both unkB bytes are zero, since the C sums them as ints; the
//! return value is a char, so bit 7 makes it negative. The u8-wrap class
//! (0x80 + 0x80 = 256 as ints, 0 as u8) is the case where a u8 sum would
//! wrongly set bit 7.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BD610));

pub const classes = [_][:0]const u8{ "sum-wraps-u8", "sum-zero", "other" };

fn maybe8(ctx: *zd.Ctx) u8 {
    if (ctx.chance(2)) return 0;
    return ctx.edge(u8, &.{ 1, 0x7F, 0x80, 0xFF });
}

fn maybe16(ctx: *zd.Ctx) i16 {
    if (ctx.chance(2)) return 0;
    return ctx.edge(i16, &.{ 1, -1, 0x100, 0x7FFF, -32768 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = d;

    d.lookAt.mode = ctx.pick(u8, &.{ 0xF, 0xE, 0 });
    d.eye.mode = ctx.pick(u8, &.{ 0xF, 0xE, 0 });
    d.unk168.unkA = maybe16(ctx);
    d.projectionDistance.unk9 = maybe8(ctx);
    d.nearClip.unk9 = maybe8(ctx);
    d.farClip.unk9 = maybe8(ctx);
    d.unk25C.unk1E = maybe16(ctx);

    // Half the runs make the int sum of the two unkB bytes zero (bit 7 set,
    // so the result is negative); a quarter make both 0x80 (the u8 sum wraps
    // to zero, the int sum does not); the rest are edge values.
    if (ctx.chance(2)) {
        d.unk1A4.unkB = 0;
        d.unk1B0.unkB = 0;
    } else if (ctx.chance(2)) {
        d.unk1A4.unkB = 0x80;
        d.unk1B0.unkB = 0x80;
    } else {
        d.unk1A4.unkB = ctx.edge(u8, &.{ 0, 1, 0x80, 0xFF });
        d.unk1B0.unkB = ctx.edge(u8, &.{ 0, 1, 0x80, 0xFF });
    }
    return .{};
}

pub fn classify(_: Args) usize {
    const d = v.g.D_800F4BA4().*.?;
    if (d.unk1A4.unkB == 0x80 and d.unk1B0.unkB == 0x80) return 0;
    if (d.unk1A4.unkB == 0 and d.unk1B0.unkB == 0) return 1;
    return 2;
}
