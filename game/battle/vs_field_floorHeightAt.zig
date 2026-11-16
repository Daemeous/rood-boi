//! Reads the collision map value at a world position for the battle room (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_floorHeightAt (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the collision map entry at world position (arg0, arg1), or 0x8000 outside the map.
pub fn vs_field_floorHeightAt(arg0: c_int, arg1: c_int) callconv(.c) c_short {
    var var_a2: c_int = 0x8000;

    if (psx.get(v.g.vs_battle_roomData().collisionMapSection)) |collisionMap| {
        if (psx.conv(c_uint, arg0 >> 7) < psx.conv(c_uint, @as(c_int, collisionMap.width)) and
            psx.conv(c_uint, arg1 >> 7) < psx.conv(c_uint, @as(c_int, collisionMap.height)))
        {
            // vs_field_sampleCollisionHeightCode takes its first argument as u_int and returns u_char.
            var_a2 = @as(c_int, c.vs_field_sampleCollisionHeightCode(psx.conv(c_uint, arg0 >> 5), arg1 >> 5, 0));
            const unk4: c_int = collisionMap.unk4;
            var_a2 = (((unk4 -% ((var_a2 & 0x7F) *% 2)) *% 8) & 0x7FFF) | ((var_a2 & 0x80) << 8);
        }
    }
    return psx.conv(i16, var_a2);
}
