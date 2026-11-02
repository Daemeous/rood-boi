//! zig_diff spec: func_800B70E8 -> func_800B70E8.func_800B70E8 (4A0A8.c).
//! Inputs: a 7-byte opcode with each slot (script[5] >> 6: 0..3) and edge
//! bytes (0x7F, 0x80, 0xFF: the signed doubling and the << 7); D_800EB9BC
//! null (one time in eight) or four arena entries; the slot's busy byte
//! unk3 zero or not.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B70E8));

pub const classes = [_][:0]const u8{ "no-base", "busy", "start" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.allocBytes(7, 1);
    for (1..7) |i| s[i] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    const base: [*]v.D_800EB9BC_t = @ptrCast(ctx.alloc([4]v.D_800EB9BC_t));
    v.g.D_800EB9BC().* = if (ctx.chance(8)) null else &base[0];
    const k = s[5] >> 6;
    v.g.D_800F4B30()[k].unk3 = if (ctx.chance(3)) ctx.int(u8) | 1 else 0;
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800EB9BC().* == null) return 0;
    return if (v.g.D_800F4B30()[args[0][5] >> 6].unk3 != 0) 1 else 2;
}
