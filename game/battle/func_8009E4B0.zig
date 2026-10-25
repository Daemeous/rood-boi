//! Battle actor status query for a slot: idle, busy or empty (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009E4B0 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Reports whether the battle actor in slot `arg0` is empty (-1), idle (0) or busy (1).
pub fn func_8009E4B0(arg0: c_int) callconv(.c) c_int {
    const temp_a1: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*;

    if (temp_a1 == null) {
        if (psx.at(v.g.D_800F45E0(), arg0).* == null) {
            return -1;
        }
        return 0;
    }
    const t = temp_a1.?;

    const kind = psx.getBits(&t.unk0, "unk9_0");
    if (kind != 0 and kind != 5 and kind != 11) {
        return 1;
    }

    if (psx.getBits(&t.unkC54, "unk548_16") == 0) {
        var temp_a0: c_int = t.animationId;
        if (temp_a0 >= 100) {
            temp_a0 -%= 100;

            if (psx.conv(c_uint, temp_a0 -% 9) < 4) {
                return 1;
            }

            if (temp_a0 == 0x1C) {
                return 1;
            }

            if (psx.conv(c_uint, temp_a0 -% 0x62) < 4) {
                return 0;
            }
            if (temp_a0 >= 0x40) {
                return 1;
            }
            // D_800F4538[arg0] is read again here and its value is unused.
            _ = psx.at(v.g.D_800F4538(), arg0).*;
            return 0;
        }

        if (temp_a0 == 0x1C) {
            return 1;
        }
        if (temp_a0 == 0x3B) {
            return 1;
        }
        if (psx.conv(c_uint, temp_a0 -% 0x40) < 7) {
            return 1;
        }
        if (psx.conv(c_uint, temp_a0 -% 0x55) < 7) {
            return 1;
        }
        if (temp_a0 == 0x5E) {
            return 1;
        }
    }
    return 0;
}
