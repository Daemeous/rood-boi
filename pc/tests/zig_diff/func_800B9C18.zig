//! zig_diff spec: func_800B9C18 -> func_800B9C18.func_800B9C18 (4A0A8.c).
//! Inputs: a u_char pointer that is either a 16-byte arena block or the last
//! two bytes of the scratchpad (so byte [1] is the scratchpad's final byte).
//! Byte 1 is zero, nonzero with the sign bit clear, or nonzero with it set.
//! arg1 is an edge short the C never reads. The two callees are void
//! recording stubs: the C reads nothing after them, so they change no state
//! it reads back.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9C18));

pub const classes = [_][:0]const u8{ "byte1-zero", "byte1-nonzero-sign", "byte1-nonzero-low" };

pub const stubs = struct {
    pub fn func_8007B0FC(ctx: *zd.Ctx) void {
        _ = ctx;
    }
    pub fn func_8007B104(ctx: *zd.Ctx) void {
        _ = ctx;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    var base: [*c]u8 = buf;
    if (ctx.chance(4)) base = @as([*c]u8, @ptrFromInt(zd.scratchpad + zd.scratchpad_size - 2));
    base[1] = switch (ctx.range(u8, 0, 2)) {
        0 => 0,
        1 => ctx.range(u8, 1, 127),
        else => ctx.range(u8, 128, 255),
    };
    const s = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ base, s };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    return if (b >= 128) 1 else 2;
}
