//! Unlocks a battle action entry (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_setActionUnlocked (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets the unlocked flag of action `action_id` when it is in 1..255.
pub fn setActionUnlocked(action_id: c_uint) callconv(.c) void {
    // Unsigned wrap: action_id == 0 gives 0xFFFFFFFF, which fails the test, as in C.
    if (action_id -% 1 < 0xFF) {
        // action_id is 1..255 here, so the index is within the 256-entry table.
        psx.setBits(&v.g.actions()[action_id], "unlocked", 1);
    }
}
