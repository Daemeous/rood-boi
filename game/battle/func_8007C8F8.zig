//! Requests a model (object data type 1) into slot 0 (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007C8F8 (src/BATTLE/BATTLE.PRG/146C.c)
/// Requests object data type 1, slot 0, model `modelId`; returns the data slot it got plus 1.
pub fn func_8007C8F8(modelId: c_int) callconv(.c) c_int {
    // The C's local leaves the other fields uninitialised, and vs_battle_populateDataSlot copies all of it into
    // the slot: those bytes are 0 here (PORTING.md §11.4).
    var request = std.mem.zeroes(v.vs_battle_objectData);
    request.modelId = psx.conv(i16, modelId);
    request.dataType = 1;
    request.index = 0;
    return c.vs_battle_populateDataSlot(@ptrCast(&request)) +% 1;
}
