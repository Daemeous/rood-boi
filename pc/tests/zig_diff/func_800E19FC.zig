//! zig_diff spec: func_800E19FC -> func_800E19FC.func_800E19FC (771A8.c).
//! Inputs: a terrainMovementState in the arena with its f1A/f29 flags (zero,
//! one, and sign-bit values such as 0x80 and 0xFF), tile and destination
//! coordinates from the edges 0, 1, 31, 32, 255 (indexes past the 32-wide rows),
//! and a TERRAIN_TILES table in the arena whose cells are set to share or
//! differ in their low nibble. The scratchpad word at 0x1F8003C0 points at that
//! table. The route stub returns modes 6, 5 and others, and sometimes moves the
//! destination onto the tile, so the goto path and both equality outcomes run.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E19FC));

pub const classes = [_][:0]const u8{ "f1A-set", "same-cell", "no-f29", "moving" };

fn cellOf(z: u8, x: u8) u16 {
    const sp: *align(1) const u32 = @ptrFromInt(0x1F80_03C0);
    const tbl: [*]const u16 = @ptrFromInt(@as(usize, sp.*));
    return tbl[@as(usize, z) * 32 + x] & 15;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    // TERRAIN_TILES: 256 rows of 32 u16 cells; z*32+x reaches 8415, so 20000 bytes.
    const tbl: [*]u16 = @ptrCast(@alignCast(ctx.allocBytes(20000, 16)));
    const sp: *align(1) u32 = @ptrFromInt(0x1F80_03C0);
    sp.* = psx.addr(tbl);

    const s = ctx.alloc(v.terrainMovementState);
    if (ctx.chance(4)) {
        s.f1A = ctx.pick(u8, &.{ 1, 0x80, 0xFF, 0x7F });
    } else {
        s.f1A = 0;
    }
    s.f29 = ctx.pick(u8, &.{ 0, 1, 2, 0x80, 0xFF, 0x7F });

    s.tile.p.z = ctx.pick(u8, &.{ 0, 1, 31, 32, 255 });
    s.tile.p.x = ctx.pick(u8, &.{ 0, 1, 31, 32, 255 });
    s.destination.p.z = ctx.pick(u8, &.{ 0, 1, 31, 32, 255 });
    s.destination.p.x = ctx.pick(u8, &.{ 0, 1, 31, 32, 255 });

    const ti = @as(usize, s.tile.p.z) * 32 + s.tile.p.x;
    const di = @as(usize, s.destination.p.z) * 32 + s.destination.p.x;
    const cur: u16 = tbl[ti] & 15;
    const want_equal = ctx.chance(3);
    const nib: u16 = if (want_equal) cur else (cur + 1 + ctx.range(u16, 0, 14)) & 15;
    tbl[di] = (tbl[di] & 0xFFF0) | nib;

    // Bounded so that targetX - position[0] (an int subtraction) cannot overflow.
    s.targetX = ctx.range(i32, -0x7FFF_0000, 0x7FFF_0000);
    s.targetZ = ctx.range(i32, -0x7FFF_0000, 0x7FFF_0000);
    return .{s};
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    if (s.f1A != 0) return 0;
    if (cellOf(s.tile.p.z, s.tile.p.x) == cellOf(s.destination.p.z, s.destination.p.x)) return 1;
    if (s.f29 == 0) return 2;
    return 3;
}

pub const stubs = struct {
    // The route step: it moves the destination (sometimes onto the tile, so the
    // goto path's cell comparison can pass) and sometimes the tile, and returns
    // mode 6, mode 5, or another value.
    pub fn func_800DFBCC(ctx: *zd.Ctx, state: anytype, current: anytype, next: anytype) c_int {
        _ = current;
        _ = next;
        const s: *v.terrainMovementState = @ptrCast(@alignCast(state));
        if (ctx.value(u8) < 128) {
            s.destination.p.z = s.tile.p.z;
            s.destination.p.x = s.tile.p.x;
        } else {
            s.destination.p.z = ctx.value(u8);
            s.destination.p.x = ctx.value(u8);
        }
        if (ctx.value(u8) < 64) s.tile.p.x = ctx.value(u8);
        const r = ctx.value(u8);
        if (r < 110) return 6;
        if (r < 150) return 5;
        return @as(c_int, ctx.value(i8));
    }

    pub fn func_800E1908(ctx: *zd.Ctx, state: anytype) void {
        const s: *v.terrainMovementState = @ptrCast(@alignCast(state));
        s.f2 = ctx.value(u8);
    }

    pub fn func_800E4BB8(ctx: *zd.Ctx, state: anytype) void {
        const s: *v.terrainMovementState = @ptrCast(@alignCast(state));
        s.angle = ctx.value(i16);
    }

    pub fn func_800E4B70(ctx: *zd.Ctx, state: anytype, angle: anytype) void {
        _ = angle;
        const s: *v.terrainMovementState = @ptrCast(@alignCast(state));
        s.targetX = ctx.value(i32);
    }

    pub fn func_800DEEFC(ctx: *zd.Ctx, state: anytype, mode: anytype) void {
        _ = mode;
        const s: *v.terrainMovementState = @ptrCast(@alignCast(state));
        s.f4 = ctx.value(u8);
    }

    pub fn func_800E3600(ctx: *zd.Ctx, state: anytype, angle: anytype, flag: anytype) void {
        _ = angle;
        _ = flag;
        const s: *v.terrainMovementState = @ptrCast(@alignCast(state));
        if (ctx.value(u8) < 128) s.position[0] = ctx.value(i16);
    }
};
