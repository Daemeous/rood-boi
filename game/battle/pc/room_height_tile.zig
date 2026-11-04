//! Height-map tile lookup (D2: 2EA3C.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const v = @import("views_battle_helpers");

/// orig: func_80099514 (pc/src/battle_helpers.c)
/// Unpacks a 4x4 collision tile value at (x, z) from the selected height
/// layer of the room map (the guest pointer at roomData + 0x68): a cell index
/// from the map's row width, the tile's 16-byte pattern, and the cell's
/// height offset (bits 11-15) added to the tile value, keeping its sign bit.
pub fn heightTileValue(x: c_uint, z: c_uint, layer: c_uint) callconv(.c) c_int {
    const map: [*]align(1) i16 = @ptrFromInt(@as(usize, psx.byteAt(u32, v.g.vs_battle_roomData(), 0x68).*));
    const cells: c_uint = psx.conv(c_uint, map[3]);
    const cell: c_uint = psx.conv(c_uint, map[0]) *% (z >> 2) +% (x >> 2);
    const packed_cell: c_uint = @as(u16, @bitCast(map[@as(usize, 4 +% layer *% cells +% cell)]));
    const tiles: [*]const i8 = @ptrCast(map + (4 + @as(usize, cells *% 2)));
    const value: c_int = tiles[((packed_cell & 0x7ff) * 16) +% (z & 3) * 4 +% (x & 3)];
    const biased = @as(c_uint, @bitCast(value)) +% ((packed_cell & 0xf800) >> 9);
    return @bitCast((biased & 0x7f) | (@as(c_uint, @bitCast(value)) & 0x80));
}
