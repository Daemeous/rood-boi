//! Finds the nearest entry above `arg2` in the active table, for entries matching `arg0`/`arg1`, less 128 (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");

/// orig: func_800B13CC (src/BATTLE/BATTLE.PRG/44F14.c)
/// Returns the smallest matching entry value above `arg2` (default 3000), less 128, capped at 0.
pub fn func_800B13CC(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) c_int {
    const table = v.g.D_800F45E0();
    var var_a3: c_int = 0xBB8;

    var i: usize = 0;
    while (i < 16) : (i += 1) {
        const temp_v1 = table[i] orelse continue;

        if (psx.getBits(temp_v1, "unk8_0") != 0) continue;
        if (psx.getBits(&temp_v1.unk6C[8], "unk0_3") != 0) continue;
        if (@as(c_int, temp_v1.unk5C) != arg0) continue;
        if (@as(c_int, temp_v1.unk5E) != arg1) continue;

        const temp_v1_2: c_int = @as(c_int, temp_v1.unk1E);

        if (arg2 < temp_v1_2 and temp_v1_2 < var_a3) {
            var_a3 = temp_v1_2;
        }
    }

    var_a3 -%= 128;

    if (var_a3 > 0) {
        var_a3 = 0;
    }

    return var_a3;
}
