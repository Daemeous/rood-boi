//! Packs the battle event file index with D_800F4BE0 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800BEB10 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns D_800F4BE0 shifted up 12 bits, OR'd with the event file index, as a short.
pub fn func_800BEB10() callconv(.c) c_short {
    // u16 promotes to int before the shift; the result fits in 32 bits, so the plain shift is exact.
    const hi: c_int = @as(c_int, v.g.D_800F4BE0().*) << 12;
    const lo: c_int = @as(c_int, v.g._evtFile().*);
    return psx.conv(c_short, hi | lo);
}
