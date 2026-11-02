//! An effect-script opcode: func_80069C6C on a kind-4 target (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6860 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// If the script's unk24 target is of kind 4, func_80069C6C on its id's low byte.
pub fn func_800D6860(script: ?*v.D_800F53B8_t) callconv(.c) void {
    const s = script.?;
    if (s.unkD1C.unk24.unk0 == 4) {
        c.func_80069C6C(@as(c_int, psx.conv(u8, s.unkD1C.unk24.unk4.unk0)));
    }
}
