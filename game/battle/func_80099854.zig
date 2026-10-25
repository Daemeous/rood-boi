//! Puts a material's object data into the first free object-data slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_80099854 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Fills the first free object-data slot with a type-7 object of `material`; does nothing for material 0.
pub fn func_80099854(arg0: c_int) callconv(.c) void {
    if (arg0 == 0) return;

    // Fields the C never sets are uninitialised in the original; zero them.
    var src = std.mem.zeroes(v.vs_battle_objectData);
    src.dataType = 7;
    src.index = 0;
    src.actorId = 3;
    src.material = psx.conv(u8, arg0);

    const slots = v.g.vs_battle_objectDataSlots();
    var slot: c_int = -1;
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        if (slots[@intCast(i)].dataType == 0) {
            slot = i;
            break;
        }
    }

    if (slot != -1) {
        slots[@intCast(slot)] = src;
    }
}
