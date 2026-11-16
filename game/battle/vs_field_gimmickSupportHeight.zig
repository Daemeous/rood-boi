//! Finds the eligible entry of the 16-slot table vs_field_gimmickInstances for (arg0, arg1)
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");

/// orig: vs_field_gimmickSupportHeight (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Returns the least unk1E among the matching entries less 128, capped at 0, and stores the index of that entry in vs_actors_supportingGimmickIndex.
pub fn vs_field_gimmickSupportHeight(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    var var_t0: c_int = 0xBB8;
    v.g.vs_actors_supportingGimmickIndex().* = 0;

    const table = v.g.vs_field_gimmickInstances();
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        // i < 16: the table has 16 slots.
        const temp = table[@intCast(i)] orelse continue;

        if (psx.getBits(temp, "unk8_0") == 0 and
            @as(c_int, temp.unk5C) == arg0 and
            @as(c_int, temp.unk5E) == arg1 and
            psx.getBits(temp, "unk9_0") == 0 and
            @as(c_int, temp.unk1A) != 0xFE and
            @as(c_int, temp.unk1E) < var_t0)
        {
            var_t0 = @as(c_int, temp.unk1E);
            v.g.vs_actors_supportingGimmickIndex().* = psx.conv(u8, i);
        }
    }

    var_t0 -%= 128;

    if (var_t0 > 0) {
        var_t0 = 0;
    }

    return var_t0;
}
