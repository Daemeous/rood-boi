//! zig_diff spec: func_800AAA88 -> func_800AAA88.func_800AAA88 (40564.c).
//! Inputs: an object D_800F4538_t in the arena, stored in D_800F4538[arg0] for arg0 in 0..16,
//! with random fields except the ones the C reads back: pitch, facing and roll from edges
//! (0, 4095, 4096, -1, 32767, -32768, so the `&= 0xFFF` and i16 wraps show), and the scale
//! unk5C0 from edges including 0, 4096 and 8192 (with |v| = 32768 those give m = 0, 2^27 and
//! 2^28, so unk18 becomes 0x8000 or wraps to 0 and takes the goto). The SVECTOR arg1 is an
//! arena object whose components come from small edges (0, +-1, +-4096, 32767, -32768), so
//! ABS ties, sums overflow and both signs of every ABS arm occur. arg2 is 0 (recenter), -1
//! (scaled: the class says whether unk18 comes out 0 and goes to recenter, or nonzero and
//! returns) or another value (stored truncated to u16). The product m * unk5C0 stays below
//! 2^31, so m is never negative: the `m & 0xFFF` test never sees a set sign bit.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AAA88));

pub const classes = [_][:0]const u8{ "recenter", "scaled-wraps-to-zero", "scaled-returns", "set-unk18" };

const vec_edges = [_]i16{ 0, 1, -1, 4096, -4096, 32767, -32768, 4095, -4095, 100, -100 };
const ang_edges = [_]i16{ 0, 4095, 4096, -1, 32767, -32768 };
const scale_edges = [_]u16{ 0, 1, 4095, 4096, 8191, 8192, 32768, 65535 };
const arg2_edges = [_]c_int{ 1, -2, 255, 256, 65536, 0x7FFF_FFFF, -256 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const sv = ctx.alloc(v.SVECTOR);
    const idx: c_int = ctx.range(c_int, 0, 16);
    v.g.D_800F4538()[@intCast(idx)] = actor;

    sv.vx = ctx.pick(i16, &vec_edges);
    sv.vy = ctx.pick(i16, &vec_edges);
    sv.vz = ctx.pick(i16, &vec_edges);

    actor.unk5C0 = ctx.pick(u16, &scale_edges);
    actor.unk0.pitch = ctx.pick(i16, &ang_edges);
    actor.unk0.facing = ctx.pick(i16, &ang_edges);
    actor.unk0.roll = ctx.pick(i16, &ang_edges);

    const mode = ctx.range(u8, 0, 9);
    const arg2: c_int = if (mode <= 2)
        0
    else if (mode <= 6)
        -1
    else
        ctx.edge(c_int, &arg2_edges);

    return .{ idx, sv, arg2 };
}

fn absv(x: i16) i64 {
    const y: i64 = x;
    return if (y < 0) -y else y;
}

pub fn classify(args: Args) usize {
    if (args[2] == 0) return 0;
    if (args[2] != -1) return 3;
    const a = v.g.D_800F4538()[@intCast(args[0])].?;
    const sv = args[1].?;
    var m: i64 = @max(@max(absv(sv.vx), absv(sv.vy)), absv(sv.vz));
    m *= @as(i64, a.unk5C0);
    var units = @divTrunc(m, 4096);
    if ((m & 0xFFF) != 0) units += 1;
    const unk18: u16 = @truncate(@as(u64, @intCast(units)));
    return if (unk18 == 0) 1 else 2;
}
