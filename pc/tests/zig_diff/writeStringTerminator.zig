//! zig_diff spec: _writeStringTerminator -> writeStringTerminator (5BF94.c).
//! Inputs: a single pointer to one writable byte, either in the harness arena
//! (random fill around it) or in the PS1 scratchpad. The C has no branches and
//! no callees; it stores one byte at `str`, so the spec varies only where that
//! byte lives and checks that the port writes the same value to the same place.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").writeStringTerminator));

pub const classes = [_][:0]const u8{ "arena", "scratchpad" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(2)) {
        const off = ctx.range(usize, 0, zd.scratchpad_size - 1);
        const p: [*c]u8 = @ptrFromInt(zd.scratchpad + off);
        return .{p};
    }
    const p: [*c]u8 = ctx.alloc(u8);
    return .{p};
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    if (a >= zd.scratchpad and a < zd.scratchpad + zd.scratchpad_size) return 1;
    return 0;
}
