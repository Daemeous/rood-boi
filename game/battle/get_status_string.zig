//! Status effect name lookup for the battle status display (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: _getStatusString (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the name of the lowest status flag set in `flags` (bits 5 and up), or the empty string.
pub fn getStatusString(flags: c_uint) callconv(.c) [*c]u8 {
    const table = v.g.vs_battle_statusStrings();
    const bits: c_uint = flags >> 5;
    var i: c_int = 0;
    while (i < 24) : (i += 1) {
        if (((bits >> @as(u5, @intCast(i))) & 1) != 0) {
            const name_index: c_int = psx.at(table, c.VS_statusStrings_INDEX_strDown + i).*;
            return @ptrCast(psx.at(table, name_index));
        }
    }
    return @ptrCast(psx.at(table, c.VS_statusStrings_OFFSET_empty));
}
