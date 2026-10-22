//! zig_diff spec: func_800D78F0 -> func_800D78F0.func_800D78F0 (6F014.c).
//! Inputs: the global D_800F569C points at an arena block whose block9Data is a
//! pFileBlock9 in the arena with random bytes. Its unk12 and unk13 are table
//! indices into D_800F5330 (33 entries), so each is kept below 33: half the
//! time an edge (0, 1, 31, 32), otherwise any value folded into 0..32. The
//! C has no branches of its own, so the classes split on whether both indices
//! sit at the table's edges (0 or 32). The three callees are stubs: the lerp
//! fills its output VECTOR, the scalar callee returns a value, and the two
//! consumers may rewrite what they are given, so a port that reads the
//! results too early or in the wrong order disagrees.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D78F0));

pub const classes = [_][:0]const u8{ "edge-indices", "interior-indices" };

pub const stubs = struct {
    pub fn vs_battle_lerpVector(ctx: *zd.Ctx, src: anytype, t: anytype, vec: anytype) void {
        _ = src;
        _ = t;
        const out: *v.VECTOR = @ptrCast(@alignCast(vec));
        out.vx = ctx.value(i32);
        out.vy = ctx.value(i32);
        out.vz = ctx.value(i32);
    }

    pub fn func_800CFE1C(ctx: *zd.Ctx, src: anytype, t: anytype) c_int {
        _ = src;
        _ = t;
        return ctx.value(c_int);
    }

    pub fn func_800D5260(ctx: *zd.Ctx, vec: anytype) void {
        const out: *v.VECTOR = @ptrCast(@alignCast(vec));
        if (ctx.value(u1) == 1) out.pad = ctx.value(i32);
    }

    pub fn func_800D5294(ctx: *zd.Ctx, p: anytype) void {
        const out: *c_int = @ptrCast(@alignCast(p));
        out.* = ctx.value(c_int);
    }
};

fn index(ctx: *zd.Ctx) u8 {
    // Every value is folded below 33, the entries D_800F5330 has.
    return ctx.pick(u8, &.{ 0, 1, 31, 32 }) % 33;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const blk = ctx.alloc(v.pFileBlock9);
    const d = ctx.alloc(v.D_800F569C_t);
    psx.put(&d.block9Data, blk);
    v.g.D_800F569C().* = d;
    blk.unk12 = index(ctx);
    blk.unk13 = index(ctx);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const blk = psx.get(v.g.D_800F569C().*.?.block9Data).?;
    const edge_a = blk.unk12 == 0 or blk.unk12 == 32;
    const edge_b = blk.unk13 == 0 or blk.unk13 == 32;
    return if (edge_a and edge_b) 0 else 1;
}
