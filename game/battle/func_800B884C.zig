//! Walks the record at arg0 + 1 in the battle script, calling func_8007C0AC for each value
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B884C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads a record from arg0 + 1 and calls func_8007C0AC with each entry, indexed by arg0[3].
pub fn func_800B884C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var sp10: v.func_800BB68C_t = undefined;
    const record: [*c]u8 = @ptrFromInt(@intFromPtr(arg0) +% 1);
    c.func_800BB68C(psx.conv(u16, c.vs_battle_getShort(record)), psx.cptr(c.func_800BB68C_t, &sp10));
    while (true) {
        const temp: c_int = c.func_800BB788(psx.cptr(c.func_800BB68C_t, &sp10));
        if (temp == 1) break;
        c.func_8007C0AC(temp, v.g.D_800E9B4C()[arg0[3]]);
    }
    return 0;
}
