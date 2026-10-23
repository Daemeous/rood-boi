//! zig_diff spec: func_800A69B4 -> func_800A69B4.func_800A69B4 (3A1A0.c).
//! Inputs: a D_800F4538_t in the arena with a random unk63C (0, 1, 4096,
//! 32768, 65535 or random), a random D_800F49F9 (its bit 1 both ways), and
//! a random D_800F4B08.vy (edges around 0, -64, 32767, -32768). rsin returns
//! any i16 so the product wraps the i16 of v.vx/v.vz. func_800A6EE8 may
//! rewrite D_800F4B08.vy and returns vy-64, vy-65, vy-63 (the comparison's
//! equal, less and greater sides) or a random i16, so both `h > -64` tests
//! see equal sides, and ret is reached.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A69B4));

pub const classes = [_][:0]const u8{ "vx-path", "vz-path", "zero-speed" };

pub const stubs = struct {
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(i16));
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, a: anytype, x: anytype, z: anytype, k: anytype) c_int {
        _ = x;
        _ = z;
        _ = k;
        const p: *v.SVECTOR = @ptrCast(@alignCast(a));
        if (ctx.value(u2) == 0) p.vy = ctx.value(i16);
        const base: c_int = @as(c_int, p.vy);
        const pick = ctx.value(u2);
        if (pick == 0) return base - 64;
        if (pick == 1) return base - 65;
        if (pick == 2) return base - 63;
        return @as(c_int, ctx.value(i16));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F4538_t);
    arg.unk63C = ctx.pick(u16, &.{ 0, 1, 4096, 32768, 65535 });
    v.g.D_800F49F9().* = ctx.pick(u8, &.{ 0x00, 0x02, 0xFD, 0xFF });
    const b = v.g.D_800F4B08();
    b.vy = ctx.pick(i16, &.{ 0, -64, 63, -1, 32767, -32768 });
    return .{arg};
}

pub fn classify(args: Args) usize {
    if (args[0].?.unk63C == 0) return 2;
    return if ((v.g.D_800F49F9().* & 2) != 0) 1 else 0;
}
