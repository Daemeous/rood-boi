//! zig_diff spec: func_8009EFEC -> func_8009EFEC.func_8009EFEC (30DB0.c).
//! Inputs: an actor in an arena block, put in table slot 0..16 (the only slot
//! the C reads), with facing 0/0x800/-0x800/extremes and unk63C around 0x80 and
//! 0x81; a target that is null, shares x or z with the actor (sometimes both,
//! so dx==0 and dz==0 both occur), or overflows dx (positions -32768 vs 32767);
//! and mode 0, 1, 2 or -1. The callees are recording stubs. The values
//! ratan2 and func_8009E228 return are chosen by prepare from the compared
//! edges (-0xE5, -0xE4, 0xAB, 0xAC, plus 0x400 and 0xC00 for ratan2, which
//! give angle 0x800 and -0x800 with facing 0 and 0x800), so both sides of
//! each ordered compare are equal in many runs. The animation output is NULL
//! or an arena block, and the table slot is sometimes cleared.
//! func_800A1AF8 writes origin, and func_8009E180 rewrites the fallback's
//! unk63C, both of which the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009EFEC));

pub const classes = [_][:0]const u8{ "mode0-target", "mode0-null", "mode1-target", "mode1-null", "other-mode" };

/// The table slot prepare filled; the stubs clear it sometimes.
var cur_id: usize = 0;
/// The return values prepare chose for this iteration (the same in both runs).
var ratan_ret: c_int = 0;
var pitch_ret: c_int = 0;

const ratan_edges = [_]c_int{ -0xE5, -0xE4, 0xAB, 0xAC, 0x400, 0xC00, 0 };
const pitch_edges = [_]c_int{ -0xE5, -0xE4, 0xAB, 0xAC, 0x400, 0xC00, 0 };

fn asActor(p: anytype) *v.D_800F4538_t {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrFromInt(@intFromPtr(q));
}

fn pickU16(ctx: *zd.Ctx) u16 {
    return switch (ctx.value(u8) % 5) {
        0 => 0x7F,
        1 => 0x80,
        2 => 0x81,
        3 => 0xFFFF,
        else => ctx.value(u16),
    };
}

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = ctx;
        _ = y;
        _ = x;
        return ratan_ret;
    }

    pub fn func_800A1AF8(ctx: *zd.Ctx, id: anytype, zero: anytype, origin: anytype, flag: anytype) void {
        _ = id;
        _ = zero;
        _ = flag;
        const o: *v.SVECTOR = @ptrFromInt(@intFromPtr(origin));
        o.vx = ctx.value(i16);
        o.vy = ctx.value(i16);
        o.vz = ctx.value(i16);
    }

    pub fn func_8009E180(ctx: *zd.Ctx, a: anytype, t: anytype) c_int {
        _ = t;
        if (ctx.value(u1) == 1) asActor(a).unk63C = pickU16(ctx);
        return ctx.value(c_int);
    }

    pub fn func_8009E228(ctx: *zd.Ctx, a: anytype, t: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = t;
        return pitch_ret;
    }

    pub fn func_800AD494(ctx: *zd.Ctx, a: anytype, id: anytype, out: anytype) c_int {
        _ = id;
        const actor = asActor(a);
        if (ctx.value(u1) == 1) actor.unk63C = pickU16(ctx);
        if (ctx.value(u3) == 0) v.g.D_800F4538()[cur_id] = null;
        if (ctx.value(u1) == 1) {
            out[0] = null;
        } else {
            out[0] = @ptrCast(ctx.alloc(u16));
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actorId = ctx.range(c_int, 0, 16);
    cur_id = @intCast(actorId);
    ratan_ret = ctx.edge(c_int, &ratan_edges);
    pitch_ret = ctx.edge(c_int, &pitch_edges);

    const actor = ctx.alloc(v.D_800F4538_t);
    v.g.D_800F4538()[cur_id] = actor;
    actor.unk0.facing = ctx.pick(i16, &.{ 0, 0x800, -0x800, 0x7FFF, -0x8000 });
    actor.unk63C = ctx.pick(u16, &.{ 0x7F, 0x80, 0x81, 0xFFFF });

    const target = ctx.alloc(v.SVECTOR);
    target.vx = ctx.int(i16);
    target.vy = ctx.int(i16);
    target.vz = ctx.int(i16);
    if (ctx.chance(4)) target.vx = actor.unk0.position.vx;
    if (ctx.chance(4)) target.vz = actor.unk0.position.vz;
    if (ctx.chance(6)) {
        actor.unk0.position.vx = -32768;
        target.vx = 32767;
    }
    const tgt: ?*v.SVECTOR = if (ctx.chance(6)) null else target;
    const mode = ctx.edge(c_int, &.{ 0, 1, 2, -1 });
    return .{ actorId, tgt, mode };
}

pub fn classify(args: Args) usize {
    const has = args[1] != null;
    return switch (args[2]) {
        0 => if (has) 0 else 1,
        1 => if (has) 2 else 3,
        else => 4,
    };
}
