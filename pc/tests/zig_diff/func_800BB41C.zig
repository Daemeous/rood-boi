//! zig_diff spec: func_800BB41C -> func_800BB41C.func_800BB41C (4A0A8.c).
//! Inputs: a 4-byte arena buffer whose byte 1 is a value edge (0, 0x01, 0x7F,
//! 0x80, 0xFF or random), so the zero, high-bit and other classes all occur.
//! arg1 is random and unused by the C. The callee func_800BD610 is a recording
//! stub returning any 32-bit value; three times in four it also returns the
//! buffer byte's bits (OR-ed in), so the result is 0 as well as 1 and the
//! high bit of the byte matters to the result. In half the runs the stub also
//! overwrites byte 1 of the buffer, so a port that reads arg0[1] before the
//! call and one that reads it after the call disagree.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB41C));

pub const classes = [_][:0]const u8{ "byte-zero", "byte-high-bit", "byte-other" };

/// The buffer the C reads, when the stub may overwrite it.
var target: ?[*]u8 = null;
/// The byte value at prepare time; the stub returns bits covering it sometimes.
var bits: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 0x01, 0x7F, 0x80, 0xFF });
    bits = buf[1];
    target = if (ctx.chance(2)) buf else null;
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    if ((b & 0x80) != 0) return 1;
    return 2;
}

pub const stubs = struct {
    pub fn func_800BD610(ctx: *zd.Ctx) c_int {
        if (target) |t| {
            if (ctx.value(u1) == 1) t[1] = ctx.value(u8);
        }
        const mask: c_int = if (ctx.value(u2) != 0) @as(c_int, bits) else 0;
        return ctx.value(c_int) | mask;
    }
};
