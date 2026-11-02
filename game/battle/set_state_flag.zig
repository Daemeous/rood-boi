//! Writes one byte of the main state flags, indexed by a signed short (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: vs_battle_setStateFlag (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores `value` at byte `id` of the main state flags; a negative `id` reaches bytes before the block.
pub fn setStateFlag(id: c_short, value: u8) callconv(.c) void {
    // flags[id]: the short index sign-extends, as C converts it to a pointer offset.
    const flags = psx.byteAt(u8, v.g.vs_main_stateFlags(), @as(isize, id));
    flags.* = value;
}
