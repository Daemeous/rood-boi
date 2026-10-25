//! zig_diff spec: func_8009D208 -> func_8009D208.func_8009D208 (30DB0.c).
//! Inputs: arg0 is 0..3 (the game passes 1..3: `arg + 1` of the three
//! sound-bank slots; D_800F244F is `u_char[]` at 0x800F244F, so [1..3] are the
//! bytes after the symbol, which the harness maps as part of the same state
//! section). The addressed byte is zero or any byte (sign bit set or clear), so
//! both arms of the entry test run. func_800AB2AC is a recording stub that rewrites
//! the table byte, which the C re-reads as the argument of func_8007E0A8.
//! func_8007E0A8 returns 0 about a third of the time (the -3 path) and an
//! odd nonzero int otherwise, which can be negative.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D208));

pub const classes = [_][:0]const u8{ "entry-zero", "entry-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F244F();
    for (0..4) |k| table[k] = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    const arg: c_int = ctx.range(c_int, 0, 3);
    return .{arg};
}

pub fn classify(args: Args) usize {
    return if (v.g.D_800F244F()[@intCast(args[0])] == 0) 0 else 1;
}

pub const stubs = struct {
    pub fn func_800AB2AC(ctx: *zd.Ctx, a: anytype) void {
        // The callee's argument is arg0 - 1: it rewrites the entry the C re-reads (arg0).
        v.g.D_800F244F()[@intCast(a + 1)] = ctx.value(u8);
    }

    pub fn func_8007E0A8(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int) | 1;
    }
};
