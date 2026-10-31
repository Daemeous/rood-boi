//! Frees a block held by a battle record's unkD1C.unk30 pointer and clears the pointer
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4EC0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Frees the heap block at arg0->unkD1C.unk30, if any, and clears the pointer; always returns 1.
pub fn func_800D4EC0(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    if (psx.get(arg0.?.unkD1C.unk30)) |block| {
        c.vs_main_freeHeapR(@ptrCast(block));
        psx.put(&arg0.?.unkD1C.unk30, null);
    }
    return 1;
}
