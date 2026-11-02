//! An event script opcode over an actor set (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B80B4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007D340(actor) for each actor of the script's reference getShort(script + 1).
pub fn func_800B80B4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var set: v.func_800BB68C_t = undefined;
    c.func_800BB68C(psx.conv(u16, c.vs_battle_getShort(script + 1)), psx.cptr(c.func_800BB68C_t, &set));
    while (true) {
        const actor: c_int = c.func_800BB788(psx.cptr(c.func_800BB68C_t, &set));
        if (actor == 1) break;
        c.func_8007D340(actor);
    }
    return 0;
}
