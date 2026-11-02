//! zig_diff spec: vs_battle_script_specialOp -> script_special_op.script_specialOp (4A0A8.c).
//! Inputs: a 7-byte opcode with every operation (script[1]: 0..17 and an
//! undefined one, whose report goes to a dead stack buffer) and any argument
//! byte (index 0..15 into D_800E9C00 for 10); D_800F4BF8 zero or not and
//! func_8007B9FC zero or not (the ending's two steps); isActionUnlocked zero
//! or not.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_specialOp));

pub const classes = [_][:0]const u8{ "op0", "ending-start", "ending-wait", "op2", "op3", "op4", "op5", "op6", "op7",
    "op8", "op9", "op10", "op11", "op12", "op13", "op14", "op15", "op16", "op17", "undefined" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.allocBytes(7, 1);
    s[1] = if (ctx.chance(20)) ctx.pick(u8, &.{ 18, 0xFF, 100 }) else ctx.range(u8, 0, 17);
    if (s[1] == 10) s[2] &= 15;
    v.g.D_800F4BF8().* = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x80 });
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const op = args[0][1];
    if (op > 17) return 19;
    if (op == 0) return 0;
    if (op == 1) return if (v.g.D_800F4BF8().* == 0) 1 else 2;
    return @as(usize, op) + 1;
}

pub const stubs = struct {
    pub fn func_8007B9FC(ctx: *zd.Ctx) c_int {
        return if (ctx.value(u8) & 1 != 0) 0 else ctx.value(c_int) | 1;
    }
    pub fn vs_battle_isActionUnlocked(ctx: *zd.Ctx, id: c_uint) c_int {
        _ = id;
        return if (ctx.value(u8) & 1 != 0) 0 else 1;
    }
};
