//! Room navigation grid setup (D2: 7E05C.s; oracle pc/src/navigation.c).
const psx = @import("common").psx;
const c = @import("c_navigation");
const v = @import("views_navigation");

fn half(base: [*]u8, offset: usize) *align(1) u16 {
    return @ptrCast(base + offset);
}

/// orig: func_800E6F9C (pc/src/navigation.c)
/// Builds the navigation grid of the loaded room section: records the section's
/// sub-table addresses in D_800F58C0.., allocates the 0x1038-byte grid (cells
/// at +0x38, heights at +0x838), fills a cell-flag halfword and a height for
/// each of the 32x32 tiles (a missing tile adds 0x8000 to the flags of the
/// tile before it: `flags` is not reset between tiles, as in the oracle),
/// takes the grid's settings from the section (150 when there is none) and
/// finishes with func_800DC638. A failed allocation is reported
/// (pc_unimplemented ends the game) and the function returns, where the
/// oracle would write through a null grid.
pub fn buildNavigationGrid() callconv(.c) void {
    const address = c.func_8008B6FC();
    const section: ?[*]u8 = if (address == 0) null else @ptrFromInt(@as(usize, address));
    v.g.D_800F58C0().* = address;
    v.g.D_800F58D8().* = address +% 0x400;
    v.g.D_800F58C8().* = address +% 0x440;
    v.g.D_800F5908().* = address +% 0x450;
    v.g.D_800F5910().* = address +% 0x454;
    v.g.D_800F58CC().* = address +% 0x498;
    const grid: [*]u8 = @ptrCast(c.vs_main_allocHeap(0x1038) orelse {
        c.pc_unimplemented("navigation allocation failed");
        return;
    });
    v.g.D_800F58BC().* = psx.addr(grid);
    c.func_800E4288(grid, 0x1038);
    v.g.D_800F58B8().* = psx.addr(grid + 0x38);
    v.g.D_800F58D0().* = psx.addr(grid + 0x838);
    const heights: [*]align(1) u16 = @ptrCast(grid + 0x838);
    const cells: [*]align(1) u16 = @ptrCast(grid + 0x38);
    const size = c._getCollisionMapDimensions(0);
    const packed_size: u16 = @truncate(((size & 0xffff) >> 4) | (size >> 12));
    half(grid, 4).* = packed_size;
    half(grid, 0).* = packed_size;
    @as(*align(1) c_int, @ptrCast(grid + 0x14)).* = -%(@as(c_int, c.func_8008E4AC()) *% 64);
    var flags: c_uint = 0;
    var i: c_uint = 0;
    while (i < 1024) : (i += 1) {
        const x = i & 31;
        const z = i >> 5;
        if (c.func_8008B764(x, z, 0)) |tile| {
            const raw = tile.*;
            flags = ((raw >> 23) & 15) | (((raw >> 27) & 1) << 4) | (((raw >> 17) & 1) << 5) |
                (((raw >> 28) & 1) << 6) | (((raw >> 31) & 1) << 11) | (((raw >> 30) & 1) << 12) |
                (((raw >> 29) & 1) << 13);
            heights[i] = @truncate(@as(c_uint, @bitCast(c.func_800E71DC(@bitCast(x), @bitCast(z)) >> 6)) & 0x3ff);
            if (c.func_8008D438(x, z, 0)) |object| {
                if (object[3] < 16 and object[4] < 2) flags |= @as(c_uint, 1) << @as(u5, @intCast(object[4] + 8));
            }
        } else {
            flags |= 0x8000;
        }
        cells[i] = @truncate(flags);
    }
    c.func_800E6B4C();
    if (section) |s| {
        const settings = @as(*align(1) const c_uint, @ptrFromInt(@as(usize, v.g.D_800F5908().*))).*;
        @as(*align(1) c_uint, @ptrCast(grid)).* = @as(*align(1) const c_uint, @ptrCast(s + 0x494)).*;
        grid[0xd] = @truncate(settings >> 29);
        half(grid, 8).* = @truncate((settings << 3) >> 24);
        grid[0xa] = @truncate((settings << 11) >> 31);
    } else {
        half(grid, 8).* = 150;
    }
    c.func_800DC638();
}
