//! zig_diff spec: func_800B860C -> func_800B860C.func_800B860C (4A0A8.c).
//! Inputs: an 8-byte arena buffer whose byte 3 selects the switch arm
//! ((b3 >> 2) & 3 = 0, 1, 2 or 3, each a quarter of the runs) with random
//! bits 0-1 (the unit) and 4-7 (the +0x1A index base). Bytes 1-2 are random
//! and reach the callees only through vs_battle_getShort, whose stub returns
//! a value. arg1 is any short (edges included) and only reaches the return
//! value in the port's mutation check. The callees (func_8007CB84,
//! func_8007CBDC, vs_battle_loadEtm, vs_battle_getShort) are recording stubs
//! that write no state and return ctx.value, so no global is read back after
//! a call.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B860C));

pub const classes = [_][:0]const u8{ "case0", "case1", "case2", "default" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(8, 1);
    const sel = ctx.range(u8, 0, 3);
    // Keep bits 0-1 and 4-7 random; bits 2-3 pick the switch arm.
    buf[3] = (ctx.int(u8) & 0xF3) | (sel << 2);
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return (args[0][3] >> 2) & 3;
}
