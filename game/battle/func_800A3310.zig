//! Battle actor lookup by position (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");

/// orig: func_800A3310 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Finds the slot from index 2 up to `arg0` whose actor is flagged and sits at the given position, or 0 if none does.
pub fn func_800A3310(arg0: c_int, arg1: ?*v.SVECTOR) callconv(.c) c_int {
    const table = v.g.D_800F4538();
    var i: c_int = 2;
    while (i < arg0) : (i +%= 1) {
        if (psx.at(table, i).*) |temp| {
            if (psx.getBits(&temp.unk0, "unkA_5") != 0 and
                temp.unk0.position.vx == arg1.?.vx and
                temp.unk0.position.vy == arg1.?.vy and
                temp.unk0.position.vz == arg1.?.vz)
            {
                return i;
            }
        }
    }
    return 0;
}
