//! An effect-script opcode: func_8006CDB8 on a kind-4 target (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6574 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// If the script's unk0 target is of kind 4, func_8006CDB8 on its id's low byte.
pub fn func_800D6574(script: ?*v.D_800F53B8_t) callconv(.c) void {
    const s = script.?;
    if (s.unkD1C.unk0.unk0 == 4) {
        c.func_8006CDB8(@as(c_int, psx.conv(u8, s.unkD1C.unk0.unk4.unk0)));
    }
}
