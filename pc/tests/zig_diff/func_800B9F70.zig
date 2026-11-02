//! zig_diff spec: func_800B9F70 -> func_800B9F70.func_800B9F70 (4A0A8.c).
//! The C ignores arg0 and arg1 and returns whether func_8008F0FC() is zero.
//! Inputs: arg0 null or pointing at arena bytes (never read), arg1 across its
//! edges. The callee stub returns 0, 1, -1, 256 (low byte zero but nonzero,
//! which catches a masked comparison) and any c_int, so the == 0 test sees
//! both zero and nonzero values, with and without the sign bit.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9F70));

pub const classes = [_][:0]const u8{ "null-arg0", "negative-arg1", "other" };

pub const stubs = struct {
    pub fn func_8008F0FC(ctx: *zd.Ctx) c_int {
        return switch (ctx.value(u2)) {
            0 => 0,
            1 => 256,
            2 => -1,
            else => ctx.value(c_int),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: [*c]u8 = if (ctx.chance(3)) null else ctx.allocBytes(16, 16);
    const arg1 = ctx.pick(i16, &.{ 0, 1, -1, 255, 256, 32767, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    if (args[0] == null) return 0;
    if (args[1] < 0) return 1;
    return 2;
}
