//! Battle script helper: walks the list read from a script's word at offset 1 and calls func_8007D08C for each entry (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BAD78 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Walks the list at a script's word 1, calling func_8007D08C for each entry with the script's byte 3.
pub fn func_800BAD78(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var sp10: v.func_800BB68C_t = undefined;

    c.func_800BB68C(psx.conv(u16, c.vs_battle_getShort(psx.at(arg0, 1))), psx.cptr(c.func_800BB68C_t, &sp10));
    while (true) {
        const temp_a0: c_int = @as(c_int, c.func_800BB788(psx.cptr(c.func_800BB68C_t, &sp10)));
        if (temp_a0 == 1) break;
        _ = c.func_8007D08C(temp_a0, @as(c_int, arg0[3]));
    }
    return 0;
}
