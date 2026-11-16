//! Sets the section 9 armour byte of one room section entry, or of every entry when none is given (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_setRoomTriggerEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Writes `value` to the unkA byte of `entry`, or of every section 9 entry of the room when `entry` is null.
pub fn vs_field_setRoomTriggerEnabled(arg0: ?*v._mpdRoomSection9, arg1: c_int) callconv(.c) void {
    if (arg0) |entry| {
        entry.unkA = psx.conv(u8, arg1);
    } else {
        const base = psx.get(v.g.vs_battle_roomData().section9);
        if (base) |first| {
            // section9Len / 12 is unsigned; the quotient fits in an int.
            const temp_a3: c_int = psx.conv(c_int, v.g.vs_battle_roomData().header.section9Len / 12);
            var i: c_int = 0;
            while (i < temp_a3) : (i +%= 1) {
                psx.at(first, i).unkA = psx.conv(u8, arg1);
            }
        }
    }
}
