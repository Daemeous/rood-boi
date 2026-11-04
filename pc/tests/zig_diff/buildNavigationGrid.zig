//! zig_diff spec: func_800E6F9C -> pc.navigation_grid.buildNavigationGrid (D2: pc/src/navigation.c).
//! Inputs: the section address func_8008B6FC returns (an arena block of 0x600
//! bytes holding the settings word at +0x450 and the grid word at +0x494, or
//! 0 for no section), the grid block func_800E4288's allocation hands out (0x1038
//! bytes, random), and a pool of tile words, tile objects (halfwords 3 and 4 of
//! each often small enough for the flag bit) that the 1024 per-tile callee
//! stubs hand out (a tile or none, an object or none, the heights random). The
//! allocation always succeeds: with null the C goes on writing through a null
//! grid (the game ends first, in pc_unimplemented), which cannot be run, so
//! the `!grid` test cannot be covered. The 3000-odd calls make hashing the
//! state at each of them too slow: `hash_at_calls` is off, the arguments and
//! the final state still are compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_navigation");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").buildNavigationGrid));

pub const hash_at_calls = false;

pub const classes = [_][:0]const u8{ "no-section", "section" };

var section_address: u32 = 0;
var grid: ?*anyopaque = null;
var tiles: [*]u32 = undefined;
var objects: [*][8]u16 = undefined;
var tile_chance: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const section = ctx.allocBytes(0x600, 16);
    for (section[0..0x600]) |*byte| byte.* = ctx.int(u8);
    section_address = if (ctx.chance(4)) 0 else @intCast(@intFromPtr(section));
    grid = ctx.allocBytes(0x1038, 16);
    for (@as([*]u8, @ptrCast(grid.?))[0..0x1038]) |*byte| byte.* = ctx.int(u8);
    tiles = @ptrCast(@alignCast(ctx.allocBytes(16 * 4, 16)));
    for (0..16) |i| tiles[i] = ctx.int(u32);
    objects = @ptrCast(@alignCast(ctx.allocBytes(8 * 16, 16)));
    for (0..8) |i| {
        for (0..8) |j| objects[i][j] = ctx.int(u16);
        objects[i][3] = ctx.edge(u16, &.{ 0, 5, 15, 16, 0xFFFF });
        objects[i][4] = ctx.edge(u16, &.{ 0, 1, 2, 0xFFFF });
    }
    tile_chance = ctx.edge(u8, &.{ 0, 40, 128, 220, 255 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(section_address != 0);
}

pub const stubs = struct {
    pub fn func_8008B6FC(ctx: *zd.Ctx) c_uint {
        _ = ctx;
        return section_address;
    }

    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = ctx;
        _ = size;
        return grid;
    }

    pub fn func_800E4288(ctx: *zd.Ctx, p: anytype, size: anytype) void {
        _ = ctx;
        _ = p;
        _ = size;
    }

    pub fn _getCollisionMapDimensions(ctx: *zd.Ctx, a: anytype) c_uint {
        _ = a;
        return ctx.value(c_uint);
    }

    pub fn func_8008E4AC(ctx: *zd.Ctx) c_short {
        return ctx.value(c_short);
    }

    pub fn func_8008B764(ctx: *zd.Ctx, x: anytype, z: anytype, a: anytype) ?*c_uint {
        _ = a;
        if (ctx.value(u8) >= tile_chance) return null;
        return &tiles[(x +% z) % 16];
    }

    pub fn func_800E71DC(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        return ctx.value(c_int);
    }

    pub fn func_8008D438(ctx: *zd.Ctx, x: anytype, z: anytype, a: anytype) ?*c_ushort {
        _ = a;
        if (ctx.value(u2) == 0) return null;
        return &objects[(x +% z) % 8][0];
    }

    pub fn func_800E6B4C(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_800DC638(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn pc_unimplemented(ctx: *zd.Ctx, message: anytype) void {
        _ = ctx;
        _ = message;
    }
};
