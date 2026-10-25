//! Calls func_8009E700 for a battle actor slot whose animation is not 0x63 or 0xC7 (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009E5C4 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Ends the slot's current animation through func_8009E700 unless it is playing animation 0x63 or 0xC7; an empty slot is ended.
pub fn func_8009E5C4(arg0: c_int) callconv(.c) void {
    // arg0 is not checked against the 17 slots: psx.at indexes as the C does.
    const enemy = psx.at(v.g.D_800F4538(), arg0).*;

    // ROOD_PC: an empty slot is ended here, not read through NULL.
    if (enemy == null) {
        c.func_8009E700(arg0, 1);
        return;
    }

    const animationId = enemy.?.animationId;
    if (animationId != 0x63 and animationId != 0xC7) {
        c.func_8009E700(arg0, 1);
    }
}
