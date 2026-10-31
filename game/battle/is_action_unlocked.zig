//! Whether a vs_main_actions entry is unlocked (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_isActionUnlocked (src/BATTLE/BATTLE.PRG/146C.c)
/// Returns the unlocked flag of action `arg0` for actions 1..255, 0 otherwise.
pub fn isActionUnlocked(arg0: c_uint) callconv(.c) c_int {
    // Unsigned wrap: arg0 - 1 < 0xFF holds exactly for arg0 in 1..255 (0 wraps high).
    if ((arg0 -% 1) < 0xFF) {
        const actions = v.g.actions();
        return @as(c_int, psx.getBits(&actions[arg0], "unlocked"));
    }
    return 0;
}
