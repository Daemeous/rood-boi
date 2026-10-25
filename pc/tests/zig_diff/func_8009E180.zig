//! zig_diff spec: func_8009E180 -> func_8009E180.func_8009E180 (30DB0.c).
//! Inputs: a random D_800F4538_t (position and facing: any i16) and an SVECTOR
//! that is null (the C returns 0 without reading arg0, so arg0 is null half of
//! those too), at the position's own spot (both deltas zero), on one axis only,
//! or off both. The off-both cases pick the angle so that var_v1 lands on the
//! edges of the two wrap tests (0x801, 0x800, -0x800, -0x801, 0) and across the
//! whole range; some use a random ratan2 result. ratan2 is a stub returning the
//! spec's `ratan_result`, which prepare sets.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E180));

pub const classes = [_][:0]const u8{ "null-arg", "same-spot", "dx-zero", "dz-zero", "wrap-high", "wrap-low", "mid" };

var ratan_result: c_int = 0;

const edge_vals = [_]i32{ 0x801, 0x800, 0x7FF, -0x800, -0x801, -0x7FF, 0, -1, 1 };

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = ctx;
        _ = y;
        _ = x;
        return ratan_result;
    }
};

fn nonzero(ctx: *zd.Ctx) i16 {
    return ctx.range(i16, 1, 32767);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const target = ctx.alloc(v.SVECTOR);
    const pos = &actor.unk0.position;
    pos.vx = ctx.int(i16);
    pos.vy = ctx.int(i16);
    pos.vz = ctx.int(i16);
    actor.unk0.facing = ctx.int(i16);
    ratan_result = ctx.int(i16);
    if (ctx.chance(12)) {
        const a0: ?*v.D_800F4538_t = if (ctx.chance(2)) null else actor;
        return .{ a0, null };
    }
    target.vx = ctx.int(i16);
    target.vz = ctx.int(i16);
    const sel = ctx.range(u8, 0, 15);
    if (sel == 0) {
        target.vx = pos.vx;
        target.vz = pos.vz;
    } else if (sel == 1) {
        target.vx = pos.vx;
        target.vz = pos.vz +% nonzero(ctx);
    } else if (sel == 2) {
        target.vz = pos.vz;
        target.vx = pos.vx +% nonzero(ctx);
    } else {
        target.vx = pos.vx +% nonzero(ctx);
        target.vz = pos.vz +% nonzero(ctx);
        const d: i32 = if (ctx.chance(2)) ctx.edge(i32, &edge_vals) else ctx.range(i32, -4096, 4095);
        if (ctx.chance(8)) {
            ratan_result = ctx.range(i32, -40000, 40000);
            actor.unk0.facing = ctx.int(i16);
        } else {
            const t = ctx.range(i32, 0, 4095);
            ratan_result = 0xC00 - t;
            actor.unk0.facing = @intCast(t - d);
        }
    }
    return .{ actor, target };
}

pub fn classify(args: Args) usize {
    const target = args[1] orelse return 0;
    const a = args[0].?;
    const dx = @as(i32, target.vx) - @as(i32, a.unk0.position.vx);
    const dz = @as(i32, target.vz) - @as(i32, a.unk0.position.vz);
    if (dx == 0 and dz == 0) return 1;
    if (dx == 0) return 2;
    if (dz == 0) return 3;
    const t: i32 = (0xC00 - ratan_result) & 0xFFF;
    const d = t - @as(i32, a.unk0.facing);
    if (d >= 0x801) return 4;
    if (d < -0x800) return 5;
    return 6;
}
