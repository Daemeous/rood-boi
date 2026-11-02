//! Forwards a short read from the script at `arg0 + 1` through `func_800BFE50`
//! into `func_8007BE20` (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800BA8BC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads two shorts from the script at `arg0` and passes them on; always returns 0.
pub fn func_800BA8BC(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    // The second argument's call is made first, as the object orders it.
    const second = c.func_800BFE50(psx.conv(c_ushort, c.vs_battle_getShort(psx.at(arg0, 3))));
    c.func_8007BE20(c.vs_battle_getShort(psx.at(arg0, 1)), second);
    return 0;
}
