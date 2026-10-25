//! Battle slot flag clear for the model table at D_800F4538 (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009FFA8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Clears bit unk9_5 of the model in slot `arg0` when its linked slot (unk12) is valid; returns -2 for 0xFF and -1 for a bad link.
pub fn func_8009FFA8(arg0: c_int, _: c_int) callconv(.c) c_int {
    const temp_a0: *v.D_800F4538_t = v.g.D_800F4538()[@intCast(arg0)].?;
    const temp_v1: c_int = @as(c_int, temp_a0.unk0.unk12);

    if (temp_v1 == 0xFF) {
        return -2;
    }

    // temp_v1 > 16 is checked before the index, so the index is in range below.
    if (temp_v1 > 16) {
        return -1;
    }
    const nBones: c_int = @as(c_int, v.g.D_800F4538()[@intCast(temp_v1)].?.unk0.nBones);
    if (nBones == 0) {
        return -1;
    }

    psx.setBits(&temp_a0.unk0, "unk9_5", 0);

    // BUG (original): the C falls off the end with no return statement. The 64-bit
    // object returns %edx, which GCC set to -1 before the nBones test and never
    // changed on this path (the only caller ignores it), so that is what this returns.
    return -1;
}
