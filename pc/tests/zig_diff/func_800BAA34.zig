//! zig_diff spec: func_800BAA34 -> func_800BAA34.func_800BAA34 (4A0A8.c).
//! The C ignores both arguments and returns `func_8007BF0C() != 1`, so the
//! inputs only vary the two arguments (arg0 null or pointing into the arena,
//! arg1 at its edges). The callee is a recording stub whose result covers 1
//! (the false outcome), 0, -1, 2 and any int (the true outcome). The C's only
//! decision is that comparison, so both outcomes are taken on every class.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAA34));

pub const classes = [_][:0]const u8{ "arg0-null", "arg0-valid" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var buf: [*c]u8 = null;
    if (!ctx.chance(4)) buf = ctx.allocBytes(8, 1);
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}

pub const stubs = struct {
    pub fn func_8007BF0C(ctx: *zd.Ctx) c_int {
        return switch (ctx.value(u3)) {
            0 => 1,
            1 => 0,
            2 => -1,
            3 => 2,
            else => ctx.value(c_int),
        };
    }
};
