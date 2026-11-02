//! zig_diff spec: func_800B66EC -> func_800B66EC.func_800B66EC (4A0A8.c).
//! Inputs: arg0 null or a block in the arena, arg1 any short, with the
//! edge values (0, -1, 32767, -32768) picked half the time. The C body is
//! `return 4;`: it reads neither argument, branches nowhere and writes no
//! state, so the check is the return value on every input.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B66EC));

pub const classes = [_][:0]const u8{ "null-arg0", "arena-arg0" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = if (ctx.chance(4)) null else ctx.allocBytes(16, 16);
    const n = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ p, n };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
