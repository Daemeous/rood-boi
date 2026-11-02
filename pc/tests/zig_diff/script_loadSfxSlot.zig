//! zig_diff spec: vs_battle_script_loadSfxSlot -> script_load_sfx_slot.script_loadSfxSlot (4A0A8.c).
//! Inputs: a 4-byte script operand block in the arena whose bytes 1 and 2 are
//! drawn from edge values (0, 1, 0x7F, 0x80, 0xFF and random), so both operands
//! cover zero, the sign bit of the byte and the top of its range. arg1 is any
//! c_short, which the C never reads. The C has no branches and no comparisons;
//! what it does is call vs_main_loadSfxSlot with the two bytes (zero-extended
//! to u_int) and return 0. The stub's result is ignored by the C.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_loadSfxSlot));

pub const classes = [_][:0]const u8{ "arg2-high-bit", "arg1-zero", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block: [*c]u8 = ctx.allocBytes(4, 1);
    block[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    block[2] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    const arg1: c_short = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ block, arg1 };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    if (p[2] & 0x80 != 0) return 0;
    if (p[1] == 0) return 1;
    return 2;
}
