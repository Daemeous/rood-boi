//! zig_diff spec: func_800BB38C -> func_800BB38C.func_800BB38C (4A0A8.c).
//! Inputs: a 3-byte opcode; getShort and func_800BFE50 return any values
//! (negative ones and above 16 bits: the u_short conversions show).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB38C));

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.allocBytes(3, 1), ctx.int(c_short) };
}
