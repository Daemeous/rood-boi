//! An effect-script opcode: func_8006CA20 on a target (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D63D0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_8006CA20(id low byte, NULL) for a kind-4 unk0 target, else func_8006CA20(its unk6, its data).
pub fn func_800D63D0(script: ?*v.D_800F53B8_t) callconv(.c) void {
    const s = script.?;
    if (s.unkD1C.unk0.unk0 == 4) {
        c.func_8006CA20(@as(c_int, psx.conv(u8, s.unkD1C.unk0.unk4.unk0)), null);
    } else {
        c.func_8006CA20(s.unkD1C.unk0.unk4.unk6, @ptrCast(&s.unkD1C.unk0.unk4));
    }
}
