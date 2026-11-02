//! An effect-script opcode: func_800AE68C on the target with a given or random variant (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6628 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// For a kind-4 unkC target: func_800AE68C(its id byte, variant) for a variant below 4, else a random 0..2 other than the last (D_800EC4B8, which keeps it).
pub fn func_800D6628(script: ?*v.D_800F53B8_t, variant: u8) callconv(.c) void {
    const s = script.?;
    if (s.unkD1C.unkC.unk0 == 4) {
        if (variant < 4) {
            c.func_800AE68C(@as(c_int, psx.conv(u8, s.unkD1C.unkC.unk4.unk0)), variant);
        } else {
            var pick: c_int = undefined;
            while (true) {
                pick = @rem(c.rand(), 3);
                if (pick != v.g.D_800EC4B8().*) break;
            }
            v.g.D_800EC4B8().* = psx.conv(u8, pick);
            c.func_800AE68C(@as(c_int, psx.conv(u8, s.unkD1C.unkC.unk4.unk0)), v.g.D_800EC4B8().*);
        }
    }
}
