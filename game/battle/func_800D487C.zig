//! An effect-script opcode: func_800D46DC with whether the script has no effect list (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D487C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800D46DC(unkD1C.unk34 == NULL, script); returns 1.
pub fn func_800D487C(script: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.func_800D46DC(@intFromBool(psx.get(script.?.unkD1C.unk34) == null), @ptrCast(script));
    return 1;
}
