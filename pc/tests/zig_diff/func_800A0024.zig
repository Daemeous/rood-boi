//! zig_diff spec: func_800A0024 -> func_800A0024.func_800A0024 (30DB0.c).
//! The C tests an uninitialised pointer before setting it; GCC folds the
//! function to `return -1` at -O2 (the game's objects, 32- and 64-bit). The
//! inputs still give the rest of the body valid data: an index 0..15 into
//! D_800F45E0 (an arena object, or null), an out block, so the coverage
//! lane's -O0 copy (where the test reads a stack slot) can run it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0024));

pub const classes = [_][:0]const u8{"any"};

pub const stubs = struct {
    /// Gives unk1E (the next short after unk1C) back sometimes: the -O0 copy's `< 0` meets 0.
    pub fn func_800A6EE8(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = d;
        const s: [*]const i16 = @ptrCast(@alignCast(p));
        return if (ctx.value(u8) & 3 == 0) s[1] else ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const objects = v.g.D_800F45E0();
    for (objects) |*slot| {
        const o = ctx.alloc(v.D_800F45E0_t);
        o.unk12 = if (ctx.chance(2)) 0xFF else ctx.int(u8);
        o.unk1A = if (ctx.chance(2)) 0 else ctx.int(i16);
        slot.* = o;
    }
    const index = ctx.range(c_int, 0, 15);
    const out = ctx.alloc(v.func_800A0024_t);
    return .{ index, out };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
