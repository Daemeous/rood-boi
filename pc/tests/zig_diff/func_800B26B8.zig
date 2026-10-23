//! zig_diff spec: func_800B26B8 -> func_800B26B8.func_800B26B8 (49268.c).
//! Inputs: an actor block in the arena, 4 KB past its start so the negative
//! saved offsets stay inside it; a random parent MATRIX; bone 0, because the
//! table D_800F3C58 has one entry in the harness, so bones 1-3 spill at most 96
//! bytes past it into the state region, which both runs write identically and
//! the compare covers (the reviewer's change: with bone always 0 a port that
//! ignored the bone passed);
//! and saved over -4..40 (both signs, and the reads reach the far end of the
//! block) or, a quarter of the time, 0x08000000 + a few, where saved << 5 wraps
//! to a small offset. The stubs of func_800B196C rewrite the matrix it is
//! passed (the C copies that result out), and the GTE reads are recording
//! stubs whose values come from ctx.value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_49268");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B26B8));

pub const classes = [_][:0]const u8{ "saved-small", "saved-negative", "saved-wrapped" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16384, 16);
    const actor: ?*anyopaque = @ptrCast(buf + 4096);
    const parent = ctx.alloc(v.MATRIX);
    const saved: c_int = if (ctx.chance(4))
        0x0800_0000 + ctx.range(i32, -3, 3)
    else
        ctx.range(i32, -4, 40);
    return .{ actor, parent, ctx.range(c_int, 0, 3), saved };
}

pub fn classify(args: Args) usize {
    const s = args[3];
    if (s < 0) return 1;
    if (s >= 0x0800_0000) return 2;
    return 0;
}

pub const stubs = struct {
    pub fn func_800B196C(ctx: *zd.Ctx, parent: anytype, matrix: anytype) void {
        _ = parent;
        const m: *v.MATRIX = @ptrCast(@alignCast(matrix));
        for (&m.m) |*row| {
            for (row) |*e| e.* = ctx.value(i16);
        }
        for (&m.t) |*e| e.* = ctx.value(i32);
    }
};
