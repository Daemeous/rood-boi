//! An effect-script opcode: func_8009F898 on a kind-4 target (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D65D8 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// For a kind-4 unkC target: func_8009F898(its id's low byte, 1, value), or (.., 0, 0) when value is 0.
pub fn func_800D65D8(script: ?*v.D_800F53B8_t, value: u8) callconv(.c) void {
    const s = script.?;
    if (s.unkD1C.unkC.unk0 == 4) {
        if (value != 0) {
            c.func_8009F898(@as(c_int, psx.conv(u8, s.unkD1C.unkC.unk4.unk0)), 1, value);
        } else {
            c.func_8009F898(@as(c_int, psx.conv(u8, s.unkD1C.unkC.unk4.unk0)), 0, 0);
        }
    }
}
