//! Unloading one actor (D2: 7E05C.s; oracle pc/src/actor_cleanup.c).
const c = @import("c_actor_cleanup");
const v = @import("views_actor_cleanup");
const psx = @import("common").psx;

/// orig: func_800E7454 (pc/src/actor_cleanup.c)
/// Releases an actor node's hold on the navigation data: its bit in
/// D_800F590C, other actors' target ids, the grid record's actor slots and
/// (for nodes flagged 1 or 4) its AI state, its bit in the grid's busy mask
/// and, for types 0xAC and 0xAD, the data block at D_800F5920. Nothing
/// without a grid (D_800F58B8).
pub fn unloadActor(node: [*]u8) callconv(.c) void {
    if (v.g.D_800F58B8().* == 0) return;
    const id = psx.byteAt(c_uint, node, 4).*;
    if (node[0x1c] & 0x14 != 0) v.g.D_800F590C().* -%= @as(c_uint, 1) << @intCast(id & 31);
    const states = v.g.D_800F5878();
    var peer: ?[*]u8 = @ptrCast(v.g.vs_battle_actors()[0]);
    while (peer) |p| : (peer = @ptrFromInt(@as(usize, psx.byteAt(u32, p, 0).*))) {
        const other = states[psx.byteAt(c_uint, p, 4).*].?;
        if (@as(c_uint, @as([*]u8, @ptrCast(other))[0x16]) == id) @as([*]u8, @ptrCast(other))[0x16] = 0x10;
    }
    const grid: [*]u8 = @ptrFromInt(@as(usize, v.g.D_800F58BC().*));
    var offset: usize = 0x34;
    while (offset >= 0x28) : (offset -= 4) {
        if (@as(usize, psx.byteAt(u32, grid, @intCast(offset)).*) == @intFromPtr(node)) psx.byteAt(u32, grid, @intCast(offset)).* = 0;
    }
    if (node[0x1c] & 5 == 0) return;
    const state = states[id];
    c.func_800E5240(id);
    c.func_800E678C(state);
    if (id == 0) return;
    const bits: *align(1) u16 = @ptrCast(grid + 0x10);
    const bit = (@as(c_uint, 1) << @intCast(id & 31)) & bits.*;
    if (bit != 0) {
        bits.* = @truncate(@as(c_uint, bits.*) -% bit);
        grid[0x12] -%= 1;
    }
    node[0xb] &= 0xfc;
    if (state != null) {
        c.func_800D821C(state);
        c.func_800DBF00(state);
        c.vs_main_freeHeap(state);
        states[id] = null;
    }
    const kind = psx.byteAt(u16, node, 0x1a).*;
    if (kind == 0xac or kind == 0xad) {
        const data: ?*anyopaque = @ptrFromInt(@as(usize, v.g.D_800F5920().*));
        v.g.D_800F5920().* = 0;
        c.vs_main_freeHeap(data);
    }
}
