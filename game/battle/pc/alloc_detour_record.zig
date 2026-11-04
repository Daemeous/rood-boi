//! Allocating the detour record (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E7660 (pc/src/battle_movement.c)
/// Allocates the zeroed 32-byte record D_800F5920 (func_800E4288 clears it)
/// and sets its halfword at 0xC to 900; `state` is not used.
pub fn allocDetourRecord(state: ?*anyopaque) callconv(.c) void {
    _ = state;
    const record: ?[*]u8 = @ptrCast(c.vs_main_allocHeap(0x20));
    v.g.D_800F5920().* = psx.addr(record);
    c.func_800E4288(record, 0x20);
    psx.byteAt(u16, record.?, 0xc).* = 0x384;
}
