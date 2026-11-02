//! Queues a weapon's effect data (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_loadEtm (src/BATTLE/BATTLE.PRG/146C.c)
/// Requests object data type 9 (ETM) for slot `index`, weapon `wepId`, with unk11 = `arg2`.
pub fn loadEtm(index: c_int, wepId: c_int, arg2: c_int) callconv(.c) void {
    // The C's local leaves the other fields uninitialised, and vs_battle_populateDataSlot copies all of it into
    // the slot: those bytes are 0 here (PORTING.md §11.4).
    var request = std.mem.zeroes(v.vs_battle_objectData);
    request.dataType = 9;
    request.index = psx.conv(i8, index);
    request.modelId = psx.conv(i16, wepId);
    request.unk11 = psx.conv(u8, arg2);
    _ = c.vs_battle_populateDataSlot(@ptrCast(&request));
}
