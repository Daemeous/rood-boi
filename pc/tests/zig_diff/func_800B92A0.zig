//! zig_diff spec: func_800B92A0 -> func_800B92A0.func_800B92A0 (4A0A8.c).
//! Inputs: the byte pointer arg0 is either a PS1 address near 0 (inside the
//! low RAM the host maps at 0x80000000, so arg0 + 1 is readable) or an arena
//! block. The C never tests arg0 for null and vs_battle_getShort reads through
//! arg0 + 1, so arg0 is never null. arg1 is any short, which the C never reads.
//! Callee results are full-width values from ctx.value (the stubs' default), so
//! the narrowing of the getShort result and the func_800BFE50 result shows up in
//! the traced arguments and the returned value.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B92A0));

pub const classes = [_][:0]const u8{ "low-ram-arg", "arena-arg" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: [*c]u8 = if (ctx.chance(2))
        @ptrFromInt(zd.low_ram + ctx.range(usize, 0, zd.low_ram_size - 16))
    else
        @ptrCast(ctx.allocBytes(16, 1));
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    return if (a >= zd.low_ram and a < zd.low_ram + zd.low_ram_size) 0 else 1;
}
