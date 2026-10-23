//! Toggles the per-entry flag byte of D_800F1DD8 for an index below D_800F225A or in [0x40, D_800F225B + 0x40) (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_800936F8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Flips the flag byte of entry `arg0` when it is in one of two index ranges; returns 1 if it did, else 0.
pub fn func_800936F8(arg0: c_int) callconv(.c) c_int {
    if (arg0 < @as(c_int, v.g.D_800F225A().*)) {
        // arg0 may be negative here: psx.at indexes without a bounds check, as the C does.
        const entry = psx.at(v.g.D_800F1DD8(), arg0);
        entry.unk2 = @as(u8, @intFromBool(entry.unk2 < 1));
        return 1;
    }
    if ((arg0 >= 0x40) and (arg0 < @as(c_int, v.g.D_800F225B().*) +% 0x40)) {
        const entry = psx.at(v.g.D_800F1DD8(), arg0);
        entry.unk2 = @as(u8, @intFromBool(entry.unk2 < 1));
        return 1;
    }
    return 0;
}
