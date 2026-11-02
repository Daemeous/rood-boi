//! zig_diff spec (the snow; built from the rain's): func_8008FDC4 -> func_8008FDC4.func_8008FDC4 (146C.c).
//! Inputs: 0..6 arena drops, falling (unkE 0 or 2) or landed (frame 0..9),
//! at the 128 and 16-bit edges; the wind random; the tick speed 0..4; the
//! pause flag D_800F1BA4. The scratchpad's packet cursor and ordering table
//! are arena blocks. RotTransPers' stubs write screen points at the clip
//! edges (x -640/-639/0/1/319/320/959/960, y 0/1/239/240) into the packets
//! and return depths around 4/5, 2047/2048 and the splash's 64/65/2111/2112;
//! the floor stubs a cell (a hole or not) or none, and a height.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008FDC4));

pub const classes = [_][:0]const u8{ "none", "drops" };

fn point(ctx: *zd.Ctx) c_int {
    const x = ctx.edge(i16, &.{ -640, -639, -55, -54, 0, 1, 160, 319, 320, 373, 374, 959, 960 });
    const y = ctx.edge(i16, &.{ 0, 1, 120, 239, 240 });
    return @as(c_int, @as(u16, @bitCast(x))) | (@as(c_int, y) << 16);
}

/// The vertex a projection stub is given (the C's are stack locals the
/// trace only tags), copied into the arena.
fn record(ctx: *zd.Ctx, vertex: anytype) void {
    const seen = ctx.alloc(v.SVECTOR);
    seen.vx = vertex[0].vx;
    seen.vy = vertex[0].vy;
    seen.vz = vertex[0].vz;
}

pub const stubs = struct {
    pub fn RotTransPers(ctx: *zd.Ctx, in: anytype, sxy: anytype, p: anytype, flag: anytype) c_int {
        record(ctx, in);
        sxy[0] = point(ctx);
        p[0] = 0;
        flag[0] = 0;
        return ctx.edge(c_int, &.{ 4, 5, 100, 2047, 2048, -1 });
    }
    pub fn rand(ctx: *zd.Ctx) c_int {
        return ctx.edge(c_int, &.{ 0, 7, 15, 64, 128, 0x7FFF, 1 });
    }
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.edge(c_int, &.{ 0, 64, -64, 4096, -4096, 700 });
    }
    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.edge(c_int, &.{ 0, 64, -64, 4096, -4096, 700 });
    }
    pub fn RotTransPers4(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype, s0: anytype, s1: anytype, s2: anytype, s3: anytype, p: anytype, flag: anytype) c_int {
        record(ctx, a);
        record(ctx, b);
        record(ctx, cc);
        record(ctx, d);
        s0[0] = point(ctx);
        s1[0] = point(ctx);
        s2[0] = point(ctx);
        s3[0] = point(ctx);
        p[0] = 0;
        flag[0] = 0;
        return ctx.edge(c_int, &.{ 64, 65, 66, 1000, 2111, 2112, 0 });
    }
    pub fn func_8008B764(ctx: *zd.Ctx, x: anytype, z: anytype, layer: anytype) ?*anyopaque {
        _ = x;
        _ = z;
        _ = layer;
        if (ctx.value(u2) == 0) return null;
        const cell: *u32 = @ptrCast(@alignCast(ctx.allocBytes(4, 4)));
        cell.* = if (ctx.value(u1) == 0) 31 else ctx.value(u32);
        return cell;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(16 * @sizeOf(v.TILE), 16))));
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ctx.allocBytes(2112 * 4, 16))));
    const n = ctx.range(i16, 0, 6);
    v.g.D_800F1DC0().* = n;
    const drops: [*]v.D_800F1DD4_t = @ptrCast(@alignCast(ctx.allocBytes(8 * @sizeOf(v.D_800F1DD4_t), 16)));
    for (0..8) |k| {
        const d = &drops[k];
        d.unkC = if (ctx.chance(3)) 1 else 0;
        d.unkD = ctx.edge(u8, &.{ 0, 1, 7, 8, 9, 31, 32, 33 });
        d.unkF = ctx.int(u8);
        d.unkE = ctx.edge(u8, &.{ 0, 2 });
        d.unk0 = ctx.pick(i16, &.{ 0, 127, 128, -128, -129, 0x7FFF });
        d.unk2 = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
        d.unk4 = ctx.pick(i16, &.{ 0, 127, 128, -128, -129, 0x7FFF });
    }
    v.g.D_800F1DD4().* = &drops[0];
    v.g.D_800F1BA4().* = if (ctx.chance(4)) 1 else 0;
    v.g.pc_gte_widescreen().* = if (ctx.chance(2)) 1 else 0;
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 2, 2, 4, 1, 0 });
    return .{};
}

pub fn classify(_: Args) usize {
    return if (v.g.D_800F1DC0().* == 0) 0 else 1;
}
