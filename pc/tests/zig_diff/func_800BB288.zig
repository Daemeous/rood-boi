//! zig_diff spec: func_800BB288 -> func_800BB288.func_800BB288 (4A0A8.c).
//! Inputs: the global D_800EB9B4 is null (1 in 4), or points at an arena
//! D_800EB9B4_t whose unk0 is drawn from the edges 0, 1, 0x7F, 0x80, 0xFF
//! (or any byte): both zero and nonzero, with and without the sign bit. The
//! unk1 and unk2..slot fields keep their random fill, so a port that reads
//! the wrong field diverges. arg0 is null half the time (the C never reads
//! it), and arg1 is any c_short.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB288));

pub const classes = [_][:0]const u8{ "null-global", "unk0-zero", "unk0-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = v.g.D_800EB9B4();
    if (ctx.chance(4)) {
        slot.* = null;
    } else {
        const obj = ctx.alloc(v.D_800EB9B4_t);
        obj.unk0 = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
        slot.* = obj;
    }
    const arg0: [*c]u8 = if (ctx.chance(2)) null else ctx.allocBytes(1, 1);
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    const p = v.g.D_800EB9B4().* orelse return 0;
    return if (p.unk0 != 0) 2 else 1;
}
