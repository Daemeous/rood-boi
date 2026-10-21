//! Finds the first free neighbour cell of (arg0, arg1) and projects its centre (src/BATTLE/BATTLE.PRG/7CD68.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: func_800E5FDC (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Projects the centre of the first free of the four neighbour cells of (arg0, arg1), or returns -1.
pub fn func_800E5FDC(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    const offsets = v.g.D_800F16EC();
    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const entry = &offsets[i * 2];
        const x: c_uint = psx.conv(c_uint, arg0 +% @as(c_int, psx.getBits(entry, "dx")));
        const z: c_uint = psx.conv(c_uint, arg1 +% @as(c_int, psx.getBits(entry, "dz")));
        if (x < 32 and z < 32) {
            const grid = v.g.D_800F58B8().*.?;
            if ((@as(c_int, grid[@as(usize, z)][@as(usize, x)]) & 0x70) == 0) {
                const sx: c_int = psx.conv(c_int, (x << 7) | 0x40);
                const sz: c_int = psx.conv(c_int, (z << 7) | 0x40);
                return @as(c_int, c.func_8008DC7C(sx, sz)) >> 6;
            }
        }
    }
    return -1;
}
