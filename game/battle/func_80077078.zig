//! Requests an actor's weapon model (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80077078 (src/BATTLE/BATTLE.PRG/146C.c)
/// Requests object data type 1 for slot `index`, model `wepId`, the given flags, the actor's unk44 as its target,
/// material 2 and variant 0; with arg4 bit 0x80 its actor id and unk11 come from arg4's bits 8..11 and 12..15,
/// else the actor id is 0xFF. Returns the data slot.
pub fn func_80077078(actor: ?*v.vs_battle_actor, index: c_int, wepId: c_int, flags: ?*v.vs_battle_objectData_flags, arg4: c_int) callconv(.c) c_int {
    // The C's local leaves the other fields uninitialised, and vs_battle_populateDataSlot copies all of it into
    // the slot: those bytes are 0 here (PORTING.md §11.4).
    var request = std.mem.zeroes(v.vs_battle_objectData);
    request.dataType = 1;
    request.index = psx.conv(i8, index);
    request.modelId = psx.conv(i16, wepId);
    request.unkC = flags.?.*;
    psx.put(&request.unk4, @as(?*anyopaque, psx.get(actor.?.unk44)));
    if (arg4 & 0x80 != 0) {
        request.actorId = psx.conv(u8, (arg4 & 0xF00) >> 8);
        request.unk11 = psx.conv(u8, (arg4 & 0xF000) >> 0xC);
    } else {
        request.actorId = 0xFF;
    }
    request.variant = 0;
    request.material = 2;
    return c.vs_battle_populateDataSlot(@ptrCast(&request));
}
