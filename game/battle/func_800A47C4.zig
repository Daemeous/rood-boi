//! Checks the sixteen entries of D_800F45E0 for a live one (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");

/// orig: func_800A47C4 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns 1 if any present entry of D_800F45E0 has bit 8.0 clear, field 9.0 zero and a nonzero 0x1A, else 0.
pub fn func_800A47C4() callconv(.c) c_int {
    for (v.g.D_800F45E0().*) |slot| {
        if (slot) |entry| {
            if (psx.getBits(entry, "unk8_0") == 0 and psx.getBits(entry, "unk9_0") == 0 and entry.unk1A != 0) {
                return 1;
            }
        }
    }
    return 0;
}
