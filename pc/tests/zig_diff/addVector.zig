//! zig_diff spec: vs_battle_addVector -> add_vector.addVector (5BF94.c).
//! The C has no branches and no calls: it adds three components. The inputs are
//! three VECTORs in the arena, each component drawn from edge values (0, +-1,
//! the i32 extremes, a power of two) so the sums overflow and wrap. The output
//! is distinct from both sources most of the time; in the other cases it aliases
//! arg0 or arg1, or both sources are the same vector, so the store order against
//! the reads is exercised. The classes name those four aliasing cases.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").addVector));

pub const classes = [_][:0]const u8{ "distinct", "out-is-arg0", "out-is-arg1", "sources-same" };

const edges = &[_]i32{ 0, -1, 1, 2, 0x7FFF_FFFF, -0x8000_0000, 0x4000_0000, -0x4000_0000 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.VECTOR);
    const b = ctx.alloc(v.VECTOR);
    const out = ctx.alloc(v.VECTOR);
    for ([_]*v.VECTOR{ a, b, out }) |p| {
        p.vx = ctx.pick(i32, edges);
        p.vy = ctx.pick(i32, edges);
        p.vz = ctx.pick(i32, edges);
        p.pad = ctx.int(i32);
    }
    // arg1 is sometimes arg0 too ("sources-same" when the output is distinct).
    const src1 = if (ctx.chance(4)) a else b;
    // The output: a fresh vector, or one of the sources.
    const dst = switch (ctx.range(u8, 0, 5)) {
        0 => a,
        1 => src1,
        else => out,
    };
    return .{ a, src1, dst };
}

pub fn classify(args: Args) usize {
    const x = args[0].?;
    const y = args[1].?;
    const z = args[2].?;
    if (z == x) return 1;
    if (z == y) return 2;
    if (x == y) return 3;
    return 0;
}
