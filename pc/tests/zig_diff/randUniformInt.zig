//! zig_diff spec: vs_battle_randUniformInt -> rand_uniform_int.randUniformInt (5BF94.c).
//! Inputs: the bounds at the int edges and small values, equal a third of
//! the time, either order (the span wraps for far bounds); rand is a stub
//! returning 0..32767 (the C library's range).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").randUniformInt));

pub const classes = [_][:0]const u8{ "equal", "descending", "ascending" };

pub const stubs = struct {
    pub fn rand(ctx: *zd.Ctx) c_int {
        return ctx.value(u15);
    }
};

const ends = [_]c_int{ 0, 1, -1, 2, 10, 32767, 32768, std.math.maxInt(c_int), std.math.minInt(c_int) };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.pick(c_int, &ends);
    const b = if (ctx.range(u8, 0, 2) == 0) a else ctx.pick(c_int, &ends);
    return .{ a, b };
}

pub fn classify(args: Args) usize {
    if (args[0] == args[1]) return 0;
    return if (args[1] < args[0]) 1 else 2;
}
