//! zig_diff spec: func_800A2FBC -> func_800A2FBC.func_800A2FBC (3A1A0.c).
//! Inputs: one D_800F4538_t in the arena. Its divisor unk5CA is never zero
//! (the C divides by it, so zero would fault: a spec bug, not a case), and
//! covers 1, 0x7FFF, the sign-bit range 0x8000 and above, and 0xFFFF. The
//! three unk34 velocities take zero, signs and the i16 extremes, so the
//! truncating divide sees negative numerators. The positions sit near the
//! i16 limits so that `position += temp` wraps.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A2FBC));

pub const classes = [_][:0]const u8{
    "divisor-one",
    "divisor-high-bit",
    "position-near-wrap",
    "negative-velocity",
    "plain",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);

    // The divisor is never zero: the C divides by it first.
    const d = ctx.pick(u16, &.{ 1, 2, 3, 7, 0x7FFF, 0x8000, 0xFFFF });
    obj.unk5CA = if (d == 0) 1 else d;

    obj.unk0.unk34.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, -7, 7 });
    obj.unk0.unk34.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, -7, 7 });
    obj.unk0.unk34.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, -7, 7 });

    obj.unk0.position.vx = ctx.pick(i16, &.{ 0, 32767, -32768, 32000, -32000 });
    obj.unk0.position.vy = ctx.pick(i16, &.{ 0, 32767, -32768, 32000, -32000 });
    obj.unk0.position.vz = ctx.pick(i16, &.{ 0, 32767, -32768, 32000, -32000 });

    return .{obj};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    if (a.unk5CA == 1) return 0;
    if (a.unk5CA >= 0x8000) return 1;
    const p = a.unk0.position;
    if (p.vx > 32000 or p.vx < -32000 or
        p.vy > 32000 or p.vy < -32000 or
        p.vz > 32000 or p.vz < -32000) return 2;
    const s = a.unk0.unk34;
    if (s.vx < 0 or s.vy < 0 or s.vz < 0) return 3;
    return 4;
}
