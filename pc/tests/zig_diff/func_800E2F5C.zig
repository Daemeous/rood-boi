//! zig_diff spec: func_800E2F5C -> func_800E2F5C.func_800E2F5C (771A8.c).
//! Inputs: a path state whose path word has every direction and turn, a
//! zero count sometimes, changed/visited bits (visited 7, 11, 13, 14 so one
//! more bit fills it), a cell the cell search returns again or not, and both
//! bounds 0 or 1-31 with the actor's x/z at, below and above them (the
//! destination's centre is a tile centre); unk4 both ways; the tile 0-6 in
//! a 12x32 collision grid (a row and a column of room before it: the C
//! reads x or z -1) with map bounds a tile around it; a wall mask 0-15. The
//! passability test is a spec function of an 8-bit mask the inputs pick;
//! the blocker search returns 0-4 (slots 0-3 of the scratchpad table hold
//! path states with their wait timers and 0x30 flags both ways); the
//! neighbour checks return 0/1 and -1/0/1.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E2F5C));

pub const classes = [_][:0]const u8{ "idle", "free-side", "blocked-side" };

/// `ahead` and `point` are built in an uninitialised local: the object leaves
/// the caller's register byte in the status byte (bits 24-31) of the detour
/// point; the port passes 0. Only the x, y, z bytes are compared.
pub const arg_masks = .{
    .{ "func_800D96C8", 2, 0x00FF_FFFF },
    .{ "func_800D954C", 1, 0x00FF_FFFF },
};

var passable: u8 = 0;

fn passTest(state: ?*anyopaque, direction: c_int) callconv(.c) c_int {
    _ = state;
    return (passable >> @intCast(direction & 7)) & 1;
}

pub const stubs = struct {
    pub fn func_800D98E8(ctx: *zd.Ctx, s: anytype, d: anytype) c_int {
        _ = s;
        _ = d;
        return if (ctx.value(u1) == 0) 0 else @as(c_int, ctx.value(u2)) + 1;
    }
    pub fn func_800D96C8(ctx: *zd.Ctx, s: anytype, a: anytype, b: anytype, d: anytype) c_int {
        _ = s;
        _ = a;
        _ = b;
        _ = d;
        return if (ctx.value(u2) == 0) 0 else ctx.value(c_int) | 1;
    }
    pub fn func_800D954C(ctx: *zd.Ctx, s: anytype, a: anytype) c_int {
        _ = s;
        _ = a;
        return switch (ctx.value(u2)) {
            0 => -1,
            1 => 0,
            2 => 1,
            else => ctx.value(c_int),
        };
    }
    pub fn func_800E3D5C(ctx: *zd.Ctx, s: anytype, t: anytype) c_int {
        _ = t;
        const st: *v.movementPathState = @ptrCast(@alignCast(s));
        return if (ctx.value(u2) == 0) psx.getBits(&st.path.bits, "cell") else ctx.value(u4);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.movementPathState);
    const bits = &s.path.bits;
    if (ctx.chance(10)) psx.setBits(bits, "count", 0) else psx.setBits(bits, "count", ctx.range(u7, 1, 127));
    psx.setBits(bits, "visited", if (ctx.chance(2)) ctx.edge(u4, &.{ 7, 11, 13, 14 }) else ctx.int(u4));
    const boundX: u5 = if (ctx.chance(3)) 0 else ctx.range(u5, 1, 31);
    const boundZ: u5 = if (ctx.chance(3)) 0 else ctx.range(u5, 1, 31);
    psx.setBits(bits, "boundX", boundX);
    psx.setBits(bits, "boundZ", boundZ);
    s.unk4 = if (ctx.chance(2)) 0 else ctx.int(u8) | 1;
    psx.setBits(&s.tile.p, "x", ctx.range(u8, 0, 6));
    psx.setBits(&s.tile.p, "z", ctx.range(u8, 0, 6));
    // The destination's tile centre on either side of each bound.
    psx.setBits(&s.destination.p, "x", if (boundX == 0) ctx.int(u8) else @as(u8, boundX) -% ctx.range(u8, 0, 1));
    psx.setBits(&s.destination.p, "z", if (boundZ == 0) ctx.int(u8) else @as(u8, boundZ) -% ctx.range(u8, 0, 1));
    s.x = @as(i16, boundX) * 128 + ctx.range(i16, -1, 1);
    s.z = @as(i16, boundZ) * 128 + ctx.range(i16, -1, 1);
    if (ctx.chance(4)) s.x = ctx.int(i16);
    if (ctx.chance(4)) s.z = ctx.int(i16);

    const grid = ctx.alloc([13][32]u16);
    for (grid) |*row| {
        for (row) |*cell| cell.* = ctx.int(u16);
    }
    // One row and one tile of room before the grid's origin.
    const origin: *u16 = &grid[1][1];
    @as(*u32, @ptrFromInt(0x1F8003C0)).* = psx.addr(origin);
    const bounds = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = bounds;
    bounds.maxX = ctx.range(u8, 0, 8);
    bounds.maxZ = ctx.range(u8, 0, 8);
    bounds.minX = ctx.range(u8, 0, 6);
    bounds.minZ = ctx.range(u8, 0, 6);

    var k: usize = 0;
    while (k < 4) : (k += 1) {
        const other = ctx.alloc(v.movementPathState);
        other.flags = if (ctx.chance(2)) ctx.int(u16) & ~@as(u16, 0x30) else ctx.int(u16);
        other.waitTimer = if (ctx.chance(2)) 0 else ctx.int(u8);
        @as(*u32, @ptrFromInt(0x1F80037C + 4 * k)).* = psx.addr(other);
    }
    s.waitTimer = if (ctx.chance(2)) 0 else ctx.int(u8);
    passable = ctx.int(u8);
    if (ctx.chance(4)) passable = 0;
    if (ctx.chance(4)) passable = 0xFF;
    const mask: c_int = ctx.range(c_int, 0, 15);
    return .{ s, @ptrCast(&passTest), mask };
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    if (psx.getBits(&s.path.bits, "count") == 0) return 0;
    return if (s.unk4 == 0) 1 else 2;
}
