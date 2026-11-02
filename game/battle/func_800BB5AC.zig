//! Loads the event file named by the script operand at arg0+1 into a 0x1800-byte heap buffer (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BB5AC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads the event file's index from the script, allocates its buffer and loads it; returns 0.
pub fn func_800BB5AC(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    v.g._evtFile().* = c.vs_battle_getShort(arg0 + 1);
    v.g.D_800F4C24().* = c.vs_main_allocHeapR(0x1800);
    c.func_800C00E8(v.g._evtFile().*, v.g.D_800F4C24().*);
    v.g.D_800F4C54().* = 1;
    return 0;
}
