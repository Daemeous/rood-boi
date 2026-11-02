//! zig_diff spec: vs_battle_script_jumpFwd -> script_jump_fwd.script_jumpFwd (4A0A8.c).
//! Inputs: arg0 is a block in the arena, a spot in the PS1 scratchpad or a spot in
//! PS1 low RAM, and arg1 is any short (the C never reads it). The C's only callee,
//! vs_battle_getShort, is a default recording stub, so its return is any short
//! (negative half the time), which covers sign extension of the index and wrapping
//! of the pointer. The C has no branches; the classes name the arg0 region.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpFwd));

pub const classes = [_][:0]const u8{ "arena", "scratchpad", "low-ram" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const base: [*c]u8 = switch (ctx.range(u8, 0, 2)) {
        0 => @ptrCast(ctx.allocBytes(64, 16)),
        1 => @ptrFromInt(zd.scratchpad + ctx.range(usize, 0, zd.scratchpad_size - 2)),
        else => @ptrFromInt(zd.low_ram + ctx.range(usize, 0, zd.low_ram_size - 2)),
    };
    return .{ base, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    if (a >= zd.scratchpad and a < zd.scratchpad + zd.scratchpad_size) return 1;
    if (a >= zd.low_ram and a < zd.low_ram + zd.low_ram_size) return 2;
    return 0;
}
