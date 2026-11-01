//! zig_diff spec: func_800DFBCC -> func_800DFBCC.func_800DFBCC (771A8.c).
//! Inputs: an actor tile 0-31 and a route table (scratchpad 0x1F8003F4)
//! whose chosen word has one or two edges (an empty word too), each edge's
//! tile matching the actor's half the time, its gate (0x400), closed
//! (0x8000), jump (0x6000, 1-4 steps) and direction (0x1800) bits random;
//! the gate search returns -1 or a gate 2-8 whose closed bit is on or off,
//! the room check -1/0/1, the neighbour check -1/0/1; the area, wall and
//! passage grids (0x1F8003C0, 0x1F8003BC, 0x1F8003C4 with its width) sized
//! for every tile the steps reach (a step below 0 wraps the u8 to 248+);
//! the direction table D_800F175C's first four headings 0-7; the target
//! slot 16, empty, or an actor with a position. The exit copy stub
//! sometimes rewrites the candidate, which the C stores after it.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DFBCC));

pub const classes = [_][:0]const u8{ "one-edge", "two-edges", "no-edge" };

fn small(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u2)) {
        0 => -1,
        1 => 0,
        2 => 1,
        else => ctx.value(c_int),
    };
}

pub const stubs = struct {
    pub fn func_800DFAF8(ctx: *zd.Ctx, a: anytype, b: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = d;
        return if (ctx.value(u2) == 0) -1 else @as(c_int, ctx.value(u4) % 13);
    }
    pub fn func_8008E320(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return small(ctx);
    }
    pub fn func_800D954C(ctx: *zd.Ctx, s: anytype, a: anytype) c_int {
        _ = s;
        _ = a;
        return small(ctx);
    }
    pub fn func_800DF9A8(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = b;
        const p: [*]u8 = @ptrCast(a);
        if (ctx.value(u1) == 1) p[0] = ctx.value(u8);
    }
};

var edges: u32 = 0;

fn edge(ctx: *zd.Ctx, x: u8, z: u8) u16 {
    const ex: u16 = if (ctx.chance(2)) x else ctx.range(u16, 0, 31);
    const ez: u16 = if (ctx.chance(2)) z else ctx.range(u16, 0, 31);
    return (ex << 5) | ez | (@as(u16, ctx.int(u6)) << 10);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.routeEdgeState);
    const x: u8 = ctx.range(u8, 0, 31);
    const z: u8 = ctx.range(u8, 0, 31);
    psx.setBits(&s.tile.p, "x", x);
    psx.setBits(&s.tile.p, "z", z);
    s.target = if (ctx.chance(3)) 16 else ctx.range(u8, 0, 15);
    s.id = if (ctx.chance(2)) 0 else 1;

    const area: c_int = ctx.range(c_int, 0, 3);
    const index: c_int = ctx.range(c_int, 0, 15);
    const table = ctx.alloc([64]u32);
    for (table) |*w| w.* = ctx.int(u32);
    var word: u32 = edge(ctx, x, z);
    if (ctx.chance(2)) word |= @as(u32, edge(ctx, x, z)) << 16 else word &= ~@as(u32, 0x1BE00000);
    if (ctx.chance(10)) word &= ~@as(u32, 0x1BE0);
    // The two "both flags" tests: 0x8400 in either half.
    if (ctx.chance(6)) word |= 0x8400;
    if (ctx.chance(6)) word |= 0x84000000;
    table[@intCast(area * 16 + index)] = word;
    edges = word;
    @as(*u32, @ptrFromInt(0x1F8003F4)).* = psx.addr(table);

    const areaGrid = ctx.alloc([32][32]u16);
    for (areaGrid) |*r| ctx.random.bytes(std.mem.asBytes(r));
    @as(*u32, @ptrFromInt(0x1F8003C0)).* = psx.addr(areaGrid);
    const walls = ctx.alloc([257][32]u16);
    for (walls) |*r| {
        for (r) |*t| t.* = if (ctx.chance(3)) 0x0400 else ctx.int(u16) & ~@as(u16, 0x1C00);
    }
    @as(*u32, @ptrFromInt(0x1F8003BC)).* = psx.addr(walls);
    const passages = ctx.allocBytes(32 * 33, 16);
    for (passages[0 .. 32 * 33]) |*b| b.* = ctx.int(u8);
    @as(*u32, @ptrFromInt(0x1F8003C4)).* = psx.addr(passages);
    @as(*u8, @ptrFromInt(0x1F8003DC)).* = ctx.range(u8, 1, 32);
    var d: usize = 0;
    while (d < 4) : (d += 1) v.g.D_800F175C()[d] = ctx.range(u8, 0, 7);

    var k: usize = 0;
    while (k < 16) : (k += 1) {
        if (ctx.chance(3)) {
            @as(*u32, @ptrFromInt(0x1F80037C + 4 * k)).* = 0;
        } else {
            const actor = ctx.alloc(v.routeEdgeActor);
            const pos = ctx.alloc([3]i16);
            for (pos) |*p| p.* = ctx.int(i16);
            psx.put(&actor.position, &pos[0]);
            @as(*u32, @ptrFromInt(0x1F80037C + 4 * k)).* = psx.addr(actor);
        }
    }
    const globals = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = globals;
    return .{ s, index, area };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (edges & 0x1BE0 == 0) return 2;
    return if (edges & 0x1BE00000 != 0) 1 else 0;
}
