//! Picks the slot in D_800F45E0 that matches arg0/arg1 with the smallest unk1E, or 0 if none (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800A91DC (src/BATTLE/BATTLE.PRG/40564.c)
/// Returns the index of the matching slot with the smallest unk1E (below ONE), or 0 when none matches.
pub fn func_800A91DC(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) c_int {
    var var_t2: c_int = 4096;
    var var_t3: c_int = 0;

    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        const temp = v.g.D_800F45E0()[@intCast(i)];
        if (temp == null) continue;
        const t = temp.?;
        if (psx.getBits(t, "unk8_0") != 0) continue;
        if (arg2 != 0 and t.unk1A != 0) continue;
        if (t.unk12 != 0xFF) continue;
        if (@as(c_int, t.unk6C[8].unk3) != @as(c_int, t.unk12)) continue;
        if (@as(c_int, t.unk5C) != arg0) continue;
        if (@as(c_int, t.unk5E) != arg1) continue;
        if (var_t2 < @as(c_int, t.unk1E)) continue;

        var_t2 = @as(c_int, t.unk1E);
        var_t3 = i;
    }

    if (var_t3 == 0) {
        return 0;
    }

    return var_t3;
}
