//! Selects which data block set the 6F014 overlay uses (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: func_800D7890 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Points D_800F569C at D_800F57A0 when `which` is 0, else at D_800F56C8.
pub fn func_800D7890(which: c_int) callconv(.c) void {
    if (which == 0) {
        psx.put(v.g.D_800F569C(), v.g.D_800F57A0());
    } else {
        psx.put(v.g.D_800F569C(), v.g.D_800F56C8());
    }
}
