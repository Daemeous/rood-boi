//! The 146C flag lookup: finds a table entry matching a pair and returns its state flag (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// vs_field_lockedDoorRoomPairs[i][k] (char[][4]): the static helper get_D_800E84AC, carried here.
fn tableEntry(i: c_int, k: c_int) c_int {
    return @as(c_int, psx.at(@as([*]u8, @ptrCast(psx.Global([23][4]u8, "vs_field_lockedDoorRoomPairs").ptr())), i *% 4 +% k).*);
}

/// orig: vs_field_roomSigilDoorUnlockByte (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the state flag of the first entry in group arg2 whose (0,1) or (2,3) pair matches arg0, arg1; else 0.
pub fn vs_field_roomSigilDoorUnlockByte(arg0: c_int, arg1: c_int, arg2: c_uint) callconv(.c) c_int {
    if (c.pc_table_index_ok("vs_field_lockedDoorIndexRanges", arg2, 7, "sigil door section", arg2, 0) == 0) return 0;
    var i: c_int = @as(c_int, psx.at(v.g.vs_field_lockedDoorIndexRanges(), arg2).*);
    while (i < @as(c_int, psx.at(v.g.vs_field_lockedDoorIndexRanges(), arg2 +% 1).*)) : (i +%= 1) {
        if ((tableEntry(i, 0) == arg0 and tableEntry(i, 1) == arg1) or
            (tableEntry(i, 2) == arg0 and tableEntry(i, 3) == arg1))
        {
            return @as(c_int, psx.at(&v.g.vs_main_stateFlags().unk186, i).*);
        }
    }
    return 0;
}
