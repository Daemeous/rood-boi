//! zig_diff spec: func_800BB604 -> func_800BB604.func_800BB604 (4A0A8.c).
//! Inputs: arg0 is null or an arena block and arg1 an edge short; the C reads
//! neither. The eight vs_battle_dismissTextBox results decide the path. The
//! spec picks a mode per iteration: every result zero (the `return 2` path
//! with the func_8007C36C call), only the last result nonzero (the count is
//! exactly 1, the boundary of `var_s1 == 0`), all results random (negative
//! and positive nonzero values), or a mix of zero and random results. Each
//! stub result is one the C tests for (zero or nonzero, with and without the
//! sign bit).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB604));

pub const classes = [_][:0]const u8{ "none-dismissed", "last-only", "all-random", "mixed" };

/// Set by prepare, read by the stubs (the same in both runs).
var mode: u8 = 0;

pub const stubs = struct {
    pub fn vs_battle_dismissTextBox(ctx: *zd.Ctx, i: anytype) c_int {
        switch (mode) {
            0 => return 0,
            1 => {
                if (i != 7) return 0;
                const x = ctx.value(c_int);
                return if (x == 0) 1 else x;
            },
            2 => return ctx.value(c_int),
            else => {
                if (ctx.value(u2) == 0) return 0;
                return ctx.value(c_int);
            },
        }
    }

    pub fn func_8007C36C(ctx: *zd.Ctx, arg0: anytype) c_int {
        _ = arg0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.edge(u8, &.{ 0, 1, 2, 3 });
    const arg0: [*c]u8 = if (ctx.chance(4)) null else ctx.allocBytes(16, 1);
    const arg1 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
