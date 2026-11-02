//! The floor height at a map point, blended with the neighbour tiles toward it (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// The C's ABS macro: `x >= 0 ? x : -x` (wrapping at INT_MIN).
fn abs(x: c_int) c_int {
    return if (x >= 0) x else -%x;
}

/// orig: func_8008DA24 (src/BATTLE/BATTLE.PRG/146C.c)
/// The floor height (the map's base unk4 less twice the tile's 7-bit height, times 8) of the tile under (x, z),
/// blended by NormalClip weights with the x and z neighbours on the point's side of the tile (a neighbour
/// outside the map or 0x40 or more away counts as the tile itself); bit 7 of the tile goes to bit 15.
/// -0x8000 outside the map; 0x8000 with no map.
pub fn func_8008DA24(x_arg: c_int, z_arg: c_int) callconv(.c) i16 {
    var x = x_arg;
    var z = z_arg;
    var tile: c_int = 0x8000;
    if (psx.get(v.g.vs_battle_roomData().collisionMapSection) != null) {
        const fx = x & 0x1F;
        x >>= 5;
        const fz = z & 0x1F;
        z >>= 5;
        const map = psx.get(v.g.vs_battle_roomData().collisionMapSection).?;
        if (psx.conv(c_uint, x >> 2) >= psx.conv(c_uint, @as(c_int, map.width)) or
            psx.conv(c_uint, z >> 2) >= psx.conv(c_uint, @as(c_int, map.height)))
        {
            return -0x8000;
        }
        tile = c.func_80099514(psx.conv(c_uint, x), z, 0);
        var height: c_int = (@as(c_int, map.unk4) -% ((tile & 0x7F) *% 2)) *% 8;
        var side_x: c_int = if (fx < 0x10) x -% 1 else x +% 1;
        var side_z: c_int = z -% 1;
        if (fz >= 0x10) {
            side_z = z +% 1;
        }
        if (psx.conv(c_uint, side_x >> 2) < psx.conv(c_uint, @as(c_int, map.width))) {
            side_x = (@as(c_int, map.unk4) -% ((c.func_80099514(psx.conv(c_uint, side_x), z, 0) & 0x7F) *% 2)) *% 8;
            if (abs(side_x -% height) >= 0x40) {
                side_x = height;
            }
        } else {
            side_x = height;
        }
        if (psx.conv(c_uint, side_z >> 2) < psx.conv(c_uint, @as(c_int, map.height))) {
            side_z = (@as(c_int, map.unk4) -% ((c.func_80099514(psx.conv(c_uint, x), side_z, 0) & 0x7F) *% 2)) *% 8;
            if (abs(side_z -% height) >= 0x40) {
                side_z = height;
            }
        } else {
            side_z = height;
        }
        const offsets = abs(fx -% 0x10) | (abs(fz -% 0x10) << 0x10);
        const weight_z = c.NormalClip(0, 0x20, offsets);
        const weight_x = c.NormalClip(0, offsets, 0x200000);
        var weight = 0x400 -% weight_x;
        weight -%= weight_z;
        if (weight >= 0) {
            weight = side_z *% weight_z +% side_x *% weight_x +% height *% weight;
            height = @divTrunc(weight, 1024);
        }
        tile = (height & 0x7FFF) | ((tile & 0x80) << 8);
    }
    return psx.conv(i16, tile);
}
