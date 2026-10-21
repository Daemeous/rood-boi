//! Flood-fills the 32x32 grid at D_800F58D0 from one cell, stamping each reached cell with the low 10 bits of arg2 (src/BATTLE/BATTLE.PRG/7CD68.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: func_800E5EC0 (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Stamps cell (arg0, arg1) with arg2's low 10 bits, then recurses into each walkable, unstamped 4-neighbour.
pub fn func_800E5EC0(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    const value: c_int = arg2 & 0x3FF;

    {
        const marks = v.g.D_800F58D0().*.?;
        const cell = psx.at(psx.at(marks, arg1), arg0);
        cell.* = psx.conv(u16, (@as(c_int, cell.*) & 0xFC00) | value | 0x2000);
    }

    var i: c_int = 0;
    while (i < 4) : (i +%= 1) {
        const step = psx.at(v.g.D_800F16EC(), i *% 2);
        const x: c_uint = psx.conv(c_uint, arg0 +% @as(c_int, psx.getBits(step, "dx")));
        const z: c_uint = psx.conv(c_uint, arg1 +% @as(c_int, psx.getBits(step, "dz")));

        // x and z are checked below 32 before either grid is indexed.
        if (x < 32 and z < 32 and
            (v.g.D_800F58B8().*.?[@as(usize, z)][@as(usize, x)] & 0x20) != 0 and
            (v.g.D_800F58D0().*.?[@as(usize, z)][@as(usize, x)] & 0x2000) == 0)
        {
            c.func_800E5EC0(psx.conv(c_int, x), psx.conv(c_int, z), arg2);
        }
    }
}
