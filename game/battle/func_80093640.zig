//! Frees the block a D_800F1DD8 entry owns and moves its saved pointer into the slot
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093640 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Frees entry `arg1->unk4[0].s32 + 0x40` when it is live, sets its slot to the saved pointer and marks it dead.
pub fn func_80093640(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    // The index is unchecked in the C, so it is computed with wrapping arithmetic and used through psx.at.
    const index: c_int = @as(c_int, arg1.?.unk4[0].s32) +% 0x40;
    const entry = psx.at(v.g.D_800F1DD8(), index);
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
