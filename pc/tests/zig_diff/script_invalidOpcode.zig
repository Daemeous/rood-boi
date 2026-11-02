//! zig_diff spec: vs_battle_invalidOpcode -> script_invalid_opcode.invalidOpcode (4A0A8.c).
//! Inputs: a script byte and any opcode index. The C formats its report into a
//! stack buffer that nothing reads (sprintf runs as libc's): the return value is
//! all a caller sees.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").invalidOpcode));

pub fn prepare(ctx: *zd.Ctx) Args {
    const script = ctx.allocBytes(8, 1);
    return .{ script, ctx.int(c_short) };
}
