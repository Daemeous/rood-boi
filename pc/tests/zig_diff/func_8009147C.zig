//! zig_diff spec: func_8009147C -> func_8009147C.func_8009147C (2842C.c).
//! Inputs: a background layout in the arena with width -4..8 (nonzero, so
//! the modulo is defined; negative widths index before `tiles`), height -3..4
//! (negative means no rows), tiles at indices -12..31 with uv.w and/or uv.h
//! zeroed in some, the prim buffer and ordering table in the arena, arg1
//! around the view (edges and a wide range), arg2 small or pinned, arg3 any,
//! and pc_gte_widescreen 0, 1, -1 and 2 (both sides of its truth test).
//! The camera sines are stubs. Mode 0 draws small values, so points land
//! inside and outside the sky bounds. Modes 1-4 pin the first row's
//! screenX/screenY exactly on SKY_LEFT, SKY_RIGHT, -0x40000 and 0x130000,
//! so every ordered comparison in the skip test sees equal sides.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009147C));

pub const classes = [_][:0]const u8{ "no-rows", "sky-edge", "widescreen-off", "widescreen-on" };

/// Set by prepare: 0 random sines, 1..4 pin the first row to one sky bound.
pub var mode: u8 = 0;

fn skyWide(w: i32) i32 {
    return if (w != 0) 54 << 12 else 0;
}

/// The tile at index k (may be negative) of a layout's inline `tiles` array.
fn tileSlot(arg0: *v.vs_battle_backgroundLayout, k: isize) *v.vs_battle_backgroundTile {
    const base: [*]u8 = @ptrCast(&arg0.tiles);
    const size = @sizeOf(v.vs_battle_backgroundTile);
    const p: [*]u8 = if (k < 0)
        base - @as(usize, @intCast(-k)) * size
    else
        base + @as(usize, @intCast(k)) * size;
    return @ptrCast(@alignCast(p));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);

    const w = ctx.edge(i32, &.{ 0, 1, -1, 2 });
    v.g.pc_gte_widescreen().* = w;

    mode = if (ctx.chance(3)) 0 else ctx.range(u8, 1, 4);

    const width = ctx.edge(i32, &.{ -4, -3, -2, -1, 1, 2, 3, 5, 8 });
    const height: i32 = if (mode == 0) ctx.range(i32, -3, 4) else ctx.range(i32, 1, 4);

    // The layout sits 1024 bytes into a block, so negative tile indices stay in memory we own.
    const base = ctx.allocBytes(4096, 16);
    const arg0: *v.vs_battle_backgroundLayout = @ptrCast(@alignCast(base + 1024));
    arg0.width = width;
    arg0.height = height;
    arg0.unk8 = ctx.int(i32);

    var k: isize = -12;
    while (k < 32) : (k += 1) {
        const t = tileSlot(arg0, k);
        if (ctx.chance(3)) {
            t.uv.w = 0;
            t.uv.h = 0;
        } else if (ctx.chance(4)) {
            t.uv.w = 0;
        } else if (ctx.chance(5)) {
            t.uv.h = 0;
        }
    }

    // Prim buffer (at most 4 rows x 33 columns of 40-byte prims plus the tile) and the ordering table.
    const prims = ctx.allocBytes(8192, 16);
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(prims)));
    const ot = ctx.allocBytes(0x3000, 16);
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ot)));

    const arg1: i32 = if (ctx.chance(2))
        ctx.edge(i32, &.{ 0, 32, -32, 1023, -1024, 1024, -1 })
    else
        ctx.range(i32, -(1 << 20), 1 << 20);

    const sl: i32 = -0x40000 - skyWide(w);
    const sr: i32 = 0x180000 + skyWide(w);
    const arg2: i32 = switch (mode) {
        1 => sl,
        2 => -sr,
        3 => 0x40000,
        4 => -0x130000,
        else => ctx.range(i32, -600, 600),
    };

    return .{ arg0, arg1, arg2, ctx.int(i32) };
}

pub fn classify(args: Args) usize {
    if (args[0].?.height <= 0) return 0;
    if (mode != 0) return 1;
    if (v.g.pc_gte_widescreen().* == 0) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn rcos(ctx: *zd.Ctx, a: c_int) c_int {
        _ = a;
        if (mode == 1 or mode == 2) return 0;
        if (mode == 3 or mode == 4) return 1;
        return ctx.value(i13);
    }

    pub fn rsin(ctx: *zd.Ctx, a: c_int) c_int {
        _ = a;
        if (mode == 1) return 1;
        if (mode == 2) return -1;
        if (mode == 3 or mode == 4) return 0;
        return ctx.value(i13);
    }

    // The real AddPrim links the prim into the table, writing its first word.
    pub fn AddPrim(ctx: *zd.Ctx, ot: ?*anyopaque, p: ?*anyopaque) void {
        _ = ot;
        const w: *u32 = @ptrCast(@alignCast(p.?));
        w.* = ctx.value(u32);
    }
};
