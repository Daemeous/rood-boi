//! zig_diff spec: func_800A3054 -> func_800A3054.func_800A3054 (3A1A0.c).
//! Inputs: a random D_800F4538_t in the arena (its position, any i16 with
//! edges at 0, +-63 and the wrap points, so the u_short cast and the +-63
//! sums wrap) and a random func_800A3054_t (unk0 and unk4 drawn from edges
//! including 0, so both `< 0` tests see equal sides often, plus arbitrary
//! signs). The C has no callees and reads no globals, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3054));

pub const classes = [_][:0]const u8{ "unk0-zero", "unk4-zero", "both-negative", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const self = ctx.alloc(v.D_800F4538_t);
    const out = ctx.alloc(v.func_800A3054_t);

    self.unk0.position.vx = ctx.pick(i16, &.{ 0, 1, -1, 63, -63, 64, -64, 0x7FFF, -32768 });
    self.unk0.position.vz = ctx.pick(i16, &.{ 0, 1, -1, 63, -63, 64, -64, 0x7FFF, -32768 });

    out.unk0 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 63, -63 });
    out.unk2 = ctx.int(i16);
    out.unk4 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 63, -63 });

    return .{ self, out };
}

pub fn classify(args: Args) usize {
    const out = args[1].?;
    if (out.unk0 == 0) return 0;
    if (out.unk4 == 0) return 1;
    if (out.unk0 < 0 and out.unk4 < 0) return 2;
    return 3;
}
