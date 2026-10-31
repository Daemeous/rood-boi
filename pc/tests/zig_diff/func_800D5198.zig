//! zig_diff spec: func_800D5198 -> func_800D5198.func_800D5198 (5BF94.c).
//! Inputs: the argument, null or a random arena object (the C only passes it
//! on, and the stub never reads it), and the two bytes func_800D5170 returns,
//! drawn fresh per call so lo and hi cover every byte value, including bit 7
//! of hi (the bit that reaches bit 15 of the result).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5198));

pub const classes = [_][:0]const u8{ "null-arg", "object-arg" };

pub const stubs = struct {
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        _ = p;
        return ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: ?*v.D_800F53B8_t = if (ctx.chance(4)) null else ctx.alloc(v.D_800F53B8_t);
    return .{p};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
