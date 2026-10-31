//! Clears bit 2 of each of the 16 slot flags that func_800A0BE0 reports (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_146C");

/// orig: func_8006C2A0 (src/BATTLE/BATTLE.PRG/146C.c)
/// For each slot 0..15 with flag bit 4 set, writes the slot back with that bit cleared.
pub fn func_8006C2A0() callconv(.c) void {
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        const temp_v0: c_int = psx.conv(c_int, c.func_800A0BE0(i));
        if ((temp_v0 & 4) != 0) {
            c.func_800A087C(i, temp_v0 & ~@as(c_int, 4));
        }
    }
}
