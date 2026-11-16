//! Replaces the 0x800-byte heap block held in vs_field_weatherParticles (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008E8F8 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Frees the block at vs_field_weatherParticles if there is one, then allocates a new 0x800-byte block into it.
pub fn func_8008E8F8() callconv(.c) void {
    const block = v.g.vs_field_weatherParticles();
    if (block.* != null) {
        c.vs_main_freeHeap(block.*);
    }
    const raw: ?*align(2) anyopaque = @alignCast(c.vs_main_allocHeap(0x800));
    block.* = @ptrCast(raw);
}
