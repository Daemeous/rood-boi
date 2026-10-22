//! Unit src/BATTLE/BATTLE.PRG/771A8.c: clears direction bits whose scratchpad offset lies beyond a limit.
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E1850 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Clears each bit of `arg0` whose scratchpad offset, applied to (x, z) from `arg1`, measures above `arg2` by func_800E42EC.
pub fn func_800E1850(arg0: u8, arg1: c_uint, arg2: c_int) callconv(.c) u8 {
    var mask: u8 = arg0;
    const x: c_int = psx.conv(c_int, arg1 & 0xFF);
    const z: c_int = psx.conv(c_int, (arg1 >> 16) & 0xFF);
    const offsets: *v.D_800F16EC_t = @ptrFromInt(0x1F8003EC);

    var i: c_int = 0;
    while (i < 8) : (i += 1) {
        const sh: u5 = @intCast(i);
        if (((@as(c_int, mask) >> sh) & 1) != 0) {
            const entry = psx.at(offsets, i);
            const dx: c_int = @as(c_int, psx.getBits(entry, "dx"));
            const dz: c_int = @as(c_int, psx.getBits(entry, "dz"));
            const dist = c.func_800E42EC(x +% dx, z +% dz);
            if (dist > arg2) {
                mask = psx.conv(u8, @as(c_int, mask) & ~(@as(c_int, 1) << sh));
            }
        }
    }
    return mask;
}
