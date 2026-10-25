//! Sets the 0x5AC bit 18 flag on the battle actor in slot `arg0` (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009D854 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets flag unk5AC_18 on the battle actor in slot `arg0`, if the slot is occupied.
pub fn func_8009D854(arg0: c_int) callconv(.c) void {
    // arg0 is not checked against the 17 slots: the C does not check it either.
    const slot = psx.at(v.g.D_800F4538(), arg0);
    if (slot.*) |actor| {
        psx.setBits(actor, "unk5AC_18", 1);
    }
}
