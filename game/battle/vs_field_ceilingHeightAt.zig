//! Battle room collision lookup at a cell position (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_ceilingHeightAt (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Looks up the collision value for the cell at (arg0, arg1) in the battle room's collision map.
pub fn vs_field_ceilingHeightAt(arg0: c_int, arg1: c_int) callconv(.c) c_short {
    const collisionMap = psx.get(v.g.vs_battle_roomData().collisionMapSection);
    var var_a2: c_int = 0;
    const new_var: c_int = 8;

    if (collisionMap != null and
        psx.conv(c_uint, arg0 >> 7) < psx.conv(c_uint, @as(c_int, collisionMap.?.width)) and
        psx.conv(c_uint, arg1 >> 7) < psx.conv(c_uint, @as(c_int, collisionMap.?.height)))
    {
        // vs_field_sampleCollisionHeightCode takes its first parameter as u_int: the shifted int is converted.
        var_a2 = @as(c_int, c.vs_field_sampleCollisionHeightCode(psx.conv(c_uint, arg0 >> 5), arg1 >> 5, 1));
        const temp_v1: c_int = (@as(c_int, collisionMap.?.unk4) -% ((var_a2 & 0x7F) *% 2)) *% new_var;
        if (temp_v1 > 0) {
            var_a2 = 0xF810;
        } else {
            var_a2 = (temp_v1 & 0x7FFF) | ((var_a2 & 0x80) << new_var);
        }
    }
    return psx.conv(c_short, var_a2);
}
