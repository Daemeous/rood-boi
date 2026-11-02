//! zig_diff spec: func_800B6724 -> func_800B6724.func_800B6724 (4A0A8.c).
//! Inputs: the global D_800EB9BC points at an arena object whose bitfield
//! unk0_1 is random, so both values of the bit occur (classes "bit1-set"
//! and "bit1-clear"). The object's other bytes stay random, so a port
//! reading a neighbouring bit or field disagrees. arg0 is null or an arena
//! pointer the C never reads, and arg1 takes edge values (0, +-1, the
//! short extremes) and random values. The C has no branch, so no
//! waivers are needed.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B6724));

pub const classes = [_][:0]const u8{ "bit1-set", "bit1-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800EB9BC_t);
    // Random bits everywhere in the object, bit 1 of byte 0 included.
    psx.setBits(obj, "unk0_1", ctx.int(u1));
    v.g.D_800EB9BC().* = obj;

    const arg0: [*c]u8 = if (ctx.chance(2)) null else ctx.allocBytes(4, 1);
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    const obj = v.g.D_800EB9BC().*.?;
    return if (psx.getBits(obj, "unk0_1") != 0) 0 else 1;
}
