//! The scene message window's poll: waits for its text, holds it for the timer, then closes it (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CB45C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Polls the scene message window: 1 while its text types out, else counts its hold down and closes it.
pub fn func_800CB45C() callconv(.c) c_int {
    if (v.g.nameWindowText().* != null) {
        if (psx.getBits(&v.g.textBoxes()[7].flags, "done") == 0) {
            return 1;
        }

        if (v.g.nameWindowHeld().* != 0) {
            const timer = v.g.nameWindowTimer();
            if (@as(c_int, timer.*) < 4) {
                timer.* = 0;
            } else {
                timer.* = psx.conv(u8, @as(c_int, timer.*) -% v.g.vs_gametime_tickspeed().*);
            }

            if (timer.* != 0) {
                return -%c.vs_battle_setTextBox(7, null);
            }

            return 0;
        }
        return -%c.vs_battle_setTextBox(7, null);
    }
    return 0;
}
