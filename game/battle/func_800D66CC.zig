//! An effect-script opcode: func_8006CE70 on a kind-5 target (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D66CC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// If the script's unk18 target is of kind 5, func_8006CE70 on its data.
pub fn func_800D66CC(script: ?*v.D_800F53B8_t) callconv(.c) void {
    const s = script.?;
    if (s.unkD1C.unk18.unk0 == 5) {
        c.func_8006CE70(@ptrCast(&s.unkD1C.unk18.unk4));
    }
}
