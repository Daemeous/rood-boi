//! The floor and ceiling heights at a map point (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008DDA8 (src/BATTLE/BATTLE.PRG/146C.c)
/// For a point inside the room's collision map: the low half is the floor height of its tile (layer 0, the
/// map's base unk4 less twice the tile's 7-bit height, times 8, with its bit 7 as bit 15), the high half the
/// ceiling's (layer 1; 0xF810 when above the base). Outside the map, or with none: 0x80008000.
pub fn func_8008DDA8(x: c_int, z: c_int) callconv(.c) c_int {
    // The C packs two shorts into an int through a short pointer: [0] is the low half.
    var halves = [2]u16{ 0x8000, 0x8000 };
    const map = psx.get(v.g.vs_battle_roomData().collisionMapSection);
    if (map != null and psx.conv(c_uint, x >> 7) < psx.conv(c_uint, @as(c_int, map.?.width))) {
        if (psx.conv(c_uint, z >> 7) < psx.conv(c_uint, @as(c_int, map.?.height))) {
            var tile: c_int = c.func_80099514(psx.conv(c_uint, x >> 5), z >> 5, 0);
            tile = (((@as(c_int, map.?.unk4) -% ((tile & 0x7F) *% 2)) *% 8) & 0x7FFF) | ((tile & 0x80) << 8);
            halves[0] = psx.conv(u16, tile);
            tile = c.func_80099514(psx.conv(c_uint, x >> 5), z >> 5, 1);
            var height: c_int = @as(c_int, map.?.unk4) -% (tile & 0x7F) *% 2;
            height *%= 8;
            if (height > 0) {
                tile = 0xF810;
            } else {
                height &= 0x7FFF;
                tile = height | ((tile & 0x80) << 8);
            }
            halves[1] = psx.conv(u16, tile);
        }
    }
    return @bitCast(@as(c_uint, halves[0]) | (@as(c_uint, halves[1]) << 16));
}
