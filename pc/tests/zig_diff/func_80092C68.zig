//! zig_diff spec: func_80092C68 -> func_80092C68.func_80092C68 (2842C.c).
//! Inputs: two objects in the arena, never null (the C dereferences both). The
//! three axes have coordinates from a fixed edge list (0, +-16, large values
//! that keep the C's own arithmetic in range, and others), and the bases are
//! chosen so that each difference is a small delta (0, +-1..3, +-5000,
//! +-40000). That makes the `x >= 0` ABS sides, the `d > 0` tests and the
//! `< 0` tests see both signs and equal sides. The limit u16 is often |delta|,
//! so `d == 0` happens, and it takes the sign bit and 0xFFFF. The C makes no
//! external calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092C68));

pub const classes = [_][:0]const u8{ "wide-x", "d-not-positive", "d-positive-x-negative", "d-positive-x-nonnegative" };

// Kept well inside i32 so that `coord - (s16 << 4)` and `ABS` never overflow
// in the C itself (signed overflow is undefined behaviour there).
const coords = [_]i32{ 0, 16, -16, 32, -32, 15, -15, 17, 0x3FFF_FFF0, -0x3FFF_FFF0, 0x12340, -0x12345, 0x00FF_FFF0, 0x7FF0, -0x7FF0 };
const deltas = [_]i32{ 0, 0, 1, -1, 2, -2, 3, -3, 5000, -5000, 40000, -40000 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.func_80092F74_t);
    const b = ctx.alloc(v.func_80092F74_t2);

    const xc = ctx.edge(i32, &coords);
    const yc = ctx.edge(i32, &coords);
    const zc = ctx.edge(i32, &coords);
    a.unkC = xc;
    a.unk10 = yc;
    a.unk14 = zc;

    const dx = ctx.edge(i32, &deltas);
    const dy = ctx.edge(i32, &deltas);
    const dz = ctx.edge(i32, &deltas);
    b.unk4[0].s16[0] = @as(i16, @truncate(@divTrunc(xc, 16) - dx));
    b.unk4[0].s16[1] = @as(i16, @truncate(@divTrunc(yc, 16) - dy));
    b.unk4[1].s16[0] = @as(i16, @truncate(@divTrunc(zc, 16) - dz));

    // The limit is often exactly one axis's delta, so `d == 0` happens.
    const from = switch (ctx.range(u8, 0, 3)) {
        0 => dx,
        1 => dy,
        2 => dz,
        else => 1,
    };
    b.unk4[1].u16[1] = if (ctx.chance(2))
        @as(u16, @intCast(@min(@abs(from), 0xFFFF)))
    else
        ctx.pick(u16, &.{ 0, 1, 2, 3, 0xFFFF, 0x8000 });

    b.unk4[2].u16[0] = ctx.pick(u16, &.{ 0xFFFF, 0, 0x7FFF, 0x8000 });

    return .{ a, b };
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const b = args[1].?;
    const x: i64 = @as(i64, @divTrunc(a.unkC, 16)) - b.unk4[0].s16[0];
    const ax: i64 = if (x < 0) -x else x;
    if (ax > 65535) return 0;
    const limit: i64 = b.unk4[1].u16[1];
    if (limit <= ax) return 1;
    return if (x < 0) 2 else 3;
}
