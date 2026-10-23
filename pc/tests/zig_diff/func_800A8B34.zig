//! zig_diff spec: func_800A8B34 -> func_800A8B34.func_800A8B34 (3A1A0.c).
//! Inputs: an actor in the arena (busy-flag bitfield 0 or nonzero, its wall
//! limit unk1E set to meet the wall test exactly or at random), an SVECTOR
//! (vx/vz at the 128 and clamp edges), a scratchpad block with unk0, unk40 and
//! unk1C, and arg1 across its edges (its low three bits index unk1C). The
//! ground, ceiling, entity, LOS and clearance callees return per-iteration
//! values chosen by prepare (spec vars), so the comparisons see equal sides:
//! the ceiling test (unk1C + unk0 against the height), the ground test (h
//! against 0), the wall test (h - unk1E against -383), the sink test (unk0
//! against elev2 - h), the floor test (h against unk40 + limit) and q against 0.
//! Every return path (busy, above ceiling, no entity, no ground, wall, sink,
//! under floor, blocked, accepted with q negative or zero) is a class.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A8B34));

pub const classes = [_][:0]const u8{
    "busy",
    "above-ceiling",
    "no-entity",
    "no-ground",
    "wall",
    "sink",
    "under-floor",
    "blocked",
    "accepted",
};

// Values the stubs return this iteration; prepare sets them.
var elev_ret: i16 = 0;
var dc_ret: i16 = 0;
var r_ret: i32 = 0;
var los_ret: i32 = 0;
var e8e_ret: i32 = 0;
var ent_null: bool = false;
var ent_bit: bool = false;
var klass: usize = 0;

pub const stubs = struct {
    pub fn func_8008DD0C(ctx: *zd.Ctx, a: anytype, b: anytype) i16 {
        _ = ctx;
        _ = a;
        _ = b;
        return elev_ret;
    }

    pub fn func_8008DC7C(ctx: *zd.Ctx, a: anytype, b: anytype) i16 {
        _ = ctx;
        _ = a;
        _ = b;
        return dc_ret;
    }

    // The entity's flags word: bit 17 is the C's test, the rest is random.
    pub fn func_800A8D64(ctx: *zd.Ctx, p: anytype, k: anytype) ?*c_uint {
        _ = p;
        _ = k;
        if (ent_null) return null;
        const e = ctx.alloc(c_uint);
        const keep: u32 = ctx.value(u32) & ~(@as(u32, 1) << 17);
        e.* = keep | (@as(u32, @intFromBool(ent_bit)) << 17);
        return e;
    }

    pub fn func_800A92B8(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        return r_ret;
    }

    pub fn func_800A9378(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        return los_ret;
    }

    // The clearance check may rewrite the vector's pad word.
    pub fn func_800A8E84(ctx: *zd.Ctx, actor: anytype, sv: anytype) c_int {
        _ = actor;
        const s: *v.SVECTOR = @ptrCast(@alignCast(sv));
        s.pad = ctx.value(i16);
        return e8e_ret;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const sb: *v.D_1F8003BC_t = @ptrFromInt(0x1F8003BC);
    const actor = ctx.alloc(v.D_800F45E0_t);
    const arg0 = ctx.alloc(v.SVECTOR);
    const arg1 = ctx.pick(c_int, &.{ 0, 1, 7, 8, -1, 15, 0x7FFF_FFFF, -0x8000_0000 });

    psx.put(&sb.unk18, actor);

    sb.unk0 = if (ctx.chance(2)) ctx.range(i32, -3000, 3000) else ctx.pick(i32, &.{ 0, 1, -1, 0x3FFF_FFFF, -0x4000_0000 });
    for (&sb.unk1C) |*x| {
        x.* = if (ctx.chance(2)) ctx.range(i16, -2000, 2000) else ctx.pick(i16, &.{ 0, -1, 0x7FFF, -0x8000 });
    }
    const idx: usize = @intCast(arg1 & 7);
    const limit: i32 = sb.unk1C[idx];

    const bits: u2 = if (ctx.chance(8)) ctx.range(u2, 1, 3) else 0;
    psx.setBits(actor, "unk5AC_9", bits);

    arg0.vx = ctx.pick(i16, &.{ 0, 127, 128, -128, -129, 32767, -32768 });
    arg0.vy = ctx.int(i16);
    arg0.vz = ctx.pick(i16, &.{ 0, 127, 128, -128, -129, 32767, -32768 });
    arg0.pad = ctx.int(i16);

    // Ceiling: elev2 (the ceiling as the C sign-extends its 15 bits).
    const sum: i32 = limit + sb.unk0;
    const fits15 = sum >= -16384 and sum < 16384;
    elev_ret = blk: {
        if (fits15 and ctx.chance(3)) break :blk @as(i16, @truncate(sum));
        if (fits15 and ctx.chance(2)) break :blk @as(i16, @truncate(sum - ctx.range(i32, 1, 200)));
        break :blk ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    };
    const elev2: i32 = @as(i15, @truncate(elev_ret));

    // Ground height, sign-extended to 15 bits after the non-negative test.
    dc_ret = ctx.pick(i16, &.{ 0, 1, -1, 0x3FFF, 0x4000, 0x7FFF, -0x8000 });
    const h0: i32 = @as(i15, @truncate(dc_ret));

    ent_null = ctx.chance(8);
    ent_bit = ctx.chance(2);
    r_ret = if (ctx.chance(2)) 0 else ctx.pick(i32, &.{ 1, -1, 0x7FFF, -0x8000 });
    var hp: i32 = if (r_ret != 0) r_ret else h0;
    if (ent_bit and hp >= 0) hp = 0xBB8;

    // Wall: h - unk1E against -383.
    const want: i32 = hp + 383;
    actor.unk1E = if (want >= -32768 and want <= 32767 and ctx.chance(3))
        @as(i16, @intCast(want))
    else
        ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });

    // Clearance: los 0 keeps h, anything else replaces it.
    const los_mode = ctx.range(u8, 0, 3);
    los_ret = switch (los_mode) {
        0 => 0,
        1 => elev2 - sb.unk0,
        2 => limit + ctx.range(i32, -70, 70),
        else => ctx.pick(i32, &.{ 1, -1, 0x7FFF, -0x8000 }),
    };
    const hF: i32 = if (los_ret == 0) hp else los_ret;

    // Floor: h against unk40 + limit.
    sb.unk40 = if (ctx.chance(3)) hF -% limit else ctx.pick(i32, &.{ 0, 1, -1, 0x7FFF, -0x8000 });

    e8e_ret = if (ctx.chance(3)) 0 else ctx.pick(i32, &.{ 1, -1 });

    klass = if (bits != 0)
        0
    else if (sum < elev2)
        1
    else if (ent_null)
        2
    else if (dc_ret < 0)
        3
    else if (hp -% @as(i32, actor.unk1E) < -0x17F)
        4
    else if (sb.unk0 < elev2 - hF)
        5
    else if (hF < sb.unk40 +% limit)
        6
    else if (e8e_ret == 0)
        7
    else
        8;

    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return klass;
}
