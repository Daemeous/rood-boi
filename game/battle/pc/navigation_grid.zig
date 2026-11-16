//! Room navigation grid setup (D2: 7E05C.s; oracle pc/src/navigation.c).
const psx = @import("common").psx;
const c = @import("c_navigation");
const v = @import("views_navigation");
const grid_views = @import("views_actor_cleanup"); // the navigation record's type and its global

/// orig: vs_battle_aiInitializeRoomAiMap (pc/src/navigation.c)
/// Builds the navigation grid of the loaded room section: records the section's
/// sub-table addresses in D_800F58C0.., allocates the grid record (its cells after the
/// header, heights 0x800 bytes on), fills a cell-flag halfword and a height for
/// each of the 32x32 tiles (a missing tile adds 0x8000 to the flags of the
/// tile before it: `flags` is not reset between tiles, as in the oracle),
/// takes the grid's settings from the section (150 when there is none) and
/// finishes with vs_battle_aiUpdateRouteGateAvailability. A failed allocation is reported
/// (pc_unimplemented ends the game) and the function returns, where the
/// oracle would write through a null grid.
pub fn buildNavigationGrid() callconv(.c) void {
    const section: ?[*]u8 = @ptrCast(c.func_8008B6FC());
    const address = psx.addr(section); // the nav words below are 4-byte guest addresses
    v.g.D_800F58C0().* = address;
    v.g.D_800F58D8().* = address +% 0x400;
    v.g.D_800F58C8().* = address +% 0x440;
    v.g.D_800F5908().* = address +% 0x450;
    v.g.D_800F5910().* = address +% 0x454;
    v.g.D_800F58CC().* = address +% 0x498;
    const bytes = @sizeOf(grid_views.D_800F58BC_t) + 0x1000;
    const grid: *grid_views.D_800F58BC_t = @ptrCast(@alignCast(c.vs_main_allocHeap(bytes) orelse {
        c.pc_unimplemented("navigation allocation failed");
        return;
    }));
    grid_views.g.vs_battle_aiRoomAiState().* = grid;
    c.vs_core_memoryClearAlignedWords(grid, bytes);
    const cells: [*]u16 = @ptrCast(&grid.cells);
    const heights: [*]u16 = cells + 0x400;
    v.g.D_800F58B8().* = psx.addr(cells);
    v.g.D_800F58D0().* = psx.addr(heights);
    const size = c._getCollisionMapDimensions(0);
    const packed_size: u16 = @truncate(((size & 0xffff) >> 4) | (size >> 12));
    grid.unk4 = packed_size;
    @as(*align(1) u16, @ptrCast(grid)).* = packed_size;
    grid.unk14 = -%(@as(c_int, c.func_8008E4AC()) *% 64);
    var flags: c_uint = 0;
    var i: c_uint = 0;
    while (i < 1024) : (i += 1) {
        const x = i & 31;
        const z = i >> 5;
        if (c.vs_field_collisionCellAt(x, z, 0)) |tile| {
            const raw = tile.*;
            flags = ((raw >> 23) & 15) | (((raw >> 27) & 1) << 4) | (((raw >> 17) & 1) << 5) |
                (((raw >> 28) & 1) << 6) | (((raw >> 31) & 1) << 11) | (((raw >> 30) & 1) << 12) |
                (((raw >> 29) & 1) << 13);
            heights[i] = @truncate(@as(c_uint, @bitCast(c.vs_battle_aiSampleAiTileGroundHeight(@bitCast(x), @bitCast(z)) >> 6)) & 0x3ff);
            if (c.vs_field_findRoomTriggerAtTile(x, z, 0)) |object| {
                if (object[3] < 16 and object[4] < 2) flags |= @as(c_uint, 1) << @as(u5, @intCast(object[4] + 8));
            }
        } else {
            flags |= 0x8000;
        }
        cells[i] = @truncate(flags);
    }
    c.vs_battle_aiBuildAiTerrainHeightsWithWorkspace();
    if (section) |s| {
        const settings = @as(*align(1) const c_uint, @ptrFromInt(@as(usize, v.g.D_800F5908().*))).*;
        @as(*align(1) c_uint, @ptrCast(grid)).* = @as(*align(1) const c_uint, @ptrCast(s + 0x494)).*;
        grid.unkD = @truncate(settings >> 29);
        grid.unk8 = @truncate((settings << 3) >> 24);
        grid.unkA = @truncate((settings << 11) >> 31);
    } else {
        grid.unk8 = 150;
    }
    c.vs_battle_aiUpdateRouteGateAvailability();
}
