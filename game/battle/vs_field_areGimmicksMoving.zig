//! Checks the sixteen entries of vs_field_gimmickInstances for a live one (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");

/// orig: vs_field_areGimmicksMoving (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns 1 if any present entry of vs_field_gimmickInstances has bit 8.0 clear, field 9.0 zero and a nonzero 0x1A, else 0.
pub fn vs_field_areGimmicksMoving() callconv(.c) c_int {
    for (v.g.vs_field_gimmickInstances().*) |slot| {
        if (slot) |entry| {
            if (psx.getBits(entry, "unk8_0") == 0 and psx.getBits(entry, "unk9_0") == 0 and entry.unk1A != 0) {
                return 1;
            }
        }
    }
    return 0;
}
