//! Frees the block a vs_field_roomEffectInstances entry owns and moves its saved pointer into the slot
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_executeRoomEffectStopScript (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Frees entry `arg1->unk4[0].s32 + 0x40` when it is live, sets its slot to the saved pointer and marks it dead.
pub fn vs_field_executeRoomEffectStopScript(_: ?*v.func_80092F74_t, arg1: ?*v.vs_field_RoomEffectCommand) callconv(.c) c_int {
    // The index is unchecked in the C, so it is computed with wrapping arithmetic and used through psx.at.
    const index: c_int = @as(c_int, arg1.?.operands[0].s32) +% 0x40;
    const entry = psx.at(v.g.vs_field_roomEffectInstances(), index);
    if (entry.unk0 != 0) {
        c.vs_main_freeHeap(psx.get(entry.unk4));
        // psx_copy: copies the 4-byte guest pointer word bit for bit, as the C does.
        const dst: *u32 = @ptrCast(&entry.unk4);
        const src: *const u32 = @ptrCast(&entry.unk8);
        dst.* = src.*;
        entry.unk0 = 0;
    }
    return 0;
}
