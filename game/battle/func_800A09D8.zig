//! Sets the flag bit unk5AC_24 on the enemy actor in slot `index` of D_800F4538 (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A09D8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores `value`'s low bit in unk5AC_24 of the actor in slot `index`, if the slot is occupied.
pub fn func_800A09D8(index: c_int, value: c_int) callconv(.c) void {
    // The C does not bounds-check `index` against the 17 slots: psx.at does not either.
    if (psx.at(v.g.D_800F4538(), index).*) |actor| {
        psx.setBits(actor, "unk5AC_24", psx.conv(u1, value));
    }
}
