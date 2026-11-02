//! zig_diff spec: func_800B80B4 -> func_800B80B4.func_800B80B4 (4A0A8.c).
//! Inputs: a 4-byte opcode. func_800BB68C fills a stack set (the trace tags
//! it); the func_800BB788 stub hands out 0, 2, 0xFFFF or any id and ends the
//! walk with 1 (one call in four, and by the 42nd), so the loop runs
//! zero, one and many times.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B80B4));

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.allocBytes(4, 1), ctx.int(c_short) };
}

pub const stubs = struct {
    pub fn func_800BB788(ctx: *zd.Ctx, set: anytype) u16 {
        _ = set;
        if (ctx.calls > 40) return @intCast((ctx.calls >> 1) & 1); // 1 or 0: the walk (and a mutant ending on 0) stops
        return switch (ctx.value(u8) % 8) {
            0, 1 => 1,
            2 => 0,
            3 => 2,
            4 => 0xFFFF,
            else => ctx.value(u16),
        };
    }
};
