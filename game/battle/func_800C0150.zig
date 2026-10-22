//! Battle slot pass: calls `func_800A0204` for each of slots 0..16 that `func_8007CF64` reports (src/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: func_800C0150 (src/BATTLE/BATTLE.PRG/573B8.c)
/// For each slot 0..16 that `func_8007CF64` returns an actor for, calls `func_800A0204(slot, 1, 0, 0)`.
pub fn func_800C0150() callconv(.c) void {
    var i: i16 = 0;
    while (i < 17) : (i +%= 1) {
        if (c.func_8007CF64(@as(c_int, i)) != null) {
            c.func_800A0204(@as(c_int, i), 1, 0, 0);
        }
    }
}
