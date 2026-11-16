//! Copies a battle block to another (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_content_copyWeaponSequence (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies 0x7800 bytes from arg1 to arg0. The block's two words are offsets from its start, so the copy needs no relocation.
pub fn vs_content_copyWeaponSequence(arg0: ?*v.func_8009C378_t, arg1: ?*v.func_8009C378_t) callconv(.c) void {
    c.vs_main_memcpy(arg0, arg1, 0x7800);
}
