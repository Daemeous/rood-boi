//! Allocating the detour record (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: vs_battle_aiAllocateOrbitingBossState (pc/src/battle_movement.c)
/// Allocates the zeroed 32-byte record D_800F5920 (vs_core_memoryClearAlignedWords clears it)
/// and sets its halfword at 0xC to 900; `state` is not used.
pub fn allocDetourRecord(state: *ai.vs_battle_aiState) callconv(.c) void {
    _ = state;
    const record: ?[*]u8 = @ptrCast(c.vs_main_allocHeap(0x20));
    v.g.D_800F5920().* = @ptrCast(record);
    c.vs_core_memoryClearAlignedWords(record, 0x20);
    psx.byteAt(u16, record.?, 0xc).* = 0x384;
}
